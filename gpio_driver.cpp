#include "gpio_driver.h"
#include "gpio_chip_driver.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "exceptions.h"
#include "config.h"
#include "log.h"

#include <wblib/wbmqtt.h>

#include <cassert>
#include <unistd.h>
#include <sys/epoll.h>

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;
using namespace WBMQTT;

const char * const TGpioDriver::Name = "wb-gpio";
const auto EPOLL_TIMEOUT_MS = 500;
const auto EPOLL_EVENT_COUNT = 20;

namespace
{
    template <int N>
    inline bool EndsWith(const string & str, const char(& with)[N])
    {
        return str.rfind(with) == str.size() - (N - 1);
    }
}

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const TGpioDriverConfig & config)
    : MqttDriver(mqttDriver)
{
    try {
        auto tx = MqttDriver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId(Name)
            .SetTitle(config.DeviceName)
            .SetIsVirtual(true)
            .SetDoLoadPrevious(false)
        ).GetValue();

        for (const auto & chipPathConfig: config.Chips) {
            const auto & chipConfig = chipPathConfig.second;

            ChipDrivers.push_back(make_shared<TGpioChipDriver>(chipConfig));
            const auto & chipDriver = ChipDrivers.back();
            const auto & mappedLines = chipDriver->MapLinesByOffset();

            size_t lineNumber = 0;
            int order = 0;
            for (const auto & offsetLineConfig: chipConfig.Lines) {
                const auto & lineConfig = offsetLineConfig.second;
                const auto & itOffsetLine = mappedLines.find(lineConfig.Offset);
                if (itOffsetLine == mappedLines.end())
                    continue;   // happens if chip driver was unable to initialize line

                const auto & line = itOffsetLine->second;
                auto futureControl = TPromise<PControl>::GetValueFuture(nullptr);
                order = max(lineConfig.Order, order);

                if (const auto & counter = line->GetCounter()) {
                    for (auto & idType: counter->GetIdsAndTypes(lineConfig.Name)) {
                        auto & id   = idType.first;
                        auto & type = idType.second;

                        bool isTotal = EndsWith(id, "_total");

                        futureControl = device->CreateControl(tx, TControlArgs{}
                            .SetId(move(id))
                            .SetType(move(type))
                            .SetOrder(order++)
                            .SetReadonly(lineConfig.Direction == EGpioDirection::Input)
                            .SetUserData(line)
                            .SetDoLoadPrevious(isTotal)
                        );

                        if (isTotal) {
                            counter->SetInitialValues(futureControl.GetValue()->GetValue().As<double>());

                            LOG(Debug) << "Set initial value for " << lineConfig.Name << " counter: " << counter->GetTotal();
                        }
                    }
                } else {
                    futureControl = device->CreateControl(tx, TControlArgs{}
                        .SetId(lineConfig.Name)
                        .SetType("switch")
                        .SetOrder(lineConfig.Order)
                        .SetReadonly(lineConfig.Direction == EGpioDirection::Input)
                        .SetUserData(line)
                    );
                }

                ++lineNumber;

                if (lineNumber == chipConfig.Lines.size()) {
                    futureControl.Wait();   // wait for last control
                }
            }
        }

    } catch (const exception & e) {
        LOG(Error) << "Unable to create GPIO driver: " << e.what();
        throw;
    }

    mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent & event){
        uint8_t value;
        if (event.RawValue == "1") {
            value = 1;
        } else if (event.RawValue == "0") {
            value = 0;
        } else {
            LOG(Warn) << "Invalid value: " << event.RawValue;
            return;
        }
        const auto & line = event.Control->GetUserData().As<PGpioLine>();
        if (line->IsOutput()) {
            line->SetValue(value);
        } else {
            LOG(Warn) << "Attempt to write value to input " << line->DescribeShort();
        }
    });
}

void TGpioDriver::Start()
{
    if (Worker) {
        wb_throw(TGpioDriverException, "attempt to start already started driver");
    }

    Worker = WBMQTT::MakeThread("GPIO worker", {[this]{
        LOG(Info) << "Started";

        Active.store(true);
        int epfd = epoll_create(1);    // creating epoll for Interrupts
        struct epoll_event events[EPOLL_EVENT_COUNT] {};

        WB_SCOPE_EXIT( close(epfd); )

        for (const auto & chipDriver: ChipDrivers) {
            chipDriver->AddToEpoll(epfd);
        }

        while (Active.load()) {
            bool isHandled = false;
            if (int count = epoll_wait(epfd, events, EPOLL_EVENT_COUNT, EPOLL_TIMEOUT_MS)) {
                for (const auto & chipDriver: ChipDrivers) {
                    isHandled |= chipDriver->HandleInterrupt(count, events);
                }
            } else {
                for (const auto & chipDriver: ChipDrivers) {
                    isHandled |= chipDriver->PollLines();
                }
            }

            if (!isHandled) {
                continue;
            }

            auto tx     = MqttDriver->BeginTx();
            auto device = tx->GetDevice(Name);

            for (const auto & chipDriver: ChipDrivers) {
                FOR_EACH_LINE(chipDriver, line) {
                    if (const auto & counter = line->GetCounter()) {
                        if (counter->IsChanged()) {
                            counter->ResetIsChanged();

                            for (const auto & idValue: counter->GetIdsAndValues(line->GetConfig()->Name)) {
                                const auto & id  = idValue.first;
                                const auto value = idValue.second;

                                device->GetControl(id)->SetRawValue(tx, value);
                            }
                        }
                    } else if (line->IsValueChanged()) {
                        device->GetControl(line->GetConfig()->Name)->SetValue(tx, static_cast<bool>(line->GetValue()));
                    }

                    line->ResetIsChanged();
                });
            }
        }

        LOG(Info) << "Stopped";
    }});
}

void TGpioDriver::Stop()
{
    if (!Worker) {
        wb_throw(TGpioDriverException, "attempt to stop not started driver");
    }

    Active.store(false);
    LOG(Info) << "Stopping...";

    if (Worker->joinable()) {
        Worker->join();
    }

    Worker.reset();
}
