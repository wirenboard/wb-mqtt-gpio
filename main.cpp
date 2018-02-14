#include "gpio_driver.h"
#include "log.h"
#include "config.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>

#include <cstdio>
#include <cstring>

#include <sstream>
#include <algorithm>
#include <set>
#include <chrono>
#include <thread>


using namespace std;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::steady_clock;


#define LOG(logger) ::logger.Log() << "[main] "


typedef pair<TGpioDesc, std::shared_ptr<TSysfsGpio>> TChannelDesc;

bool FuncComp( const TChannelDesc &a, const TChannelDesc &b)
{
    return (a.first.Order < b.first.Order);
}
class TMQTTGpioHandler : public TMQTTWrapper
{

  public:
    TMQTTGpioHandler(const TMQTTGpioHandler::TConfig &mqtt_config,
                     const THandlerConfig &handler_config);
    ~TMQTTGpioHandler();

    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    void UpdateChannelValues();
    void InitInterrupts(int epfd);// look through all gpios and select Interrupt supporting ones
    string GetChannelTopic(const TGpioDesc &gpio_desc);
    void CatchInterrupts(int count, struct epoll_event *events);
    void PublishValue(const TGpioDesc &gpio_desc, std::shared_ptr<TSysfsGpio> gpio_handler);
    bool FirstTime = true;

  private:
    THandlerConfig Config;
    vector<TChannelDesc> Channels;

    void UpdateValue(const TGpioDesc &gpio_desc, std::shared_ptr<TSysfsGpio> gpio_handler);

};







TMQTTGpioHandler::TMQTTGpioHandler(const TMQTTGpioHandler::TConfig &mqtt_config,
                                   const THandlerConfig &handler_config)
    : TMQTTWrapper(mqtt_config)
    , Config(handler_config)
{
    // init gpios
    for (const TGpioDesc &gpio_desc : handler_config.Gpios) {
        std::shared_ptr<TSysfsGpio> gpio_handler(nullptr);
        if (gpio_desc.Type.empty()) {
            gpio_handler.reset( new TSysfsGpio(gpio_desc.Gpio, gpio_desc.Inverted, gpio_desc.InterruptEdge));
        } else {
            gpio_handler.reset( new TSysfsGpioBaseCounter(gpio_desc.Gpio, gpio_desc.Inverted,
                                gpio_desc.InterruptEdge,  gpio_desc.Type, gpio_desc.Multiplier, gpio_desc.DecimalPlacesTotal,
                                gpio_desc.DecimalPlacesCurrent));
        }

        gpio_handler->Unexport();
        gpio_handler->Export();

        if (gpio_handler->IsExported()) {
            if (gpio_desc.Direction == TGpioDirection::Input) {
                gpio_handler->SetInput();
            } else {
                gpio_handler->SetOutput(gpio_desc.InitialState);
            }

            Channels.emplace_back(gpio_desc, gpio_handler);
        } else {
            Error.Log() << "unable to export gpio " << gpio_desc.Gpio;
        }
    }
    sort(Channels.begin(), Channels.end(), FuncComp);
    Connect();
}

TMQTTGpioHandler::~TMQTTGpioHandler() {}

void TMQTTGpioHandler::OnConnect(int rc)
{
    printf("Connected with code %d.\n", rc);
    if (rc == 0) {
        /* Only attempt to Subscribe on a successful connect. */
        string prefix = string("/devices/") + MQTTConfig.Id + "/";

        // Meta
        Publish(NULL, prefix + "meta/name", Config.DeviceName, 0, true);

        for (const auto &channel_desc : Channels) {
            const auto &gpio_desc = channel_desc.first;

            //~ cout << "GPIO: " << gpio_desc.Name << endl;
            string control_prefix = prefix + "controls/" + gpio_desc.Name;
            std::shared_ptr<TSysfsGpio> gpio_handler = channel_desc.second;
            vector<TPublishPair> what_to_publish (gpio_handler->MetaType());
            int order = gpio_desc.Order * 2;
            for (TPublishPair tmp : what_to_publish) {
                Publish(NULL, control_prefix + "/meta/order", to_string(order), 0, true);
                Publish(NULL, control_prefix + tmp.first + "/meta/type", tmp.second, 0, true);
                order++;
            }
            if (what_to_publish.size() > 1) {
                Subscribe(NULL, control_prefix + "_total");
            }
            if (gpio_desc.Direction == TGpioDirection::Input)
                Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
            else {
                Publish(NULL, control_prefix + "/meta/readonly", "", 0, true);
                Subscribe(NULL, control_prefix + "/on");
            }
        }
        //~ /devices/293723-demo/controls/Demo-Switch 0
        //~ /devices/293723-demo/controls/Demo-Switch/on 1
        //~ /devices/293723-demo/controls/Demo-Switch/meta/type switch



    }
}

