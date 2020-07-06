#include "gpio_driver.h"
#include "log.h"
#include "config.h"
#include "exceptions.h"
#include "utils.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>


using namespace std;

using PGpioDriver = unique_ptr<TGpioDriver>;

#define LOG(logger) ::logger.Log() << "[gpio] "

const auto WBMQTT_DB_FILE = "/var/lib/wb-homa-gpio/libwbmqtt.db";
const auto GPIO_DRIVER_INIT_TIMEOUT_S = chrono::seconds(5);
const auto GPIO_DRIVER_STOP_TIMEOUT_S = chrono::seconds(5); // topic cleanup can take a lot of time


int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig {};
    mqttConfig.Id = TGpioDriver::Name;

    string configFileName;
    WBMQTT::TPromise<void> initialized;

    WBMQTT::SetThreadName("main");
    WBMQTT::SignalHandling::Handle({ SIGINT, SIGTERM, SIGHUP });
    WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{
        WBMQTT::SignalHandling::Stop();
    });

    /* if signal arrived before driver is initialized:
        wait some time to initialize and then exit gracefully
        else if timed out: exit with error
    */
    WBMQTT::SignalHandling::SetWaitFor(GPIO_DRIVER_INIT_TIMEOUT_S, initialized.GetFuture(), [&]{
        LOG(Error) << "Driver takes too long to initialize. Exiting.";
        exit(1);
    });

    /* if handling of signal takes too much time: exit with error */
    WBMQTT::SignalHandling::SetOnTimeout(GPIO_DRIVER_STOP_TIMEOUT_S, [&]{
        LOG(Error) << "Driver takes too long to stop. Exiting.";
        exit(2);
    });
    WBMQTT::SignalHandling::Start();

    int c, debug = 0;
    //~ int digit_optind = 0;
    //~ int aopt = 0, bopt = 0;
    //~ char *copt = 0, *dopt = 0;
    while ( (c = getopt(argc, argv, "c:h:p:d:T:u:P:")) != -1) {
        //~ int this_option_optind = optind ? optind : 1;
        switch (c) {
            case 'c':
                printf ("option c with value '%s'\n", optarg);
                configFileName = optarg;
                break;

            case 'p':
                printf ("option p with value '%s'\n", optarg);
                mqttConfig.Port = stoi(optarg);
                break;

            case 'h':
                printf ("option h with value '%s'\n", optarg);
                mqttConfig.Host = optarg;
                break;

            case 'T':
                printf ("option T with value '%s'\n", optarg);
                mqttConfig.Prefix = optarg;
                break;

            case 'u':
                printf ("option u with value '%s'\n", optarg);
                mqttConfig.User = optarg;
                break;

            case 'P':
                printf ("option P with value '%s'\n", optarg);
                mqttConfig.Password = optarg;
                break;

            case 'd':
                printf ("option d with value '%s'\n", optarg);
                debug = stoi(optarg);
                break;

            case '?':
                break;

            default:
                printf ("?? Getopt returned character code 0%o ??\n", c);
        }
    }
    //~ if (optind < argc) {
    //~ printf ("non-option ARGV-elements: ");
    //~ while (optind < argc)
    //~ printf ("%s ", argv[optind++]);
    //~ printf ("\n");
    //~ }

    switch(debug) {
        case -1:
            Info.SetEnabled(false);
            break;

        case -2:
            WBMQTT::Info.SetEnabled(false);
            break;

        case -3:
            WBMQTT::Info.SetEnabled(false);
            Info.SetEnabled(false);
            break;

        case 1:
            Debug.SetEnabled(true);
            break;

        case 2:
            WBMQTT::Debug.SetEnabled(true);
            break;

        case 3:
            WBMQTT::Debug.SetEnabled(true);
            Debug.SetEnabled(true);
            break;

        default:
            break;
    }

    if (configFileName.empty()) {
        LOG(Error) << "Please specify config file with -c option";
        return 1;
    }

    auto mqttDriver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
        .SetBackend(WBMQTT::NewDriverBackend(WBMQTT::NewMosquittoMqttClient(mqttConfig)))
        .SetId(mqttConfig.Id)
        .SetUseStorage(true)
        .SetReownUnknownDevices(true)
        .SetStoragePath(WBMQTT_DB_FILE)
    );

    mqttDriver->StartLoop();
    WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{
        mqttDriver->StopLoop();
        mqttDriver->Close();
    });

    mqttDriver->WaitForReady();

    try {
        PGpioDriver         gpioDriver;
        condition_variable  startedCv;
        mutex               startedCvMtx;

        auto start = [&]{
            {
                lock_guard<mutex> lk(startedCvMtx);
                gpioDriver = WBMQTT::MakeUnique<TGpioDriver>(mqttDriver, TGpioDriverConfig(configFileName, "/usr/share/wb-mqtt-confed/schemas/wb-homa-gpio.schema.json"));
                Utils::ClearMappingCache();
                gpioDriver->Start();
            }
            startedCv.notify_all();
        };

        auto stop = [&]{
            unique_lock<mutex> lk(startedCvMtx);
            startedCv.wait(lk, [&]{ return bool(gpioDriver); });
            gpioDriver->Stop();
            gpioDriver->Clear();
            gpioDriver.reset();
        };

        start();

        WBMQTT::SignalHandling::OnSignal(SIGHUP, [&]{
            LOG(Info) << "Reloading config...";
            stop();
            start();
        });

        WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, stop);

        initialized.Complete();
        WBMQTT::SignalHandling::Wait();
    } catch (const TGpioDriverException & e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    } catch (const WBMQTT::TBaseException & e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }

    return 0;
}
