#include "gpio_driver.h"
#include "config.h"
#include "exceptions.h"
#include "gpio_chip_driver.h"
#include "gpio_counter.h"
#include "gpio_line.h"
#include "interruption_context.h"
#include "log.h"

#include <wblib/wbmqtt.h>

#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;
using namespace WBMQTT;

const char* const TGpioDriver::Name = "wb-gpio";
const auto EPOLL_TIMEOUT_MS = 500;
const auto EPOLL_EVENT_COUNT = 20;

namespace
{
    template<int N> inline bool EndsWith(const string& str, const char (&with)[N])
    {
        return str.rfind(with) == str.size() - (N - 1);
    }

    template<typename F> inline void SuppressExceptions(F&& fn, const char* place)
    {
        try {
            fn();
        } catch (const exception& e) {
            LOG(Warn) << "Exception at " << place << ": " << e.what();
        } catch (...) {
            LOG(Warn) << "Unknown exception in " << place;
        }
    }
} // namespace

TFuture<PControl> CreateOutputControl(WBMQTT::PLocalDevice device,
                                      const WBMQTT::PDriverTx& tx,
                                      PGpioLine line,
                                      const TGpioLineConfig& lineConfig,
                                      std::function<void(uint8_t)> setLineValueFn,
                                      const std::string error = "")
{
    auto futureControl = device->CreateControl(tx,
                                               TControlArgs{}
                                                   .SetId(lineConfig.Name)
                                                   .SetType("switch")
                                                   .SetReadonly(false)
                                                   .SetUserData(line)
                                                   .SetRawValue(lineConfig.InitialState ? "1" : "0")
                                                   .SetError(error)
                                                   .SetDoLoadPrevious(lineConfig.LoadPreviousState));
    setLineValueFn(futureControl.GetValue()->GetValue().As<bool>() ? 1 : 0);
    return futureControl;
}

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver& mqttDriver, const TGpioDriverConfig& config)
    : MqttDriver(mqttDriver),
      Active(false)
{
    try {
        auto tx = MqttDriver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
                                           .SetId(Name)
                                           .SetTitle(config.DeviceName)
                                           .SetIsVirtual(true)
                                           .SetDoLoadPrevious(false))
                          .GetValue();

        if (config.Chips.empty()) {
            wb_throw(TGpioDriverException, "no chips defined in config. Nothing to do");
        }

        for (const auto& chipConfig: config.Chips) {
            if (chipConfig.Lines.empty()) {
                LOG(Warn) << "No lines for chip at '" << chipConfig.Path << "'. Skipping";
                continue;
            }

            try {
                ChipDrivers.push_back(make_shared<TGpioChipDriver>(chipConfig));
            } catch (const TGpioDriverException& e) {
                LOG(Error) << "Failed to create chip driver for " << chipConfig.Path << ": " << e.what();
                continue;
            }

            const auto& chipDriver = ChipDrivers.back();
            const auto& mappedLines = chipDriver->MapLinesByOffset();
            const auto& mappedDisconnectedLines = chipDriver->MapInitiallyDisconnectedLinesByOffset();

            size_t lineNumber = 0;
            for (const auto& lineConfig: chipConfig.Lines) {

                PGpioLine line;
                std::string lineError = "";

                const auto& itOffsetLine = mappedLines.find(lineConfig.Offset);
                if (itOffsetLine != mappedLines.end()) {
                    line = itOffsetLine->second;
                } else {
                    const auto& itDisconnectedLine = mappedDisconnectedLines.find(lineConfig.Offset);
                    if (itDisconnectedLine != mappedDisconnectedLines.end()) {
                        line = itDisconnectedLine->second;
                        lineError = "disconnected";
                    } else
                        continue; // happens if chip driver was unable to initialize line
                }

                auto futureControl = TPromise<PControl>::GetValueFuture(nullptr);

                if (const auto& counter = line->GetCounter()) {
                    for (auto& idType: counter->GetIdsAndTypes(lineConfig.Name)) {
                        auto& id = idType.first;
                        auto& type = idType.second;

                        bool isTotal = EndsWith(id, "_total");

                        futureControl = device->CreateControl(
                            tx,
                            TControlArgs{}
                                .SetId(move(id))
                                .SetType(move(type))
                                .SetReadonly(lineConfig.Direction == EGpioDirection::Input && !isTotal)
                                .SetUserData(line)
                                .SetError(lineError)
                                .SetDoLoadPrevious(isTotal));

                        if (isTotal) {
                            auto initialValue = futureControl.GetValue()->GetValue().As<double>();
                            counter->SetInitialValues(initialValue);

                            LOG(Info) << "Set initial value for " << lineConfig.Name << " counter: " << initialValue;
                        }
                    }
                } else {
                    if (lineConfig.Direction == EGpioDirection::Input) {
                        futureControl = device->CreateControl(tx,
                                                              TControlArgs{}
                                                                  .SetId(lineConfig.Name)
                                                                  .SetType("switch")
                                                                  .SetReadonly(true)
                                                                  .SetUserData(line)
                                                                  .SetError(lineError)
                                                                  .SetRawValue(line->GetValue() == 1 ? "1" : "0"));
                    } else {
                        futureControl = CreateOutputControl(
                            device,
                            tx,
                            line,
                            lineConfig,
                            [&](uint8_t value) { line->SetValue(value); },
                            lineError);
                    }
                }

                ++lineNumber;

                if (lineNumber == chipConfig.Lines.size()) {
                    futureControl.Wait(); // wait for last control
                }
            }
        }

        if (ChipDrivers.empty()) {
            wb_throw(TGpioDriverException, "Failed to create any chip driver. Nothing to do");
        }

    } catch (const exception& e) {
        LOG(Error) << "Unable to create GPIO driver: " << e.what();
        throw;
    }

    EventHandlerHandle = mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent& event) {
        const auto& line = event.Control->GetUserData().As<PGpioLine>();
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
            if (line->GetCounter()) {
                line->GetCounter()->SetInitialValues(value);
                valueForPublishing = line->GetCounter()->GetRoundedTotal();
            }
        }

        auto lineError = line->GetIoctlErrno();
        if (lineError) {
            event.Control->GetDevice()->GetDriver()->AccessAsync([=](const PDriverTx& tx) {
                event.Control->UpdateValueAndError(tx, valueForPublishing, strerror(lineError));
            });
        } else {
            event.Control->GetDevice()->GetDriver()->AccessAsync(
                [=](const PDriverTx& tx) { event.Control->SetRawValue(tx, valueForPublishing); });
        }
    });
}

