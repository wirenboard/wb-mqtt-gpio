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
#include <sys/timerfd.h>

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;
using namespace WBMQTT;

const char * const TGpioDriver::Name = "wb-gpio";
const auto NONINTERRUPT_POLL_TIMEOUT_MS = 500;
const auto EPOLL_EVENT_COUNT = 20;
const auto DEBOUNCE_POLL_PERIOD_MS = 10;

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
        auto tx = MqttDriver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId(Name)
            .SetTitle(config.DeviceName)
            .SetIsVirtual(true)
            .SetDoLoadPrevious(false)
        ).GetValue();

        if (config.Chips.empty()) {
            wb_throw(TGpioDriverException, "no chips defined in config. Nothing to do");
        }

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

            const auto & chipDriver = ChipDrivers.back();
            const auto & mappedLines = chipDriver->MapLinesByOffset();

            size_t lineNumber = 0;
            for (const auto & lineConfig: chipConfig.Lines) {
                const auto & itOffsetLine = mappedLines.find(lineConfig.Offset);
                if (itOffsetLine == mappedLines.end())
                    continue;   // happens if chip driver was unable to initialize line

                const auto & line = itOffsetLine->second;
                auto futureControl = TPromise<PControl>::GetValueFuture(nullptr);

                if (const auto & counter = line->GetCounter()) {
                    for (auto & idType: counter->GetIdsAndTypes(lineConfig.Name)) {
                        auto & id   = idType.first;
                        auto & type = idType.second;

                        bool isTotal = EndsWith(id, "_total");

                        futureControl = device->CreateControl(tx, TControlArgs{}
                            .SetId(move(id))
                            .SetType(move(type))
                            .SetReadonly(lineConfig.Direction == EGpioDirection::Input && !isTotal)
                            .SetUserData(line)
                            .SetDoLoadPrevious(isTotal)
                        );

                        if (isTotal) {
                            auto initialValue = futureControl.GetValue()->GetValue().As<double>();
                            counter->SetInitialValues(initialValue);

                            LOG(Info) << "Set initial value for " << lineConfig.Name << " counter: " << initialValue;
                        }
                    }
                } else {
                    if (lineConfig.Direction == EGpioDirection::Input) {
                        futureControl = device->CreateControl(tx, TControlArgs{}
                            .SetId(lineConfig.Name)
                            .SetType("switch")
                            .SetReadonly(true)
                            .SetUserData(line)
                            .SetRawValue(line->GetValue() == 1 ? "1" : "0")
                        );
                    } else {
                        futureControl = device->CreateControl(tx, TControlArgs{}
                            .SetId(lineConfig.Name)
                            .SetType("switch")
                            .SetReadonly(false)
                            .SetUserData(line)
                            .SetRawValue(lineConfig.InitialState ? "1" : "0")
                            .SetDoLoadPrevious(true)
                        );
                        line->SetValue(futureControl.GetValue()->GetValue().As<bool>() ? 1 : 0);
                    }
                }

                ++lineNumber;

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

    EventHandlerHandle = mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent & event){
        const auto & line = event.Control->GetUserData().As<PGpioLine>();
        std::string valueForPublishing;
        if (line->IsOutput()) {
            uint8_t value;
            if (event.RawValue == "1") {
                value = 1;
            } else if (event.RawValue == "0") {
                value = 0;
            } else {
                LOG(Warn) << "Invalid value: " << event.RawValue;
                return;
            }
            line->SetValue(value);
            valueForPublishing = event.RawValue;
        } else {
            char* end;
            float value = strtof(event.RawValue.c_str(), &end);
            if (end == event.RawValue.c_str()) {
                LOG(Warn) << "Invalid value: " << event.RawValue;
                return;
            }
            line->GetCounter()->SetInitialValues(value);
            valueForPublishing = line->GetCounter()->GetRoundedTotal();
        }
        event.Control->GetDevice()->GetDriver()->AccessAsync([=](const PDriverTx & tx){
            event.Control->SetRawValue(tx, valueForPublishing);
        });
    });
}

TGpioDriver::~TGpioDriver()
{
    Stop();
    if (EventHandlerHandle) {
        Clear();
    }
}

int TGpioDriver::CreateIntervalTimer(uint16_t intervalMsec)
{
    struct itimerspec ts; // Periodic timer
    ts.it_value.tv_sec = 0;
	ts.it_value.tv_nsec = 0;
	ts.it_interval.tv_sec = intervalMsec / 1000;
	ts.it_interval.tv_nsec = (intervalMsec % 1000) * 1000000;

    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (tfd == -1) {
        LOG(Error) << "timerfd_create failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to create timer: timerfd_create failed with " + string(strerror(errno)));
	}

    if (timerfd_settime(tfd, 0, &ts, NULL) < 0) {
        LOG(Error) << "timerfd_settime failed: " << strerror(errno);
		close(tfd);
        wb_throw(TGpioDriverException, "unable to setup timer: timerfd_settime failed with " + string(strerror(errno)));
	}

    LOG(Debug) << "Created interval timer (fd: " << tfd << "; period: " << intervalMsec << "ms";
    return tfd;
}

