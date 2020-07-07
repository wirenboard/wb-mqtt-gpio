#include "config.h"
#include "gpio_driver.h"
#include "log.h"
#include "utils.h"

#include <wblib/signal_handling.h>
#include <wblib/wbmqtt.h>

#include <getopt.h>

using namespace std;

using PGpioDriver = unique_ptr<TGpioDriver>;

#define LOG(logger) ::logger.Log() << "[gpio] "

const auto WBMQTT_DB_FILE             = "/var/lib/wb-mqtt-gpio/libwbmqtt.db";
const auto GPIO_DRIVER_INIT_TIMEOUT_S = chrono::seconds(5);
const auto GPIO_DRIVER_STOP_TIMEOUT_S = chrono::seconds(5); // topic cleanup can take a lot of time

namespace
{
    void PrintUsage()
    {
        cout << "Usage:" << endl
             << " wb-homa-gpio [options]" << endl
             << "Options:" << endl
             << "  -d level     enable debuging output:" << endl
             << "                 1 - gpio only;" << endl
             << "                 2 - mqtt only;" << endl
             << "                 3 - both;" << endl
             << "                 negative values - silent mode (-1, -2, -3))" << endl
             << "  -c config    config file" << endl
             << "  -p port      MQTT broker port (default: 1883)" << endl
             << "  -h IP        MQTT broker IP (default: localhost)" << endl
             << "  -u user      MQTT user (optional)" << endl
             << "  -P password  MQTT user password (optional)" << endl
             << "  -T prefix    MQTT topic prefix (optional)" << endl;
    }

    void ParseCommadLine(int                           argc,
                         char*                         argv[],
                         WBMQTT::TMosquittoMqttConfig& mqttConfig,
                         string&                       customConfig)
    {
        int debugLevel = 0;
        int c;

        while ((c = getopt(argc, argv, "d:c:h:p:u:P:T:")) != -1) {
            switch (c) {
            case 'd':
                debugLevel = stoi(optarg);
                break;
            case 'c':
                customConfig = optarg;
                break;
            case 'p':
                mqttConfig.Port = stoi(optarg);
                break;
            case 'h':
                mqttConfig.Host = optarg;
                break;
            case 'T':
                mqttConfig.Prefix = optarg;
                break;
            case 'u':
                mqttConfig.User = optarg;
                break;
            case 'P':
                mqttConfig.Password = optarg;
                break;

            case '?':
            default:
                PrintUsage();
                exit(2);
            }
        }

        switch (debugLevel) {
        case 0:
            break;
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
            cout << "Invalid -d parameter value " << debugLevel << endl;
            PrintUsage();
            exit(2);
        }

        if (optind < argc) {
            for (int index = optind; index < argc; ++index) {
                cout << "Skipping unknown argument " << argv[index] << endl;
            }
        }
    }
} // namespace

int main(int argc, char* argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig{};
    mqttConfig.Id = TGpioDriver::Name;

    string                 configFileName;
    WBMQTT::TPromise<void> initialized;

    WBMQTT::SetThreadName("main");
    WBMQTT::SignalHandling::Handle({SIGINT, SIGTERM, SIGHUP});
    WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] { WBMQTT::SignalHandling::Stop(); });

    /* if signal arrived before driver is initialized:
        wait some time to initialize and then exit gracefully
        else if timed out: exit with error
    */
    WBMQTT::SignalHandling::SetWaitFor(GPIO_DRIVER_INIT_TIMEOUT_S, initialized.GetFuture(), [&] {
        LOG(Error) << "Driver takes too long to initialize. Exiting.";
        exit(1);
    });

    /* if handling of signal takes too much time: exit with error */
    WBMQTT::SignalHandling::SetOnTimeout(GPIO_DRIVER_STOP_TIMEOUT_S, [&] {
        LOG(Error) << "Driver takes too long to stop. Exiting.";
        exit(2);
    });
    WBMQTT::SignalHandling::Start();

    ParseCommadLine(argc, argv, mqttConfig, configFileName);

    auto mqttDriver = WBMQTT::NewDriver(
        WBMQTT::TDriverArgs{}
            .SetBackend(WBMQTT::NewDriverBackend(WBMQTT::NewMosquittoMqttClient(mqttConfig)))
            .SetId(mqttConfig.Id)
            .SetUseStorage(true)
            .SetReownUnknownDevices(true)
            .SetStoragePath(WBMQTT_DB_FILE));

    mqttDriver->StartLoop();
    WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] {
        mqttDriver->StopLoop();
        mqttDriver->Close();
    });

    mqttDriver->WaitForReady();

    try {
        PGpioDriver        gpioDriver;
        condition_variable startedCv;
        mutex              startedCvMtx;

        auto start = [&] {
            {
                lock_guard<mutex> lk(startedCvMtx);
                gpioDriver = WBMQTT::MakeUnique<TGpioDriver>(
                    mqttDriver,
                    LoadConfig("/etc/wb-homa-gpio.conf",
                               configFileName,
                               "/usr/share/wb-mqtt-confed/schemas/wb-homa-gpio.schema.json"));
                Utils::ClearMappingCache();
                gpioDriver->Start();
            }
            startedCv.notify_all();
        };

        auto stop = [&] {
            unique_lock<mutex> lk(startedCvMtx);
            startedCv.wait(lk, [&] { return bool(gpioDriver); });
            gpioDriver->Stop();
            gpioDriver->Clear();
            gpioDriver.reset();
        };

        start();

        WBMQTT::SignalHandling::OnSignal(SIGHUP, [&] {
            LOG(Info) << "Reloading config...";
            stop();
            start();
        });

        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, stop);

        initialized.Complete();
        WBMQTT::SignalHandling::Wait();
    } catch (const std::exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }

    return 0;
}
