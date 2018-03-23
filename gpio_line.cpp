#include "gpio_line.h"
#include "gpio_counter.h"
#include "gpio_chip.h"
#include "exceptions.h"
#include "log.h"

#include <wblib/utils.h>
#include <sys/ioctl.h>

#include <sstream>
#include <cassert>

#define LOG(logger) ::logger.Log() << "[gpio line] "

using namespace std;


TGpioLine::TGpioLine(const PGpioChip & chip, uint32_t offset)
    : Chip(chip)
    , Offset(offset)
    , Fd(-1)
    , Debouncing(false)
{
    assert(Offset < AccessChip()->GetLineCount());

    UpdateInfo();
}

TGpioLine::TGpioLine(const PGpioChip & chip, const TGpioLineConfig & config)
    : TGpioLine(chip, config.Offset)
{
    Config = WBMQTT::MakeUnique<TGpioLineConfig>(config);

    if (!config.Type.empty()) {
        Counter = WBMQTT::MakeUnique<TGpioCounter>(config);
    }
}

void TGpioLine::UpdateInfo()
{
    gpioline_info info {};

    info.line_offset = Offset;

    int retVal = ioctl(AccessChip()->GetFd(), GPIO_GET_LINEINFO_IOCTL, &info);
    if (retVal < 0) {
        wb_throw(TGpioDriverException, "unable to load " + Describe());
    }

    Name     = info.name;
    Flags    = info.flags;
    Consumer = info.consumer;
}

std::string TGpioLine::DescribeShort() const
{
    ostringstream ss;
    ss << "GPIO line " << AccessChip()->GetNumber() << ":" << Offset;
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

const std::string & TGpioLine::GetName() const
{
    return Name;
}

const std::string & TGpioLine::GetConsumer() const
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

bool TGpioLine::IsValueChanged() const
{
    return AccessChip()->IsLineValueChanged(Offset);
}

uint8_t TGpioLine::GetValue() const
{
    return InvertIfNeeded(AccessChip()->GetLineValue(Offset));
}

void TGpioLine::SetValue(uint8_t value)
{
    assert(IsOutput());

    AccessChip()->SetLineValue(Offset, InvertIfNeeded(value));
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
}

int TGpioLine::GetFd() const
{
    assert(Fd > -1);

    return Fd;
}

void TGpioLine::HandleInterrupt(EGpioEdge edge)
{
    const auto now = std::chrono::steady_clock::now();
    const auto isFirstInterruption = PreviousInterruptionTimePoint.time_since_epoch() == chrono::nanoseconds::zero();
    auto intervalUs = isFirstInterruption ? chrono::microseconds::zero()
                                          : chrono::duration_cast<chrono::microseconds>(now - PreviousInterruptionTimePoint);

    /* if interval of impulses is bigger than 1000 microseconds we consider it is not a debounnce */
    Debouncing = isFirstInterruption ? false : intervalUs.count() <= 1000;

    LOG(Debug) << DescribeShort() << " handle interrupt. Edge: " << GpioEdgeToString(edge) << (Debouncing ? " [debouncing]" : "");

    if (Counter) {
        Counter->HandleInterrupt(edge, intervalUs);
    }

    PreviousInterruptionTimePoint = now;
}

const PUGpioCounter & TGpioLine::GetCounter() const
{
    return Counter;
}

const PUGpioLineConfig & TGpioLine::GetConfig() const
{
    assert(Config);

    return Config;
}

bool TGpioLine::IsDebouncing() const
{
    return Debouncing;
}

uint8_t TGpioLine::InvertIfNeeded(uint8_t value) const
{
    return value ^ IsActiveLow();
}