void TGpioDriver::Start()
{
    {
        std::lock_guard<std::mutex> lg(ActiveMutex);
        if (Active) {
            wb_throw(TGpioDriverException, "attempt to start already started driver");
        }
        Active = true;
    }

    // DebouncePoll = WBMQTT::MakeThread("Debounce poll thread", {[this]{
    //     LOG(Info) << "Debounce poll thread started (period: " << DEBOUNCE_POLL_PERIOD_MS << "ms)";
    //     while (Active) {
    //         auto now = chrono::steady_clock::now();
    //         for (const auto & chipDriver: ChipDrivers) {
    //             for (const auto & line: chipDriver->LinesRecentlyFired) {
    //                 if (line->GetIntervalFromPreviousInterrupt(now) > line->GetConfig()->DebounceTimeout) {
    //                     line->SetCachedValue(line->GetValueUnfiltered());
    //                     chipDriver->LinesRecentlyFired.erase(line);
    //                 }
    //             }
    //         };

    //         std::this_thread::sleep_for(std::chrono::milliseconds(DEBOUNCE_POLL_PERIOD_MS));
    //     };
    //     LOG(Info) << "Debounce poll thread stopped";
    // }});

    Worker = WBMQTT::MakeThread("GPIO worker", {[this]{
        LOG(Info) << "Main worker thread started";

        int epfd = epoll_create(1);    // creating epoll for Interrupts
        int epTimers = epoll_create(1);    // creating epoll for Timers
        struct epoll_event events[EPOLL_EVENT_COUNT] {}, timers[EPOLL_EVENT_COUNT] {};

        int timerNoInterruptFd = CreateIntervalTimer(500);
        int timerDebounceFd = CreateIntervalTimer(10);

        WB_SCOPE_EXIT( close(epfd); close(epTimers); close(timerNoInterruptFd); close(timerDebounceFd); )

        for (const auto & chipDriver: ChipDrivers) {
            chipDriver->AddToEpoll(epfd);
        }

        struct epoll_event ep_event {};
        ep_event.events = EPOLLIN;
        ep_event.data.fd = timerNoInterruptFd;
        if (epoll_ctl(epTimers, EPOLL_CTL_ADD, timerNoInterruptFd, &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno);
        }
        ep_event.data.fd = timerDebounceFd;
        if (epoll_ctl(epTimers, EPOLL_CTL_ADD, timerDebounceFd, &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno);
        }

        while (Active) {
            TTimePoint realInterruptTs;

            bool isHandled = false;

            if (int count = epoll_wait(epfd, events, EPOLL_EVENT_COUNT, 0)) {
                realInterruptTs = chrono::steady_clock::now();
                // LOG(Debug) << "Real interrupt at ts: " << realInterruptTs;
                TInterruptionContext ctx {count, events};
                for (const auto & chipDriver: ChipDrivers) {
                    isHandled |= chipDriver->HandleInterrupt(ctx);
                }
            }
            // } else {
            //     for (const auto & chipDriver: ChipDrivers) {
            //         isHandled |= chipDriver->PollLines();
            //     }
            // }

            if (epoll_wait(epTimers, timers, EPOLL_EVENT_COUNT, 20)) {
                auto now = chrono::steady_clock::now();
                for (const auto & timerEvent: timers) {
                    if (timerEvent.data.fd == timerDebounceFd) {
                        LOG(Debug) << "Debounce";
                        for (const auto & chipDriver: ChipDrivers) {
                            for (const auto & line: chipDriver->LinesRecentlyFired) {
                                if (line->GetIntervalFromPreviousInterrupt(now) > line->GetConfig()->DebounceTimeout) {
                                    line->SetCachedValue(line->GetValueUnfiltered());
                                    chipDriver->LinesRecentlyFired.erase(line);
                                }
                            }
                        }
                    } else if (timerEvent.data.fd == timerNoInterruptFd) {
                        if (chrono::duration_cast<chrono::milliseconds>(now - realInterruptTs) > std::chrono::milliseconds(500)) {
                            LOG(Debug) << "Fake interrupt";
                            for (const auto & chipDriver: ChipDrivers) {
                                isHandled |= chipDriver->PollLines();
                            }
                        }
                    } else {
                        LOG(Error) << "Unknown fd in epoll timers: " << timerEvent.data.fd;
                    }
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
                        for (const auto & idValue: counter->GetIdsAndValues(line->GetConfig()->Name)) {
                            const auto & id  = idValue.first;
                            const auto value = idValue.second;

                            device->GetControl(id)->SetRawValue(tx, value);
                        }
                    } else {
                        device->GetControl(line->GetConfig()->Name)->SetValue(tx, static_cast<bool>(line->GetValue()));
                    }
                });
            }
        }

        LOG(Info) << "Main worker thread stopped";
    }});
}

void TGpioDriver::Stop()
{
    {
        std::lock_guard<std::mutex> lg(ActiveMutex);
        if (!Active) {
            LOG(Warn) << "attempt to stop not started driver";
            return;
        }
        Active = false;
    }

    LOG(Info) << "Stopping...";

    if (Worker->joinable()) {
        Worker->join();
    }

    Worker.reset();

    // if (DebouncePoll->joinable()) {
    //     DebouncePoll->join();
    // }

    // DebouncePoll.reset();

}

void TGpioDriver::Clear() noexcept
{
    if (Active) {
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
