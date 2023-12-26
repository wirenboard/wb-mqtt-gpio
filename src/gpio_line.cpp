#include "gpio_line.h"
#include "exceptions.h"
#include "gpio_chip.h"
#include "gpio_counter.h"
#include "log.h"

#include <sys/ioctl.h>
#include <wblib/utils.h>

#include <cassert>
#include <sstream>
#include <string.h>
#include <unistd.h>

#define LOG(logger) ::logger.Log() << "[gpio line] "

using namespace std;

TGpioLine::TGpioLine(const PGpioChip& chip, const TGpioLineConfig& config)
    : Chip(chip),
      Offset(config.Offset),
      Fd(-1),
      TimerFd(-1),
      Error(0),
      NeedReinit(false),
      Value(0),
      ValueUnfiltered(0),
      InterruptSupport(EInterruptSupport::UNKNOWN)
{
    assert(Offset < AccessChip()->GetLineCount());

    Config = WBMQTT::MakeUnique<TGpioLineConfig>(config);

    if (!config.Type.empty()) {
        Counter = WBMQTT::MakeUnique<TGpioCounter>(config);
    }

    UpdateInfo();
}

TGpioLine::TGpioLine(const TGpioLineConfig& config)
    : Chip(PGpioChip()),
      Offset(config.Offset),
      Fd(-1),
      TimerFd(-1),
      Error(0),
      Value(0),
      NeedReinit(false),
      ValueUnfiltered(0),
      InterruptSupport(EInterruptSupport::UNKNOWN)
{
    Name = "Dummy gpio line";
    Flags = 0;
    Consumer = "null";
    Config = WBMQTT::MakeUnique<TGpioLineConfig>(config);
}

TGpioLine::~TGpioLine()
{
    if (TimerFd > -1) {
        close(TimerFd);
    }
}

void TGpioLine::UpdateInfo()
{
    gpioline_info info{};

    info.line_offset = Offset;

    int retVal = ioctl(AccessChip()->GetFd(), GPIO_GET_LINEINFO_IOCTL, &info);
    if (retVal < 0) {
        LOG(Error) << "Unable to load " << Describe();
        SetError(retVal);
        return;
    }

    Name = info.name;
    Flags = info.flags;
    Consumer = info.consumer;
}

std::string TGpioLine::DescribeShort() const
{
    std::string chipNum;
    try {
        chipNum = to_string(AccessChip()->GetNumber());
    } catch (const TGpioDriverException& e) {
        chipNum = "disconnected";
    }

    ostringstream ss;
    ss << "GPIO line " << chipNum << ":" << Offset;
    if (Config) {
        ss << " (" << Config->Name << ")";
    }
    return ss.str();
}

std::string TGpioLine::Describe() const
{
    ostringstream ss;
    ss << "GPIO line ";

    if (Name.empty()) {
        ss << "(offset " << Offset << ")";
    } else {
        ss << "'" << Name << "'";
    }

    ss << " of " << AccessChip()->Describe();

    return ss.str();
}

std::string TGpioLine::DescribeVerbose() const
{
    ostringstream ss;
    ss << "GPIO line ";

    if (Name.empty()) {
        ss << "(offset " << Offset << ")";
    } else {
        ss << "'" << Name << "'";
    }

    ss << " of " << AccessChip()->Describe() << endl;
    ss << "\tConsumer: '" << Consumer << "'" << endl;
    ss << "\tIsOutput: '" << IsOutput() << "'" << endl;
    ss << "\tIsActiveLow: '" << IsActiveLow() << "'" << endl;
    ss << "\tIsUsed: '" << IsUsed() << "'" << endl;
    ss << "\tIsOpenDrain: '" << IsOpenDrain() << "'" << endl;
    ss << "\tIsOpenSource: '" << IsOpenSource() << "'" << endl;

    return ss.str();
}

const std::string& TGpioLine::GetName() const
{
    return Name;
}

const std::string& TGpioLine::GetConsumer() const
{
    return Consumer;
}

uint32_t TGpioLine::GetOffset() const
{
    return Offset;
}

uint32_t TGpioLine::GetFlags() const
{
    return Flags;
}

bool TGpioLine::IsOutput() const
{
    return Flags & GPIOLINE_FLAG_IS_OUT;
}

bool TGpioLine::DoesNeedReinit() const
{
    return NeedReinit;
}

void TGpioLine::TreatAsOutput()
{
    Flags |= GPIOLINE_FLAG_IS_OUT;
}

bool TGpioLine::IsActiveLow() const
{
    return Flags & GPIOLINE_FLAG_ACTIVE_LOW;
}

bool TGpioLine::IsUsed() const
{
    return Flags & GPIOLINE_FLAG_KERNEL;
}

bool TGpioLine::IsOpenDrain() const
{
    return Flags & GPIOLINE_FLAG_OPEN_DRAIN;
}

bool TGpioLine::IsOpenSource() const
{
    return Flags & GPIOLINE_FLAG_OPEN_SOURCE;
}

uint8_t TGpioLine::GetValue() const
{
    return Value.Get();
}

uint8_t TGpioLine::GetValueUnfiltered() const
{
    return ValueUnfiltered.Get();
}

