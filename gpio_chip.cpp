#include "gpio_chip.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "exceptions.h"
#include "utils.h"
#include "log.h"

#include <wblib/utils.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>

#define LOG(logger) ::logger.Log() << "[gpio chip] "

using namespace std;


TGpioChip::TGpioChip(const string & path)
    : Fd(-1)
    , Path(path)
    , InterruptSupport(EInterruptSupport::UNKNOWN)
{
    Fd = open(Path.c_str(), O_RDWR | O_CLOEXEC);
    if (Fd < 0) {
        wb_throw(TGpioDriverException, "unable to open device path '" + Path + "'");
    }

    LOG(Debug) << "Open chip at " << Path;

    WB_SCOPE_THROW_EXIT( close(Fd); )

    gpiochip_info info {};
    int retVal = ioctl(Fd, GPIO_GET_CHIPINFO_IOCTL, &info);
    if (retVal < 0) {
        wb_throw(TGpioDriverException, "unable to get GPIO chip info from '" + Path + "'");
    }

    LineCount = info.lines;

    Name = info.name;
    Label = info.label;

    if (Label.empty()) {
        Label = "unknown";
    }
}

TGpioChip::~TGpioChip()
{
    close(Fd);
    LOG(Debug) << "Close chip at " << Path;
}

vector<PGpioLine> TGpioChip::LoadLines(const TLinesConfig & linesConfigs)
{
    vector<PGpioLine> lines;

    lines.reserve(linesConfigs.size());

    for (const auto & lineConfig: linesConfigs) {
        auto line = make_shared<TGpioLine>(shared_from_this(), lineConfig);
        lines.push_back(line);
    }

    return lines;
}

const string & TGpioChip::GetName() const
{
    return Name;
}

const string & TGpioChip::GetLabel() const
{
    return Label;
}

const string & TGpioChip::GetPath() const
{
    return Path;
}

uint32_t TGpioChip::GetLineCount() const
{
    return LineCount;
}

uint32_t TGpioChip::GetNumber() const
{
    return GpioPathToChipNumber(Path);
}

void TGpioChip::SetInterruptSupport(EInterruptSupport interruptSupport)
{
    InterruptSupport = interruptSupport;
}

EInterruptSupport TGpioChip::GetInterruptSupport() const
{
    return InterruptSupport;
}

string TGpioChip::Describe() const
{
    return "GPIO chip @ '" + Path + "' Name: '" + Name + "' Label: '" + Label + "'";
}

int TGpioChip::GetFd() const
{
    return Fd;
}
