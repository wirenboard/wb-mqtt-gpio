#include "gpio_chip.h"
#include "gpio_line.h"

#define LOG(logger) ::logger.Log() << "[gpio chip] "

using namespace std;

const auto CONSUMER = "wb-homa-gpio";


TGpioChip::TGpioChip(const std::string & path)
    : Fd(-1)
    , Path(path)
    , SupportsInterrupts(-1)
{
    Fd = open(Path.c_str(), O_RDWD | O_CLOEXEC);
    if (Fd < 0) {
        wb_throw(TGpioDriverException, "unable to open device path '" + Path + "'")
    }

    SCOPE_THROW_EXIT( close(Fd); )

    gpiochip_info info {};
    int retVal = ioctl(Fd, GPIO_GET_CHIPINFO_IOCTL, &info);
    if (retVal < 0) {
        wb_throw(TGpioDriverException, "unable to get GPIO chip info from '" + Path + "'")
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

void TGpioChip::LoadLines(const TLinesConfig & linesConfigs)
{
    for (size_t offset = 0; offset < Lines.size(); ++offset) {
        auto line = make_shared<TGpioLine>(shared_from_this(), offset);
        Lines[offset] = line;

        if (linesConfigs.count(offset) == 0) {
            LOG(Info) << line->Describe() << " is not in config. Skipping."
            continue;
        }

        const auto & config = linesConfigs.at(offset);
        if (SupportsInterrupts != 0) {
            if (!TryListenLine(line, config)) {
                LOG(Info) << Describe() << " does not support events. Polling will be used instead."
                AddToPolling(line, config)
                assert(SupportsInterrupts != 1);   // fail after success is not expected: chip either supports events or not.
                SupportsInterrupts = 0;
            } else {
                SupportsInterrupts = 1;
            }
        }
    }

    InitPolling();
}

void TGpioChip::PollLinesValues()
{
    for (auto & flagsLines: PollingLines) {
        gpiohandle_data data {};
        if (ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
            LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
            wb_throw(TGpioDriverException, "unable to poll lines values")
        }

        for (uint32_t i = 0; i < req.lines; ++i) {
            const auto & line = flagsLines.second.Lines[i];
            auto value = data.values[i];
            LinesValues[line->GetOffset()] = value;
        }
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

const string & TGpioChip::GetPath() const
{
    return Path;
}

uint32_t TGpioChip::GetLineCount() const
{
    return static_cast<uint32_t>(Lines.size());
}

uint32_t TGpioChip::GetNumber() const
{
    return stoul(Path.substr(13));
}

string TGpioChip::Describe() const
{
    return "GPIO chip @ '" + Path + "' Name: '" + Name + "' Label: '" + Label + "'";
}

bool TGpioChip::DoesSupportInterrupts() const
{
    return SupportsInterrupts == 1;
}

void TGpioChip::AddToEpoll(int epfd)
{
    for (const auto & listenedLine: ListenedLines) {
        struct epoll_event ep_event {};

        ep_event.events = EPOLLIN | EPOLLPRI;
        ep_event.data.fd = listenedLine.Fd;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenedLine.Fd, &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno) << "' with " << listenedLine.Line->Describe();
        }
    }
}

bool TGpioChip::TryListenLine(const PGpioLine & line, const TGpioLineConfig & config)
{
    gpioevent_request req {};

    strcpy(req.consumer_label, CONSUMER);
    req.lineoffset = line->GetOffset();
    req.handleflags |= GPIOHANDLE_REQUEST_INPUT;

    if (config.IsOpenDrain)
        req.handleflags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
    if (config.IsOpenSource)
        req.handleflags |= GPIOHANDLE_REQUEST_OPEN_SOURCE;
    if (config.IsActiveLow)
        req.handleflags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

    if (config.InterruptEdge == TGpioEdge::RISING)
        req.eventflags |= GPIOEVENT_REQUEST_RISING_EDGE;
    else if(config.InterruptEdge == TGpioEdge::FALLING)
        req.eventflags |= GPIOEVENT_REQUEST_FALLING_EDGE;
    else if(config.InterruptEdge == TGpioEdge::BOTH)
        req.eventflags |= GPIOEVENT_REQUEST_BOTH_EDGES;

    auto retVal = ioctl(Fd, GPIO_GET_LINEEVENT_IOCTL, &req);

    if (retVal < 0) {
        LOG(Error) << "GPIO_GET_LINEEVENT_IOCTL failed: " << strerror(errno);
    } else (retVal >= 0) {
        ListenedLines.push_back({ line, req.fd });
        LOG(Info) << "Listening to " << line->Describe();
    }

    return retVal >= 0;
}

void TGpioChip::AddToPolling(const PGpioLine & line, const TGpioLineConfig & config)
{
    uint32_t flags = 0;

    if (config.IsOpenDrain)
        flags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
    if (config.IsOpenSource)
        flags |= GPIOHANDLE_REQUEST_OPEN_SOURCE;
    if (config.IsActiveLow)
        flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

    if (config.Direction == TGpioDirection::Input)
        flags |= GPIOHANDLE_REQUEST_INPUT;
    else if (config.Direction == TGpioDirection::Output)
        flags |= GPIOHANDLE_REQUEST_OUTPUT;

    PollingLines[flags].Lines.push_back(line);
}

void TGpioChip::InitPolling()
{
    for (auto & flagsLines: PollingLines) {
        gpiohandle_request req;

        assert(flagsLines.second.Fd < 0);

        req.lines = 0;
        for (const auto & line: flagsLines.second.Lines) {
            req.lineoffsets[req.lines] = line->GetOffset();
            req.default_values[req.lines] = line->IsActiveLow();
            ++req.lines;
        }
        req.flags = flagsLines.first;
        strcpy(req.consumer_label, CONSUMER);

        if (ioctl(Fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
            LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno);
            wb_throw(TGpioDriverException, "unable to poll lines values")
        }

        flagsLines.second.Fd = req.fd;
    }
}

uint8_t TGpioChip::GetLineValue(uint32_t offset) const
{
    assert(offset < GetLineCount());

    return LinesValues[line->GetOffset()];
}

int TGpioChip::GetFd() const
{
    return Fd;
}