void TMQTTGpioHandler::OnMessage(const struct mosquitto_message *message)
{
    string topic = message->topic;
    string payload = static_cast<const char *>(message->payload);


    const vector<string> &tokens = StringSplit(topic, '/');

    if (  (tokens.size() == 5) &&
            (tokens[0] == "") && (tokens[1] == "devices") &&
            (tokens[2] == MQTTConfig.Id) && (tokens[3] == "controls") &&
            (tokens[4].find("_total") == (tokens[4].size() - 6)) ) {
        int pos = tokens[4].find("_total");
        string gpio_name = tokens[4].substr(0, pos);
        for (TChannelDesc &channel_desc : Channels) {
            const auto &gpio_desc = channel_desc.first;
            const auto &gpio_handler = channel_desc.second;
            if (gpio_desc.Name == gpio_name) {
                float total = stof(payload);
                gpio_handler->SetInitialValues(total);
                unsubscribe(NULL, topic.c_str());
            }
        }
    }
    if (  (tokens.size() == 6) &&
            (tokens[0] == "") && (tokens[1] == "devices") &&
            (tokens[2] == MQTTConfig.Id) && (tokens[3] == "controls") &&
            (tokens[5] == "on") ) {
        int val;
        if (payload == "1") {
            val = 1;
        } else if (payload == "0") {
            val = 0;
        } else {
            Warn.Log() << "invalid payload for /on topic: " << payload;
            return;
        }

        for (TChannelDesc &channel_desc : Channels) {
            const auto &gpio_desc = channel_desc.first;
            if (gpio_desc.Direction != TGpioDirection::Output)
                continue;

            if (tokens[4] == gpio_desc.Name) {
                auto &gpio_handler = *channel_desc.second;

                if (gpio_handler.SetValue(val) == 0) {
                    // echo, retained
                    Publish(NULL, GetChannelTopic(gpio_desc), to_string(val), 0, true);
                } else {
                    Debug.Log() << "couldn't set value";
                }
            }
        }
    }
}

void TMQTTGpioHandler::OnSubscribe(int mid, int qos_count, const int *granted_qos)
{
    printf("Subscription succeeded.\n");
}

string TMQTTGpioHandler::GetChannelTopic(const TGpioDesc &gpio_desc)
{
    static string controls_prefix = string("/devices/") + MQTTConfig.Id + "/controls/";
    return (controls_prefix + gpio_desc.Name);
}

void TMQTTGpioHandler::UpdateValue(const TGpioDesc &gpio_desc,
                                   std::shared_ptr<TSysfsGpio> gpio_handler)
{
    // look at previous value and compare it with current
    int cached = gpio_handler->GetCachedValue();
    int value = gpio_handler->GetValue();
    if (value >= 0) {
        // Buggy GPIO driver may yield any non-zero number instead of 1,
        // so make sure it's either 1 or 0 here.
        // See https://github.com/torvalds/linux/commit/25b35da7f4cce82271859f1b6eabd9f3bd41a2bb
        value = !!value;
        if ((cached < 0) || (cached != value)) {
            gpio_handler->SetCachedValue(cached);
            PublishValue(gpio_desc, gpio_handler);
        }
    }
}
void TMQTTGpioHandler::PublishValue(const TGpioDesc &gpio_desc,
                                    std::shared_ptr<TSysfsGpio> gpio_handler)
{
    vector<TPublishPair> what_to_publish(
        gpio_handler->GpioPublish()); //gets nessesary to publish with updating value
    for (TPublishPair &publish_element : what_to_publish) {
        string to_topic = publish_element.first;
        string value = publish_element.second;
        Publish(NULL, GetChannelTopic(gpio_desc) + to_topic, value, 0,
                true); // Publish current value (make retained)
    }
}
void TMQTTGpioHandler::UpdateChannelValues()
{
    for (TChannelDesc &channel_desc : Channels) {
        const auto &gpio_desc = channel_desc.first;
        std::shared_ptr<TSysfsGpio> gpio_handler = channel_desc.second;
        UpdateValue(gpio_desc, gpio_handler);
        if (gpio_desc.Type != "") {
            TPublishPair what_to_publish = gpio_handler->CheckTimeInterval();
            if (what_to_publish.first != "") {
                Publish(NULL, GetChannelTopic(gpio_desc) + what_to_publish.first, what_to_publish.second, 0, true);
            }
        }
    }
}

