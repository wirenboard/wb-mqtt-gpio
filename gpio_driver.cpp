#include "gpio_driver.h"
#include "gpio_chip_driver.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "interruption_context.h"
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

    template <typename F>
    inline void SuppressExceptions(F && fn, const char * place)
    {
        try {
            fn();
        } catch (const exception & e) {
            LOG(Warn) << "Exception at " << place << ": " << e.what();
        } catch (...) {
            LOG(Warn) << "Unknown exception in " << place;
        }
    }
}

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const TGpioDriverConfig & config)
    : MqttDriver(mqttDriver)
    , Active(false)
{
    try {
        // begin TX for initiating communication with MQttDriver
        auto tx = MqttDriver->BeginTx();
        // create MQTT device (not topic)
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId(Name)
            .SetTitle(config.DeviceName)
            .SetIsVirtual(true) // allow to store values in databse
            .SetDoLoadPrevious(false)
        ).GetValue();

        if (config.Chips.empty()) {
            wb_throw(TGpioDriverException, "no chips defined in config. Nothing to do");
        }

        // filling ChipDriver vector by GPIO chips
        for (const auto & chipConfig: config.Chips) {
            if (chipConfig.Lines.empty()) {
                LOG(Warn) << "No lines for chip at '" << chipConfig.Path << "'. Skipping";
                continue;
            }

            try {
                ChipDrivers.push_back(make_shared<TGpioChipDriver>(chipConfig));
            } catch (const TGpioDriverException & e) {
                LOG(Error) << "Failed to create chip driver for " << chipConfig.Path << ": " << e.what();
                continue;
            }

            // get the shared pointer of the current ChipDriver
            const auto & chipDriver = ChipDrivers.back();
            const auto & mappedLines = chipDriver->MapLinesByOffset();

            size_t lineNumber = 0;
            for (const auto & lineConfig: chipConfig.Lines) {
                const auto & itOffsetLine = mappedLines.find(lineConfig.Offset);
                if (itOffsetLine == mappedLines.end())
                    continue;   // happens if chip driver was unable to initialize line

                const auto & line = itOffsetLine->second;
                auto futureControl = TPromise<PControl>::GetValueFuture(nullptr);

                if (const auto & counter = line->GetCounter()) { // if line has counter

                    // If line is a counter, the total value can stored and restored by the databse. 
                    // We call  "GetIdsAndTypes" to get the vector of "id, type" pairs. In the vector
                    // there are data pairs of channels "_current" and "_total"
                    for (auto & idType: counter->GetIdsAndTypes(lineConfig.Name)) {
                        auto & id   = idType.first;
                        auto & type = idType.second;

                        bool isTotal = EndsWith(id, "_total");

                        // creating control topic for line                        
                        futureControl = device->CreateControl(tx, TControlArgs{}
                            .SetId(move(id))
                            .SetType(move(type))
                            .SetOrder(lineConfig.Order)
                            .SetReadonly(lineConfig.Direction == EGpioDirection::Input)
                            .SetUserData(line)
                            .SetDoLoadPrevious(isTotal) // restoring from database
                        );

                        if (isTotal) {
                            // storing restored "total" counter value to local object
                            auto initialValue = futureControl.GetValue()->GetValue().As<double>();
                            counter->SetInitialValues(initialValue);

                            LOG(Info) << "Set initial value for " << lineConfig.Name << " counter: " << initialValue;
                        }
                    }
                } else {    /// line has no counter
                    futureControl = device->CreateControl(tx, TControlArgs{}
                        .SetId(lineConfig.Name)
                        .SetType("switch")
                        .SetOrder(lineConfig.Order)
                        .SetReadonly(lineConfig.Direction == EGpioDirection::Input)
                        .SetUserData(line)
                    );
                }

                ++lineNumber;

                // sync
                if (lineNumber == chipConfig.Lines.size()) {
                    futureControl.Wait();   // wait for last control
                }
            }
        }

        if (ChipDrivers.empty()) {
            wb_throw(TGpioDriverException, "Failed to create any chip driver. Nothing to do");
        }

    } catch (const exception & e) {
        LOG(Error) << "Unable to create GPIO driver: " << e.what();
        throw;
    }

    // Output changes are handeled by an event handler
    EventHandlerHandle = mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent & event){
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

TGpioDriver::~TGpioDriver()
{
    if (EventHandlerHandle) {
        Clear();
    }
}

void TGpioDriver::Start()
{
    if (Active.load()) {
        wb_throw(TGpioDriverException, "attempt to start already started driver");
    }

    Active.store(true);

    Worker = WBMQTT::MakeThread("GPIO worker", {[this]{
        LOG(Info) << "Started";

        int epfd = epoll_create(1);    // creating epoll for Interrupts
        // EPOLL_EVENT_COUNT number of epoll event can be handled
        struct epoll_event events[EPOLL_EVENT_COUNT] {};

        WB_SCOPE_EXIT( close(epfd); )

        for (const auto & chipDriver: ChipDrivers) {
            // add each line descriptor of the chip to epoll
            chipDriver->AddToEpoll(epfd);
        }

        // main loop of operation
        while (Active.load()) {
            bool isHandled = false;
            // If there is any waiting file descriptor for request -> read file descriptor's line
            if (int count = epoll_wait(epfd, events, EPOLL_EVENT_COUNT, EPOLL_TIMEOUT_MS)) {
                TInterruptionContext ctx {count, events};
                for (const auto & chipDriver: ChipDrivers) {
                    isHandled |= chipDriver->HandleInterrupt(ctx);
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
                            for (const auto & idValue: counter->GetIdsAndValues(line->GetConfig()->Name)) {
                                const auto & id  = idValue.first;
                                const auto value = idValue.second;

                                device->GetControl(id)->SetRawValue(tx, value);
                            }

                            counter->ResetIsChanged();
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
    if (!Active.load()) {
        wb_throw(TGpioDriverException, "attempt to stop not started driver");
    }

    Active.store(false);
    LOG(Info) << "Stopping...";

    if (Worker->joinable()) {
        Worker->join();
    }

    Worker.reset();
}

void TGpioDriver::Clear() noexcept
{
    if (Active.load()) {
        LOG(Error) << "Unable to clear driver while it's running";
        return;
    }

    LOG(Info) << "Cleaning...";

    SuppressExceptions([this]{
        MqttDriver->RemoveEventHandler(EventHandlerHandle);
    }, "TGpioDriver::Clear()");

    SuppressExceptions([this]{
        MqttDriver->BeginTx()->RemoveDeviceById(Name).Sync();
    }, "TGpioDriver::Clear()");

    SuppressExceptions([this]{
        ChipDrivers.clear();
    }, "TGpioDriver::Clear()");

    EventHandlerHandle = nullptr;

    LOG(Info) << "Cleaned";
}