TGpioDriver::~TGpioDriver()
{
    Stop();
    if (EventHandlerHandle) {
        Clear();
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

    Worker =
        WBMQTT::MakeThread("GPIO worker", {[this] {
                               LOG(Info) << "Started";

                               int epfd = epoll_create(1); // creating epoll for Interrupts
                               struct epoll_event events[EPOLL_EVENT_COUNT]
                               {};

                               WB_SCOPE_EXIT(close(epfd);)

                               for (const auto& chipDriver: ChipDrivers) {
                                   chipDriver->AddToEpoll(epfd);
                               }

                               while (Active) {
                                   bool isHandled = false;
                                   if (int count = epoll_wait(epfd, events, EPOLL_EVENT_COUNT, EPOLL_TIMEOUT_MS)) {
                                       TInterruptionContext ctx{count, events};
                                       for (const auto& chipDriver: ChipDrivers) {
                                           isHandled |= chipDriver->HandleInterrupt(ctx);
                                       }
                                   } else {
                                       for (const auto& chipDriver: ChipDrivers) {
                                           isHandled |= chipDriver->PollLines();
                                       }
                                   }

                                   if (!isHandled) {
                                       continue;
                                   }

                                   auto tx = MqttDriver->BeginTx();
                                   auto device = tx->GetDevice(Name);

                                   for (const auto& chipDriver: ChipDrivers) {
                                       FOR_EACH_LINE(chipDriver, line)
                                       {
                                           const auto err = line->GetIoctlErrno();
                                           if (err) {
                                               device->GetControl(line->GetConfig()->Name)->SetError(tx, strerror(err));
                                           } else {
                                               if (const auto& counter = line->GetCounter()) {
                                                   for (const auto& idValue:
                                                        counter->GetIdsAndValues(line->GetConfig()->Name)) {
                                                       const auto& id = idValue.first;
                                                       const auto value = idValue.second;

                                                       device->GetControl(id)->SetRawValue(tx, value);
                                                   }
                                               } else {
                                                   device->GetControl(line->GetConfig()->Name)
                                                       ->SetValue(tx, static_cast<bool>(line->GetValue()));
                                               }
                                           }
                                       });
                                   }
                               }

                               LOG(Info) << "Stopped";
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

    SuppressExceptions([this] { MqttDriver->RemoveEventHandler(EventHandlerHandle); }, "TGpioDriver::Clear()");

    SuppressExceptions([this] { MqttDriver->BeginTx()->RemoveDeviceById(Name).Sync(); }, "TGpioDriver::Clear()");

    SuppressExceptions([this] { ChipDrivers.clear(); }, "TGpioDriver::Clear()");

    EventHandlerHandle = nullptr;

    LOG(Info) << "Cleaned";
}
