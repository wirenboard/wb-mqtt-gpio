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


TGpioLine::TGpioLine(const PGpioChip & chip, const TGpioLineConfig & config)
    : Chip(chip)
    , Offset(config.Offset)
{
    assert(Offset < AccessChip()->GetLineCount());

    if (!config.Type.empty()) {
        Counter = WBMQTT::MakeUnique<TGpioCounter>(config);
    }

    UpdateInfo();
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

uint8_t TGpioLine::GetValue() const
{
    assert(!IsOutput());

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
    return IsHandledByDriver;
}

void TGpioLine::SetIsHandled(bool isHandled)
{
    IsHandledByDriver = isHandled;
}

void TGpioLine::HandleInterrupt(EGpioEdge edge)
{
    if (Counter) {
        Counter->HandleInterrupt(edge);
    }
}

uint8_t TGpioLine::InvertIfNeeded(uint8_t value) const
{
    return value ^ IsActiveLow();
}
