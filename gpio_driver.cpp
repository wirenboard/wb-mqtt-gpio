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
const auto POLL_LINES_PERIOD_MS = 500;
const auto DEBOUNCE_POLL_PERIOD_MS = 10;
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

int TGpioDriver::CreateIntervalTimer(uint16_t intervalMs)
{
    struct itimerspec ts; // Periodic timer
    ts.it_value.tv_sec = intervalMs / 1000;
	ts.it_value.tv_nsec = (intervalMs % 1000) * 1000000;
	ts.it_interval.tv_sec = intervalMs / 1000;
	ts.it_interval.tv_nsec = (intervalMs % 1000) * 1000000;

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

    LOG(Info) << "Created interval timer (fd: " << tfd << "; period: " << intervalMs << "ms)";
    return tfd;
}

void TGpioDriver::AddToEpoll(int epollFd, int timerFd)
{
    struct epoll_event ep_event {};
    ep_event.events = EPOLLIN;
    ep_event.data.fd = timerFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, timerFd, &ep_event) < 0) {
        LOG(Error) << "epoll_ctl error: '" << strerror(errno);
        wb_throw(TGpioDriverException, "unable to add timer to epoll: epoll_ctl failed with " + string(strerror(errno)));
    }
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

    Worker = WBMQTT::MakeThread("GPIO worker", {[this]{
        LOG(Info) << "Main worker thread started";

        int epInterruptsFd = epoll_create(1), epTimersFd = epoll_create(1);
        struct epoll_event events[EPOLL_EVENT_COUNT] {}, timers[EPOLL_EVENT_COUNT] {};

        int timerPollLinesFd = CreateIntervalTimer(POLL_LINES_PERIOD_MS);
        int timerDebounceFd = CreateIntervalTimer(DEBOUNCE_POLL_PERIOD_MS);
        uint64_t timerfdRes;

        WB_SCOPE_EXIT( close(epInterruptsFd); close(epTimersFd); close(timerPollLinesFd); close(timerDebounceFd); )

        for (const auto & chipDriver: ChipDrivers) {
            chipDriver->AddToEpoll(epInterruptsFd);
        }
        AddToEpoll(epTimersFd, timerPollLinesFd);
        AddToEpoll(epTimersFd, timerDebounceFd);

        while (Active) {
            TTimePoint realInterruptTs;
            bool isHandled = false;

            // real interrupt: fill values to gpio lines (unfiltered)
            if (int count = epoll_wait(epInterruptsFd, events, EPOLL_EVENT_COUNT, 0)) {
                realInterruptTs = chrono::steady_clock::now();
                TInterruptionContext ctx {count, events};
                for (const auto & chipDriver: ChipDrivers) {
                    chipDriver->HandleInterrupt(ctx);
                }
            }

            if (epoll_wait(epTimersFd, timers, EPOLL_EVENT_COUNT, 10)) {
                auto now = chrono::steady_clock::now();
                for (const auto & timerEvent: timers) {
                    read(timerEvent.data.fd, &timerfdRes, sizeof(timerfdRes));

                    // gpio value is stable if remains some time
                    if (timerEvent.data.fd == timerDebounceFd) {
                        for (const auto & chipDriver: ChipDrivers) {
                            auto it = chipDriver->LinesRecentlyFired.begin();
                            while(it != chipDriver->LinesRecentlyFired.end()) {
                                auto line = *it;
                                if (line->UpdateIfStable(now)) {
                                    chipDriver->LinesRecentlyFired.erase(it++);
                                    isHandled = true;
                                } else {
                                    ++it;
                                }
                            }
                        };
                        if (isHandled) {  // faster real-interrupt handling
                            break;
                        }

                    // poll lines, if no real interrupts
                    } else if (timerEvent.data.fd == timerPollLinesFd) {
                        if (chrono::duration_cast<chrono::milliseconds>(now - realInterruptTs) > std::chrono::milliseconds(POLL_LINES_PERIOD_MS)) {
                            for (const auto & chipDriver: ChipDrivers) {
                                isHandled |= chipDriver->PollLines();
                            }
                        }

                    } else {
                        LOG(Debug) << "Unknown fd in epoll timers: " << timerEvent.data.fd;
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
