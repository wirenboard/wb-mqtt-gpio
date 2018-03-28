#include "gpio_chip_driver.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "exceptions.h"
#include "utils.h"
#include "log.h"

#include <wblib/utils.h>

#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <fstream>
#include <algorithm>

#define LOG(logger) ::logger.Log() << "[gpio chip driver] "

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

TGpioChipDriver::TGpioChipDriver(const TGpioChipConfig & config)
{
    Chip = make_shared<TGpioChip>(config.Path);

    unordered_map<uint32_t, vector<vector<PGpioLine>>> pollLines;
    auto addToPoll = [&pollLines](const PGpioLine & line) {
        auto & lineBulks = pollLines[GetFlagsFromConfig(*line->GetConfig())];

        if (lineBulks.empty() || lineBulks.back().size() == GPIOHANDLES_MAX) {
            lineBulks.emplace_back();
        }

        lineBulks.back().push_back(line);
    };

    for (const auto & line: Chip->LoadLines(config.Lines)) {
        if (!ReleaseLineIfUsed(line)) {
            LOG(Error) << "Skipping " << line->DescribeShort();
        }

        const auto & config = line->GetConfig();

        if (config->Direction == EGpioDirection::Input) {
            auto interruptSupport = Chip->GetInterruptSupport();
            switch (interruptSupport) {
            case EInterruptSupport::UNKNOWN:
                if (TryListenLine(line)) {
                    Chip->SetInterruptSupport(EInterruptSupport::YES);
                } else {
                    LOG(Info) << Chip->Describe() << " does not support interrupts. Polling will be used instead.";
                    Chip->SetInterruptSupport(EInterruptSupport::NO);
                }
                break;

            case EInterruptSupport::YES:
                assert(TryListenLine(line));
                break;

            case EInterruptSupport::NO:
                addToPoll(line);
                break;
            }
        } else if (config->Direction == EGpioDirection::Output) {
            if (!InitOutput(line)) {
                LOG(Error) << "Skipping output" << line->DescribeShort();
            }
        } else {
            assert(false);
        }
    }

    if (Chip->GetInterruptSupport() == EInterruptSupport::UNKNOWN) {
        Chip->SetInterruptSupport(EInterruptSupport::NO);
    }

    /* Initialize polling if chip doesn't support interrupts */
    {
        assert(pollLines.empty() || Chip->GetInterruptSupport() == EInterruptSupport::NO);

        for (const auto & flagsLines: pollLines) {
            const auto flags = flagsLines.first;
            const auto & lineBulks = flagsLines.second;

            for (const auto & lines: lineBulks) {
                if (!InitLinesPolling(flags, lines)) {
                    auto logError = move(LOG(Error) << "Failed to initialize polling of lines:");
                    for (const auto & line: lines) {
                        logError << "\n\t" << line->DescribeShort();
                    }
                }
            }
        }
    }

    AutoDetectInterruptEdges();
}

TGpioChipDriver::TGpioLinesByOffsetMap TGpioChipDriver::MapLinesByOffset() const
{
    TGpioLinesByOffsetMap linesByOffset;

    FOR_EACH_LINE(this, line) {
        linesByOffset[line->GetOffset()] = line;
    });

    return linesByOffset;
}

void TGpioChipDriver::AddToEpoll(int epfd)
{
    if (Chip->GetInterruptSupport() != EInterruptSupport::YES)
        return;

    FOR_EACH_LINE(this, line) {
        if (line->IsOutput()) {
            return;
        }

        struct epoll_event ep_event {};

        ep_event.events = EPOLLIN | EPOLLPRI;
        ep_event.data.fd = line->GetFd();

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, line->GetFd(), &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno) << "' at " << line->DescribeShort();
        }
    });
}

bool TGpioChipDriver::HandleInterrupt(int count, struct epoll_event * events)
{
    bool isHandled = false;

    if (Chip->GetInterruptSupport() != EInterruptSupport::YES)
        return isHandled;

    for (int i = 0; i < count; i++) {
        auto itFdLines = Lines.find(events[i].data.fd);
        if (itFdLines != Lines.end()) {
            isHandled = true;
            auto fd = itFdLines->first;
            const auto & lines = itFdLines->second;
            assert(lines.size() == 1);

            const auto & line = lines.front();

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
                gpiohandle_data data;
                if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
                    LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
                    wb_throw(TGpioDriverException, "unable to get line value");
                }

                line->SetCachedValue(line->PrepareValue(data.values[0]));
            }
        }
    }

    return isHandled;
}

bool TGpioChipDriver::PollLines()
{
    bool isHandled = false;

    for (const auto & fdLines: Lines) {
        const auto & lines = fdLines.second;
        assert(!lines.empty());

        isHandled = true;

        PollLinesValues(lines);
    }

    return isHandled;
}

void TGpioChipDriver::ForEachLine(const TGpioLineHandler & handler) const
{
    for (const auto & fdLines: Lines) {
        const auto & lines = fdLines.second;
        assert(!lines.empty());

        for (const auto & line: lines) {
            handler(line);
        }
    }
}

bool TGpioChipDriver::ReleaseLineIfUsed(const PGpioLine & line)
{
    if (!line->IsUsed())
        return true;

    LOG(Warn) << line->Describe() << " is used by '" << line->GetConsumer() << "'.";
    if (line->GetConsumer() == "sysfs") {
        ofstream unexportGpio("/sys/class/gpio/unexport");
        if (unexportGpio.is_open()) {
            LOG(Info) << "Trying to unexport...";
            unexportGpio << ToSysfsGpio(line);
        }
    }

    line->UpdateInfo();

    if (line->IsUsed()) {
        LOG(Error) << "Failed to release " << line->DescribeShort();
        return false;
    }

    LOG(Info) << line->DescribeShort() << " successfully released";
    return true;
}

