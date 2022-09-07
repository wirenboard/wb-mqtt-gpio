#include "config.h"
#include "gpio_driver.h"
#include "log.h"
#include "utils.h"
#include "interruption_context.h"

#include <wblib/signal_handling.h>
#include <wblib/wbmqtt.h>
#include <wblib/json_utils.h>

#include <unordered_set>
#include <getopt.h>
#include <sys/utsname.h>

// From LSB
#define EXIT_INVALIDARGUMENT 2 // Invalid or excess arguments
#define EXIT_NOTCONFIGURED 6   // The program is not configured

using namespace std;

using PGpioDriver = unique_ptr<TGpioDriver>;

#define LOG(logger) ::logger.Log() << "[gpio] "

const auto WBMQTT_DB_FILE       = "/var/lib/wb-mqtt-gpio/libwbmqtt.db";
const auto CONFIG_FILE          = "/etc/wb-mqtt-gpio.conf";
const auto SYSTEM_CONFIGS_DIR   = "/var/lib/wb-mqtt-gpio/conf.d";
const auto CONFIG_SCHEMA_FILE   = "/var/lib/wb-mqtt-confed/schemas/wb-mqtt-gpio.schema.json";
const auto GPIO_DRIVER_INIT_TIMEOUT_S = chrono::seconds(30);
const auto GPIO_DRIVER_STOP_TIMEOUT_S = chrono::seconds(60); // topic cleanup can take a lot of time

namespace
{
    enum class DebugLevel : int32_t
    {
        NONE = 0,
        SILENT_GPIO = -1,
        SILENT_MQTT = -2,
        SILENT_GPIO_MQTT = -3,
        DEBUG_GPIO = 1,
        DEBUG_MQTT = 2,
        DEBUG_GPIO_MQTT = 3
    };
    DebugLevel CommandLineDebugLevel = DebugLevel::NONE;

    void PrintUsage()
    {
        cout << "Usage:" << endl
             << " wb-mqtt-gpio [options]" << endl
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
             << "  -T prefix    MQTT topic prefix (optional)" << endl
             << "  -j           Make JSON for wb-mqtt-confed from /etc/wb-mqtt-gpio.conf" << endl
             << "  -J           Make /etc/wb-mqtt-gpio.conf from wb-mqtt-confed output" << endl;
    }

    void ParseCommandLine(int                           argc,
                          char*                         argv[],
                          WBMQTT::TMosquittoMqttConfig& mqttConfig,
                          string&                       customConfig)
    {
        int c;

        while ((c = getopt(argc, argv, "d:c:h:p:u:P:T:jJ")) != -1) {
            switch (c) {
            case 'd':
                CommandLineDebugLevel = static_cast<DebugLevel>(stoi(optarg));
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
            case 'j':
                try {
                    MakeJsonForConfed(CONFIG_FILE, SYSTEM_CONFIGS_DIR, CONFIG_SCHEMA_FILE);
                    exit(EXIT_SUCCESS);
                } catch (const std::exception& e) {
                    LOG(Error) << "FATAL: " << e.what();
                    exit(EXIT_FAILURE);
                }
            case 'J':
                try {
                    MakeConfigFromConfed(SYSTEM_CONFIGS_DIR, CONFIG_SCHEMA_FILE);
                    exit(EXIT_SUCCESS);
                } catch (const std::exception& e) {
                    LOG(Error) << "FATAL: " << e.what();
                    exit(EXIT_FAILURE);
                }
            case '?':
            default:
                PrintUsage();
                exit(EXIT_INVALIDARGUMENT);
            }
        }

        if (optind < argc) {
            for (int index = optind; index < argc; ++index) {
                cout << "Skipping unknown argument " << argv[index] << endl;
            }
        }
    }

    struct TLinuxKernelVersion
    {
        int Version = -1;
        int Patchlevel = -1;
    };

    TLinuxKernelVersion GetLinuxKernelVersion()
    {
        utsname buf{};
        uname(&buf);
        TLinuxKernelVersion res;
        auto v = WBMQTT::StringSplit(buf.release, ".");
        res.Version = stol(v[0].c_str());
        if (v.size() > 1) {
            res.Patchlevel = stoul(v[1].c_str());
        }
        return res;
    }

    // Since linux kernel v5.7-rc1 interrupt events have monotonic clock timestamp.
    // Prior to v5.7-rc1 they had realtime clock timestamp.
    // https://github.com/torvalds/linux/commit/f8850206e160bfe35de9ca2e726ab6d6b8cb77dd
    bool HasMonotonicClockForInterruptionTimestamps(const TLinuxKernelVersion& kernel)
    {
        if (kernel.Version < 5) {
            return false;
        }
        if (kernel.Version > 5) {
            return true;
        }
        return (kernel.Patchlevel >= 7);
    }

