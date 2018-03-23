#include "gpio_chip.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "exceptions.h"
#include "utils.h"
#include "log.h"

#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <fstream>

#define LOG(logger) ::logger.Log() << "[gpio chip] "

using namespace std;

const auto CONSUMER = "wb-homa-gpio";

namespace
{
    uint32_t GetFlagsFromConfig(const TGpioLineConfig & config)
    {
        uint32_t flags = 0;

        if (config.Direction == EGpioDirection::Input)
            flags |= GPIOHANDLE_REQUEST_INPUT;
        else if (config.Direction == EGpioDirection::Output)
            flags |= GPIOHANDLE_REQUEST_OUTPUT;

        if (config.IsOpenDrain)
            flags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
        if (config.IsOpenSource)
            flags |= GPIOHANDLE_REQUEST_OPEN_SOURCE;
        if (config.IsActiveLow)
            flags |= GPIOHANDLE_REQUEST_ACTIVE_LOW;

        return flags;
    }
}

TGpioChip::TGpioChip(const string & path)
    : Fd(-1)
    , Path(path)
    , SupportsInterrupts(-1)
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

    Lines.resize(info.lines);
    LinesValues.resize(info.lines);
    LinesValuesChanged.resize(info.lines);

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

void TGpioChip::LoadLines(const TLinesConfig & linesConfigs)
{
    for (size_t offset = 0; offset < Lines.size(); ++offset) {
        if (linesConfigs.count(offset) == 0) {
            auto line = make_shared<TGpioLine>(shared_from_this(), offset);
            LOG(Info) << line->Describe() << " is not in config. Skipping.";
            Lines[offset] = line;
            continue;
        }

        const auto & config = linesConfigs.at(offset);
        auto line = make_shared<TGpioLine>(shared_from_this(), config);

        Lines[offset] = line;

        if (line->IsUsed()) {
            LOG(Warn) << line->Describe() << " is used by '" << line->GetConsumer() << "'. ";
            if (line->GetConsumer() == "sysfs") {
                ofstream unexportGpio("/sys/class/gpio/unexport");
                if (unexportGpio.is_open()) {
                    LOG(Info) << "Trying to unexport...";
                    unexportGpio << ToSysfsGpio(line);
                } else {
                    LOG(Warn) << "Unable to unexport. Skipping.";
                    continue;
                }
            } else {
                LOG(Warn) << "Consumer is not sysfs (" << line->GetConsumer() << "). Skipping.";
                continue;
            }

            LOG(Info) << "Line usage successfully resolved";
        }

        if (config.Direction == EGpioDirection::Input) {
            if (SupportsInterrupts != 0) {
                auto interruptSupport = TryListenLine(line, config);
                if (interruptSupport == EInterruptSupport::NO) {
                    LOG(Info) << Describe() << " does not support events. Polling will be used instead.";
                    AddToPolling(line, config);
                    assert(SupportsInterrupts != 1);   // fail after success is not expected: chip either supports events or not.
                    SupportsInterrupts = 0;
                } else if (interruptSupport == EInterruptSupport::YES) {
                    SupportsInterrupts = 1;
                }
            }

            if (SupportsInterrupts == -1) {
                SupportsInterrupts = 0;
            }
        } else if (config.Direction == EGpioDirection::Output) {
            InitOutput(line, config);
        } else {
            assert(false);
        }
    }

    InitInputs();

    for (const auto & line: Lines) {
        line->UpdateInfo();
    }

    AutoDetectInterruptEdges();
}

void TGpioChip::PollLinesValues()
{
    for (auto & flagsLines: PollingLines) {
        PollLinesValues(flagsLines.second);
    }
}