void TMQTTGpioHandler::InitInterrupts(int epfd)
{
    int n;
    for ( TChannelDesc &channel_desc : Channels) {
        const auto &gpio_desc = channel_desc.first;
        auto &gpio_handler = *channel_desc.second;
        // check if file edge exists and is direction input
        gpio_handler.InterruptUp();
        if (gpio_handler.GetInterruptSupport()) {
            n = epoll_ctl(epfd, EPOLL_CTL_ADD, gpio_handler.GetFileDes(),
                          &gpio_handler.GetEpollStruct()); // adding new instance to epoll
            if (n != 0 ) {
                Error.Log() << "epoll_ctl gained error with GPIO" << gpio_desc.Gpio;
            }
        }
    }

}

void TMQTTGpioHandler::CatchInterrupts(int count, struct epoll_event *events)
{
    int i;
    for ( auto &channel_desc : Channels) {
        const auto &gpio_desc = channel_desc.first;
        std::shared_ptr<TSysfsGpio> gpio_handler = channel_desc.second;
        for (i = 0; i < count; i++) {
            if (gpio_handler->GetFileDes() == events[i].data.fd) {
                if (!gpio_handler->IsDebouncing()) {
                    PublishValue(gpio_desc, gpio_handler);
                }
            }
        }
    }
    //std::this_thread::sleep_for(std::chrono::milliseconds(10));//avoid debouncing

}

const auto libwbmqttDbFile = "/var/lib/wb-homa-gpio/libwbmqtt.db";

int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig {};
    mqttConfig.Id = TGpioDriver::Name;

    int rc;
    string configFileName;
    int epfd;
    struct epoll_event events[20];

    WBMQTT::SignalHandling::Handle({ SIGINT });
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName("main");

    int c, n, debug;
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
                printf ("option T with value '%s'\n", optarg);
                mqttConfig.User = optarg;
                break;

            case 'P':
                printf ("option T with value '%s'\n", optarg);
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
        Error.Log() << "Please specify config file with -c option";
        return 1;
    }

    auto mqttDriver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
        .SetBackend(WBMQTT::NewDriverBackend(WBMQTT::NewMosquittoMqttClient(mqttConfig)))
        .SetId(driverName)
        .SetUseStorage(true)
        .SetReownUnknownDevices(true)
        .SetStoragePath(libwbmqttDbFile)
    );

    PConfig config = nullptr;

    if (!config) {
        try {
            config = make_shared<THandlerConfig>(configFileName);
        } catch (const exception & e) {
            LOG()
        }
    }

    mqttDriver->StartLoop();
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ mqttDriver->StopLoop(); });

    mqttDriver->WaitForReady();

    TGpioDriver driver(mqttDriver, )

    std::shared_ptr<TMQTTGpioHandler> mqtt_handler(new TMQTTGpioHandler(mqtt_config, handler_config));
    mqtt_handler->Init();

    rc = mqtt_handler->loop_start();
    if (rc != 0 ) {
        Error.Log() << "couldn't start mosquitto_loop_start ! " << rc;
    } else {
        epfd = epoll_create(1);// creating epoll for Interrupts
        mqtt_handler->InitInterrupts(epfd);
        steady_clock::time_point start;
        int interval;
        start = steady_clock::now();
        while(1) {
            n = epoll_wait(epfd, events, 20, 500);
            interval = duration_cast<milliseconds>(steady_clock::now() - start).count() ;
            if (interval >= 500 ) {  //checking is it time to look through all gpios
                mqtt_handler->UpdateChannelValues();
                start = steady_clock::now();
            } else {
                if (mqtt_handler->FirstTime && interval == 0) {
                    mqtt_handler->FirstTime = false;
                    continue;
                }
                mqtt_handler->CatchInterrupts( n, events );
            }
        }
    }

    mosqpp::lib_cleanup();

    return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto


// 2420 2032
// 6008 2348 1972
