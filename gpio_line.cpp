#include "gpio_line.h"
#include "gpio_chip.h"
#include "log.h"

#include <sstream>
#include <cassert>

#define LOG(logger) ::logger.Log() << "[gpio line] "

using namespace std;


TGpioLine::TGpioLine(const PGpioChip & chip, uint32_t offset)
    : Chip(chip)
    , Offset(offset)
{
    assert(offset < AccessChip()->GetLineCount());

    gpioline_info info {};

    info.line_offset = Offset;

    int retVal = ioctl(AccessChip()->GetFd(), GPIO_GET_LINEINFO_IOCTL, &info);
    if (retVal < 0) {
        wb_throw(TGpioDriverException, "unable to load " + Describe())
    }

    Name     = info.name;
    Flags    = info.flags;
    Consumer = info.consumer;
}

std::string TGpioLine::Describe() const
{
    ostringstream ss;
    ss << "GPIO line ";

    if (Name.empty()) {
        ss << "(offset " << Offset << ")"
    } else {
        ss << "'" << Name << "'"
    }

    ss << " of " << AccessChip()->Describe();

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
    return AccessChip()->GetLineValue(Offset);
}

PGpioChip TGpioLine::AccessChip() const
{
    auto chip = Chip.lock();

    assert(chip);

    return chip;
}
