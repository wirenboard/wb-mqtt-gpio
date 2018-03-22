#include "gpio_driver.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "exceptions.h"
#include "config.h"
#include "log.h"

#include <wblib/wbmqtt.h>
#include <sys/epoll.h>

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;
using namespace WBMQTT;

const char * const TGpioDriver::Name = "wb-gpio";
const auto EPOLL_TIMEOUT_MS = 500;
const auto EPOLL_EVENT_COUNT = 20;

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const TGpioDriverConfig & config)
{
    try {
        auto tx = mqttDriver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId(Name)
            .SetTitle(config.DeviceName)
        ).GetValue();

        for (const auto & chipPathConfig: config.Chips) {
            const auto & chipConfig = chipPathConfig.second;

            Chips.push_back(make_shared<TGpioChip>(chipConfig.Path));
            const auto & chip = Chips.back();
            chip->LoadLines(chipConfig.Lines);

            size_t lineNumber = 0;
            for (const auto & offsetLineConfig: chipConfig.Lines) {
                const auto & lineConfig = offsetLineConfig.second;

                const auto & futureControl = device->CreateControl(tx, TControlArgs{}
                    .SetId(lineConfig.Name)
                    .SetType(lineConfig.Type)
                    .SetOrder(lineConfig.Order)
                    .SetReadonly(lineConfig.Direction == TGpioDirection::Input)
                    .SetUserData(chip->GetLine(lineConfig.Offset))
                );

                ++lineNumber;

                if (lineNumber == chipConfig.Lines.size()) {
                    futureControl.Wait();   // wait for last control
                }
            }
        }

    } catch (const exception & e) {
        LOG(Error) << "unable to create GPIO driver: " << e.what();
        throw;
    }

    mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent & event){
        uint8_t value;
        if (event.RawValue == "1") {
            value = 1;
        } else if (event.RawValue == "0") {
            value = 0;
        } else {
            LOG(Warn) << "invalid value: " << event.RawValue;
            return;
        }
        event.Control->GetUserData().As<PGpioLine>()->SetValue(value);
    });
}

void TGpioDriver::Start()
{
    Worker = WBMQTT::MakeThread("GPIO worker", {[this]{
        Active.store(true);
        int epfd = epoll_create(1);    // creating epoll for Interrupts
        struct epoll_event events[EPOLL_EVENT_COUNT] {};

        for (const auto & chip: Chips) {
            chip->AddToEpoll(epfd);
        }

        while (Active.load()) {
            if (int count = epoll_wait(epfd, events, EPOLL_EVENT_COUNT, EPOLL_TIMEOUT_MS)) {
            } else {
                
            }
        }
    }});
}

void TGpioDriver::Stop()
{

}

void TGpioDriver::Poll()
{
    for (const auto & chip: Chips) {
        chip->PollLinesValues();
    }

    
}