void TGpioChip::PollLinesValues(const TPollingLines & pollingLines)
{
    const auto & lines = pollingLines.Lines;

    gpiohandle_data data {};
    if (ioctl(pollingLines.Fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to poll lines values");
    }

    for (uint32_t i = 0; i < lines.size(); ++i) {
        AcceptLineValue(lines[i]->GetOffset(), data.values[i]);
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

const vector<PGpioLine> & TGpioChip::GetLines() const
{
    return Lines;
}

vector<pair<PGpioLine, EGpioEdge>> TGpioChip::HandleInterrupt(int count, struct epoll_event * events)
{
    vector<pair<PGpioLine, EGpioEdge>> res;

    for (int i = 0; i < count; i++) {
        auto itListenedLine = ListenedLines.find(events[i].data.fd);
        if (itListenedLine != ListenedLines.end()) {
            auto fd = itListenedLine->first;
            auto line = itListenedLine->second;

            EGpioEdge edge;
            {
                gpioevent_data data {};
                if (read(fd, &data, sizeof(data)) < 0) {
                    LOG(Error) << "Read gpioevent_data failed: " << strerror(errno);
                    wb_throw(TGpioDriverException, "unable to read line event data");
                }
                edge = (data.id == GPIOEVENT_EVENT_RISING_EDGE) ? EGpioEdge::RISING
                                                                : EGpioEdge::FALLING;
            }

            line->HandleInterrupt(edge);

            if (!line->IsDebouncing()) {   // update value
                gpiohandle_data data {};
                if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
                    LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
                    wb_throw(TGpioDriverException, "unable to get line value");
                }

                AcceptLineValue(line->GetOffset(), data.values[0]);
            }

            res.push_back({ line, edge });
        }
    }

    return res;
}

uint32_t TGpioChip::GetNumber() const
{
    return GpioPathToChipNumber(Path);
}

PGpioLine TGpioChip::GetLine(uint32_t offset) const
{
    return Lines[offset];
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
        ep_event.data.fd = listenedLine.first;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenedLine.first, &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno) << "' at " << listenedLine.second->DescribeShort();
        }
    }
}

TGpioChip::EInterruptSupport TGpioChip::TryListenLine(const PGpioLine & line, const TGpioLineConfig & config)
{
    assert(config.Direction == EGpioDirection::Input);

    gpioevent_request req {};

    strcpy(req.consumer_label, CONSUMER);
    req.lineoffset = line->GetOffset();
    req.handleflags = GetFlagsFromConfig(config);

    if (config.InterruptEdge == EGpioEdge::RISING)
        req.eventflags |= GPIOEVENT_REQUEST_RISING_EDGE;
    else if(config.InterruptEdge == EGpioEdge::FALLING)
        req.eventflags |= GPIOEVENT_REQUEST_FALLING_EDGE;
    else if(config.InterruptEdge == EGpioEdge::BOTH)
        req.eventflags |= GPIOEVENT_REQUEST_BOTH_EDGES;

    errno = 0;
    auto retVal = ioctl(Fd, GPIO_GET_LINEEVENT_IOCTL, &req);
    auto error = errno;

    if (retVal < 0) {
        LOG(Error) << "GPIO_GET_LINEEVENT_IOCTL failed: " << strerror(error) << " at " << line->DescribeShort();
    } else {
        auto inserted = ListenedLines.insert({req.fd, line}).second;
        assert(inserted);
        line->SetFd(req.fd);
        LOG(Info) << "Listening to " << line->Describe();
    }

    switch(errno) {
        case 0:
            return EInterruptSupport::YES;
        case EBUSY:
            return EInterruptSupport::UNKNOWN;
        default:
            return EInterruptSupport::NO;
    }
}

void TGpioChip::AddToPolling(const PGpioLine & line, const TGpioLineConfig & config)
{
    assert(config.Direction == EGpioDirection::Input);

    auto flags = GetFlagsFromConfig(config);

    PollingLines[flags].Lines.push_back(line);
}

void TGpioChip::InitInputs()
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
            wb_throw(TGpioDriverException, "unable to poll lines values");
        }

        flagsLines.second.Fd = req.fd;

        for (const auto & line: flagsLines.second.Lines) {
            line->SetFd(req.fd);
        }
    }
}

