#include "gpio_chip.h"
#include "gpio_line.h"

#define LOG(logger) ::logger.Log() << "[gpio chip] "

using namespace std;


TGpioChip::TGpioChip(const WBMQTT::PDeviceDriver & mqttDriver, const THandlerConfig & handlerConfig)
    : RequestFd(-1)
{
    Fd = open(handlerConfig.Path.c_str(), O_RDWD | O_CLOEXEC);
    if (Fd < 0) {
        wb_throw(TGpioDriverException, "unable to open device path '" + handlerConfig.Path + "'")
    }

    SCOPE_THROW_EXIT( close(Fd); )

    gpiochip_info info {};
    int retVal = ioctl(Fd, GPIO_GET_CHIPINFO_IOCTL, &info);
    if (retVal < 0) {
        wb_throw(TGpioDriverException, "unable to get GPIO chip info from '" + handlerConfig.Path + "'")
    }

    Lines.resize(info.lines);
    LinesValues.resize(info.lines);

    Name = info.name;
    Label = info.label;

    if (Label.empty()) {
        Label = "unknown";
    }
}

TGpioChip::~TGpioChip()
{
    close(Fd);
}

void TGpioChip::LoadLines()
{
    for (size_t offset = 0; offset < Lines.size(); ++offset) {
        Lines[offset] = make_shared<TGpioLine>(shared_from_this(), offset);
    }
}

void TGpioChip::UpdateLinesValues()
{
    if (RequestFd >= 0) {
        LOG(Warn) << "update lines values on busy chip";
        return;
    }
}

const string & TGpioChip::GetName() const
{
    return Name;
}

const string & TGpioChip::GetLabel() const
{
    return Label;
}

uint32_t TGpioChip::GetLineCount() const
{
    return static_cast<uint32_t>(Lines.size());
}

int TGpioChip::GetFd() const
{
    return Fd;
}