    // In kernels prior to v5.3-rc3 there is a bug with events polarity and GPIOHANDLE_REQUEST_ACTIVE_LOW.
    // A kernel sends events with inverted polarity not with requested.
    // https://github.com/torvalds/linux/commit/223ecaf140b1dd1c1d2a1a1d96281efc5c906984
    bool HasActiveLowEventsBug(const TLinuxKernelVersion& kernel)
    {
        if (kernel.Version > 5) {
            return false;
        }
        if (kernel.Version < 5) {
            return true;
        }
        return (kernel.Patchlevel < 3);
    }

    void SetDebugLevel(DebugLevel level)
    {
        switch (level) {
            case DebugLevel::NONE:
                break;
            case DebugLevel::SILENT_GPIO:
                Info.SetEnabled(false);
                break;

            case DebugLevel::SILENT_MQTT:
                WBMQTT::Info.SetEnabled(false);
                break;

            case DebugLevel::SILENT_GPIO_MQTT:
                WBMQTT::Info.SetEnabled(false);
                Info.SetEnabled(false);
                break;

            case DebugLevel::DEBUG_GPIO:
                Debug.SetEnabled(true);
                break;

            case DebugLevel::DEBUG_MQTT:
                WBMQTT::Debug.SetEnabled(true);
                break;

            case DebugLevel::DEBUG_GPIO_MQTT:
                WBMQTT::Debug.SetEnabled(true);
                Debug.SetEnabled(true);
                break;

            default:
                cout << "Invalid -d parameter value " << static_cast<int32_t>(CommandLineDebugLevel) << endl;
                PrintUsage();
                exit(EXIT_INVALIDARGUMENT);
        }
    }
} // namespace

int main(int argc, char* argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig;
    mqttConfig.Id = TGpioDriver::Name;

    string configFileName;
    ParseCommandLine(argc, argv, mqttConfig, configFileName);

    WBMQTT::TPromise<void> initialized;

    WBMQTT::SetThreadName("wb-mqtt-gpio");
    WBMQTT::SignalHandling::Handle({SIGINT, SIGTERM});
    WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] { WBMQTT::SignalHandling::Stop(); });

    /* if signal arrived before driver is initialized:
        wait some time to initialize and then exit gracefully
        else if timed out: exit with error
    */
    WBMQTT::SignalHandling::SetWaitFor(GPIO_DRIVER_INIT_TIMEOUT_S, initialized.GetFuture(), [&] {
        LOG(Error) << "Driver takes too long to initialize. Exiting.";
        exit(EXIT_FAILURE);
    });

    /* if handling of signal takes too much time: exit with error */
    WBMQTT::SignalHandling::SetOnTimeout(GPIO_DRIVER_STOP_TIMEOUT_S, [&] {
        LOG(Error) << "Driver takes too long to stop. Exiting.";
        exit(EXIT_FAILURE);
    });
    WBMQTT::SignalHandling::Start();

    TLinuxKernelVersion kernel = GetLinuxKernelVersion();

    if (HasMonotonicClockForInterruptionTimestamps(kernel)) {
        LOG(Info) << "Kernel uses monotonic clock for interrupt timestamps";
        TInterruptionContext::SetMonotonicClockForInterruptTimestamp();
    }

    TGpioDriverConfig config;

    try {
        config = LoadConfig(CONFIG_FILE,
                            configFileName,
                            SYSTEM_CONFIGS_DIR,
                            CONFIG_SCHEMA_FILE,
                            { HasActiveLowEventsBug(kernel) });
    } catch (const std::exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return EXIT_NOTCONFIGURED;
    }

    if (CommandLineDebugLevel != DebugLevel::NONE) {
        SetDebugLevel(CommandLineDebugLevel);
    } else if (config.Debug) {
        SetDebugLevel(DebugLevel::DEBUG_GPIO);
    }

    try {
        auto mqttDriver = WBMQTT::NewDriver(
            WBMQTT::TDriverArgs{}
                .SetBackend(WBMQTT::NewDriverBackend(WBMQTT::NewMosquittoMqttClient(mqttConfig)))
                .SetId(mqttConfig.Id)
                .SetUseStorage(true)
                .SetReownUnknownDevices(true)
                .SetStoragePath(WBMQTT_DB_FILE),
                config.PublishParameters
            );
        mqttDriver->StartLoop();
        mqttDriver->WaitForReady();
        auto gpioDriver = WBMQTT::MakeUnique<TGpioDriver>(mqttDriver, config);
        Utils::ClearMappingCache();
        gpioDriver->Start();

        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&]{
            gpioDriver.reset();
            mqttDriver->StopLoop();
            mqttDriver->Close();
            mqttDriver.reset();
        });

        initialized.Complete();
        WBMQTT::SignalHandling::Wait();
    } catch (const std::exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
