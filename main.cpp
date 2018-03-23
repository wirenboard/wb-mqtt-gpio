#include "gpio_driver.h"
#include "log.h"
#include "config.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>


using namespace std;

#define LOG(logger) ::logger.Log() << "[main] "

const auto WBMQTT_DB_FILE = "/var/lib/wb-homa-gpio/libwbmqtt.db";


int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig {};
    mqttConfig.Id = TGpioDriver::Name;

    string configFileName;

    WBMQTT::SignalHandling::Handle({ SIGINT, SIGTERM });
    WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName("main");

    int c, debug;
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
    WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{ mqttDriver->StopLoop(); });

    mqttDriver->WaitForReady();

    TGpioDriver driver(mqttDriver, GetConvertConfig(configFileName));
    driver.Start();

    WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{ driver.Stop(); });
    WBMQTT::SignalHandling::Start();
    WBMQTT::SignalHandling::Wait();

    return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto


// 2420 2032
// 6008 2348 1972