struct gpiohandle_data TGpioLine::FdGet()
{
    gpiohandle_data data;
    if (ioctl(GetFd(), GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << DescribeShort() << " GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        SetError(errno);
    } else {
        if (GetError() != 0) {
            LOG(Warn) << DescribeShort() << " is connected again. Clearing error '" << strerror(GetError())
                      << "' on it";
            ClearError();
        }
    }
    return data;
}

void TGpioLine::FdSet(uint8_t value)
{
    LOG(Debug) << "Set " << DescribeShort() << " = " << static_cast<int>(value);

    gpiohandle_data data{};

    data.values[0] = value;

    if (ioctl(Fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error)
        "Set " << to_string((int)value) << " to: " << DescribeShort()
               << " GPIOHANDLE_SET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        SetError(errno);
    }
}

void TGpioLine::SetValue(uint8_t value)
{
    assert(IsOutput());
    assert(IsHandled());

    auto error = GetError();
    if (error == 0) {
        FdSet(value);
        SetCachedValue(value);
    } else {
        LOG(Warn) << DescribeShort() << " has error: " << strerror(error) << "; Will not set value "
                  << to_string(value);
    }
}

void TGpioLine::SetCachedValue(uint8_t value)
{
    Value.Set(value);
}

void TGpioLine::SetCachedValueUnfiltered(uint8_t value)
{
    ValueUnfiltered.Set(value);
}

PGpioChip TGpioLine::AccessChip() const
{
    auto chip = Chip.lock();

    assert(chip);

    return chip;
}

bool TGpioLine::IsHandled() const
{
    return Fd > -1;
}

void TGpioLine::SetFd(int fd)
{
    Fd = fd;
    UpdateInfo();
}

void TGpioLine::SetError(int err)
{
    Error = err;
}

int TGpioLine::GetError() const
{
    return Error;
}

void TGpioLine::SetDoesNeedReinit(bool needReinit)
{
    NeedReinit = needReinit;
}

void TGpioLine::ClearError()
{
    if (IsOutput() && (AccessChip()->GetLabel() == "mcp23017")) {
        LOG(Warn) << DescribeShort() << " needs to be re-initialized";
        SetDoesNeedReinit(true);
    }
    SetError(0);
}

int TGpioLine::GetFd() const
{
    assert(Fd > -1);

    return Fd;
}

void TGpioLine::SetTimerFd(int fd)
{
    if (TimerFd > -1) {
        close(TimerFd);
    }

    TimerFd = fd;
}

int TGpioLine::GetTimerFd() const
{
    assert(TimerFd > -1);

    return TimerFd;
}

const TTimePoint& TGpioLine::GetInterruptionTimepoint() const
{
    return PreviousInterruptionTimePoint;
}

EGpioEdge TGpioLine::GetInterruptEdge() const
{
    return Counter ? Counter->GetInterruptEdge() : EGpioEdge::BOTH;
}

std::chrono::microseconds TGpioLine::GetIntervalFromPreviousInterrupt(const TTimePoint& interruptTimePoint) const
{
    return chrono::duration_cast<chrono::microseconds>(interruptTimePoint - GetInterruptionTimepoint());
}

EInterruptStatus TGpioLine::HandleInterrupt(EGpioEdge edge, const TTimePoint& interruptTimePoint)
{
    assert(edge != EGpioEdge::BOTH);

    auto interruptEdge = GetInterruptEdge();
    if (interruptEdge != EGpioEdge::BOTH && interruptEdge != edge) {
        if (Debug.IsEnabled()) {
            LOG(Debug) << DescribeShort() << " handle interrupt. Edge: " << GpioEdgeToString(edge)
                       << " interval: " << GetIntervalFromPreviousInterrupt(interruptTimePoint).count() << " us [skip]";
        }
        return EInterruptStatus::SKIP;
    }

    PreviousInterruptionTimePoint = interruptTimePoint;
    return EInterruptStatus::Handled;
}

void TGpioLine::Update()
{
    if (Counter) {
        Counter->Update(
            chrono::duration_cast<chrono::microseconds>(std::chrono::steady_clock::now() - GetInterruptionTimepoint()));
    }
}

const PUGpioCounter& TGpioLine::GetCounter() const
{
    return Counter;
}

const PUGpioLineConfig& TGpioLine::GetConfig() const
{
    assert(Config);

    return Config;
}

void TGpioLine::SetInterruptSupport(EInterruptSupport interruptSupport)
{
    InterruptSupport = interruptSupport;
}

EInterruptSupport TGpioLine::GetInterruptSupport() const
{
    return InterruptSupport;
}

bool TGpioLine::UpdateIfStable(const TTimePoint& checkTimePoint)
{
    auto fromLastTs = GetIntervalFromPreviousInterrupt(checkTimePoint);
    if (fromLastTs > GetConfig()->DebounceTimeout) {
        SetCachedValue(GetValueUnfiltered());
        LOG(Debug) << "Value (" << static_cast<bool>(GetValueUnfiltered()) << ") on (" << GetName() << " is stable for "
                   << fromLastTs.count() << "us";

        const auto& gpioCounter = GetCounter();
        if (gpioCounter) {
            gpioCounter->HandleInterrupt(GetInterruptEdge(), fromLastTs);
        }

        return true;
    } else {
        return false;
    }
}
