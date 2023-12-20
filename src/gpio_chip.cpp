#include "gpio_chip.h"
#include "exceptions.h"
#include "gpio_counter.h"
#include "gpio_line.h"
#include "log.h"
#include "utils.h"

#include <wblib/utils.h>

#include <cassert>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LOG(logger) ::logger.Log() << "[gpio chip] "

using namespace std;

TGpioChip::TGpioChip(const string& path): Fd(-1), Path(path)
{
    Fd = open(Path.c_str(), O_RDWR | O_CLOEXEC);
    if (Fd < 0) {
        Name = Path;
        Label = "disconnected";
        LineCount = 0;
        LOG(Error) << "Unable to open device path '" << Path << "'";
        return;
    }

    LOG(Debug) << "Open chip at " << Path;

    WB_SCOPE_THROW_EXIT(close(Fd);)

    gpiochip_info info{};
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

TGpioChip::TGpioChip(): Fd(-1), Path("/dev/null")
{
    LineCount = 0;
    Name = "Dummy gpiochip";
    Label = "unknown";
}

TGpioChip::~TGpioChip()
{
    if (Fd > -1) {
        close(Fd);
        LOG(Debug) << "Close chip at " << Path;
    }
}

vector<PGpioLine> TGpioChip::LoadLines(const TLinesConfig& linesConfigs)
{
    vector<PGpioLine> lines;

    lines.reserve(linesConfigs.size());

    for (const auto& lineConfig: linesConfigs) {
        auto line = make_shared<TGpioLine>(shared_from_this(), lineConfig);
        lines.push_back(line);
    }

    return lines;
}

const string& TGpioChip::GetName() const
{
    return Name;
}

const string& TGpioChip::GetLabel() const
{
    return Label;
}

const string& TGpioChip::GetPath() const
{
    return Path;
}

uint32_t TGpioChip::GetLineCount() const
{
    return LineCount;
}

uint32_t TGpioChip::GetNumber() const
{
    return Utils::GpioPathToChipNumber(Path);
}

string TGpioChip::Describe() const
{
    return "GPIO chip @ '" + Path + "' Name: '" + Name + "' Label: '" + Label + "'";
}

int TGpioChip::GetFd() const
{
    return Fd;
}