void TGpioChip::InitOutput(const PGpioLine & line, const TGpioLineConfig & config)
{
    assert(config.Direction == EGpioDirection::Output);

    gpiohandle_request req;

    req.lines = 1;
    req.lineoffsets[0] = line->GetOffset();
    req.default_values[0] = line->IsActiveLow();
    req.flags = GetFlagsFromConfig(config);
    strcpy(req.consumer_label, CONSUMER);

    if (ioctl(Fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to init output line " + line->Describe());
    }

    bool inserted = OutputLines.insert({ line, req.fd }).second;
    assert(inserted);

    line->SetFd(req.fd);
}

void TGpioChip::AutoDetectInterruptEdges()
{
    static auto doesNeedAutoDetect = [](const PGpioLine & line) {
        if (line->IsHandled() && !line->IsOutput()) {
            if (const auto & counter = line->GetCounter()) {
                return counter->GetInterruptEdge() == EGpioEdge::BOTH;
            }
        }

        return false;
    };

    unordered_map<int, TPollingLines> pollingLinesByFds;
    unordered_map<PGpioLine, int> linesSum;

    if (DoesSupportInterrupts()) {
        for (const auto & line: Lines) {
            if (doesNeedAutoDetect(line)) {
                auto fd = line->GetFd();
                auto & pollingLines = pollingLinesByFds[fd];

                pollingLines.Fd = fd;
                pollingLines.Lines.push_back(line);
                linesSum[line] = 0;
            }
        }
    } else {
        for (const auto & line: Lines) {
            if (doesNeedAutoDetect(line)) {
                auto fd = line->GetFd();
                auto & pollingLines = pollingLinesByFds[fd];

                if (pollingLines.Lines.empty()) {
                    assert(PollingLines.count(line->GetFlags()));

                    pollingLines = PollingLines[line->GetFlags()];
                } else {
                    assert(PollingLines[line->GetFlags()].Fd == fd);
                }
                linesSum[line] = 0;
            }
        }
    }

    const auto testCount = 10;

    for (auto i = 0; i < testCount; ++i) {
        for (const auto & fdPollingLines: pollingLinesByFds) {
            PollLinesValues(fdPollingLines.second);
        }

        for (auto & lineSum: linesSum) {
            const auto & line = lineSum.first;
            auto & sum = lineSum.second;

            sum += line->GetValue();
        }
    }

    for (auto & lineSum: linesSum) {
        const auto & line = lineSum.first;
        auto & sum = lineSum.second;

        auto edge = sum < testCount ? EGpioEdge::RISING : EGpioEdge::FALLING;

        LOG(Debug) << "Auto detected edge for line: " << line->DescribeShort() << ": " << GpioEdgeToString(edge);

        line->GetCounter()->SetInterruptEdge(edge);
    }
}

int TGpioChip::GetFd() const
{
    return Fd;
}

uint8_t TGpioChip::GetLineValue(uint32_t offset) const
{
    assert(offset < GetLineCount());

    return LinesValues[offset];
}

bool TGpioChip::IsLineValueChanged(uint32_t offset) const
{
    assert(offset < GetLineCount());

    return LinesValuesChanged[offset];
}

void TGpioChip::SetLineValue(uint32_t offset, uint8_t value)
{
    assert(offset < GetLineCount());
    const auto & line = Lines[offset];
    assert(OutputLines.count(line));
    int fd = OutputLines.at(line);

    gpiohandle_data data {};

    data.values[0] = value;

    if (ioctl(fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << "GPIOHANDLE_SET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to set value '" + to_string((int)value) + "' to line " + line->DescribeShort());
    }

    AcceptLineValue(offset, value);
}

void TGpioChip::AcceptLineValue(uint32_t offset, uint8_t value)
{
    assert(offset < GetLineCount());

    auto & currentValue = LinesValues[offset];
    LinesValuesChanged[offset] = (value != currentValue);
    currentValue = value;
}
