#include "gpio_line.h"
#include "gpio_chip.h"
#include "log.h"

#include <sstream>
#include <cassert>

#define LOG(logger) ::logger.Log() << "[gpio line] "

using namespace std;


TGpioLine::TGpioLine(const PGpioChipDriver & chip, uint32_t offset)
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

    IsOutput     = info.flags & GPIOLINE_FLAG_IS_OUT;
    IsActiveLow  = info.flags & GPIOLINE_FLAG_ACTIVE_LOW;
    IsUsed       = info.flags & GPIOLINE_FLAG_KERNEL;
    IsOpenDrain  = info.flags & GPIOLINE_FLAG_OPEN_DRAIN;
    IsOpenSource = info.flags & GPIOLINE_FLAG_OPEN_SOURCE;

    Name     = info.name;
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

    ss << " of chip '" << AccessChip()->GetName() << "'";

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

PGpioChipDriver TGpioLine::AccessChip() const
{
    auto chip = Chip.lock();

    assert(chip);

    return chip;
}