bool TGpioChipDriver::TryListenLine(const PGpioLine & line)
{
    const auto & config = line->GetConfig();
    assert(config->Direction == EGpioDirection::Input);

    gpioevent_request req {};

    strcpy(req.consumer_label, CONSUMER);
    req.lineoffset = line->GetOffset();
    req.handleflags = GetFlagsFromConfig(*config);

    if (config->InterruptEdge == EGpioEdge::RISING)
        req.eventflags |= GPIOEVENT_REQUEST_RISING_EDGE;
    else if(config->InterruptEdge == EGpioEdge::FALLING)
        req.eventflags |= GPIOEVENT_REQUEST_FALLING_EDGE;
    else if(config->InterruptEdge == EGpioEdge::BOTH)
        req.eventflags |= GPIOEVENT_REQUEST_BOTH_EDGES;

    errno = 0;
    if (ioctl(Chip->GetFd(), GPIO_GET_LINEEVENT_IOCTL, &req) < 0) {
        auto error = errno;
        LOG(Warn) << "GPIO_GET_LINEEVENT_IOCTL failed: " << strerror(error) << " at " << line->DescribeShort();
        return false;
    } else {
        Lines[req.fd].push_back(line);
        assert(Lines[req.fd].size() == 1);
        line->SetFd(req.fd);

        LOG(Info) << "Listening to " << line->DescribeShort();
        return true;
    }
}

bool TGpioChipDriver::InitOutput(const PGpioLine & line)
{
    const auto & config = line->GetConfig();
    assert(config->Direction == EGpioDirection::Output);

    gpiohandle_request req;

    req.lines = 1;
    req.lineoffsets[0] = line->GetOffset();
    req.default_values[0] = line->IsActiveLow();
    req.flags = GetFlagsFromConfig(*config);
    strcpy(req.consumer_label, CONSUMER);

    if (ioctl(Chip->GetFd(), GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno) << " at " << line->DescribeShort();
        return false;
    }

    Lines[req.fd].push_back(line);
    assert(Lines[req.fd].size() == 1);
    line->SetFd(req.fd);

    line->SetValue(config->InitialState);

    LOG(Info) << "Initialized output " << line->DescribeShort();
    return true;
}

bool TGpioChipDriver::InitLinesPolling(uint32_t flags, const vector<PGpioLine> & lines)
{
    assert(lines.size() <= GPIOHANDLES_MAX);

    gpiohandle_request req;
    req.lines = lines.size();
    req.flags = flags;
    strcpy(req.consumer_label, CONSUMER);

    for (auto & line: lines) {
        req.lineoffsets[req.lines] = line->GetOffset();
        req.default_values[req.lines] = line->IsActiveLow();
    }

    if (ioctl(Chip->GetFd(), GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno);
        return false;
    }

    auto & initialized = Lines[req.fd];

    assert(initialized.empty());
    initialized.reserve(lines.size());

    for (const auto & line: initialized) {
        line->SetFd(req.fd);
        initialized.push_back(line);
    }

    return true;
}

void TGpioChipDriver::PollLinesValues(const TGpioLines & lines)
{
    assert(!lines.empty());

    auto fd = lines.front()->GetFd();

    gpiohandle_data data;
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to get line value");
    }

    for (uint32_t i = 0; i < lines.size(); ++i) {
        const auto & line = lines[i];
        assert(line->GetFd() == fd);

        bool oldValue = line->GetValue();
        bool newValue = line->PrepareValue(data.values[i]);

        LOG(Debug) << "Poll " << line->DescribeShort() << " old value: " << oldValue << " new value: " << newValue;

        EGpioEdge edge;
        if (oldValue == 0 && newValue == 1) {
            edge = EGpioEdge::RISING;
        } else if (oldValue == 1 && newValue == 0) {
            edge = EGpioEdge::FALLING;
        } else {
            edge = EGpioEdge::BOTH;
        }

        line->HandleInterrupt(edge);

        if (!line->IsDebouncing()) {
            line->SetCachedValue(newValue);
        }
    }
}

void TGpioChipDriver::ReadLinesValues(const TGpioLines & lines)
{
    assert(!lines.empty());
    auto fd = lines.front()->GetFd();

    gpiohandle_data data;
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to get line value");
    }

    uint32_t i = 0;
    for (const auto & line: lines) {
        assert(line->GetFd() == fd);

        line->SetCachedValue(line->PrepareValue(data.values[i++]));
    }
}

void TGpioChipDriver::AutoDetectInterruptEdges()
{
    static auto doesNeedAutoDetect = [](const PGpioLine & line) {
        if (line->IsHandled() && !line->IsOutput()) {
            if (const auto & counter = line->GetCounter()) {
                return counter->GetInterruptEdge() == EGpioEdge::BOTH;
            }
        }

        return false;
    };

    vector<TGpioLines> pollingLines;
    unordered_map<PGpioLine, int> linesSum;

    for (const auto & fdLines: Lines) {
        const auto & lines = fdLines.second;

        if (any_of(lines.begin(), lines.end(), doesNeedAutoDetect)) {
            pollingLines.push_back(lines);
        }
    }

    const auto testCount = 10;

    for (auto i = 0; i < testCount; ++i) {
        for (const auto & lines: pollingLines) {
            ReadLinesValues(lines);

            for (const auto & line: lines) {
                if (doesNeedAutoDetect(line)) {
                    linesSum[line] += line->GetValue();
                }
            }
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
