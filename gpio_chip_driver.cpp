#include "gpio_chip_driver.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "interruption_context.h"
#include "exceptions.h"
#include "utils.h"
#include "log.h"

#include <wblib/utils.h>

#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <fstream>
#include <algorithm>

#define LOG(logger) ::logger.Log() << "[gpio chip driver] "

using namespace std;

const auto CONSUMER = "wb-mqtt-gpio";


namespace
{
    uint32_t GetFlagsFromConfig(const TGpioLineConfig & config, bool asIs = false)
    {
        uint32_t flags = 0;

        if (!asIs) {
            if (config.Direction == EGpioDirection::Input)
                flags |= GPIOHANDLE_REQUEST_INPUT;
            else if (config.Direction == EGpioDirection::Output)
                flags |= GPIOHANDLE_REQUEST_OUTPUT;
        }
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
    : AddedToEpoll(false)
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
        switch (line->GetConfig()->Direction) {
            case EGpioDirection::Input: {
                if (!InitInputInterrupts(line)) {
                    addToPoll(line);
                }
                break;
            }
            case EGpioDirection::Output: {
                if (!InitOutput(line)) {
                    LOG(Error) << "Skipping output" << line->DescribeShort();
                }
                break;
            }
        }
    }

    /* Initialize polling if line doesn't support interrupts */
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

    if (Lines.empty()) {
        wb_throw(TGpioDriverException, "Failed to initialize any line");
    }

    AutoDetectInterruptEdges();
    ReadInputValues();
}

TGpioChipDriver::~TGpioChipDriver()
{
    for (const auto & fdLines: Lines) {
        auto fd = fdLines.first;
        const auto & lines = fdLines.second;

        {
            auto logDebug = move(LOG(Debug) << "Close fd for:");
            for (const auto & line: lines) {
                logDebug << "\n\t" << line->DescribeShort();
                close(line->GetTimerFd());
            }
        }

        close(fd);
    }
}

int TGpioChipDriver::CreateIntervalTimer()
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd == -1) {
        LOG(Error) << "timerfd_create failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to create timer: timerfd_create failed with " + string(strerror(errno)));
    }
    return tfd;
}

void TGpioChipDriver::SetIntervalTimer(int tfd, std::chrono::microseconds intervalUs)
{
    auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(intervalUs).count();

    struct itimerspec ts;
    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = nsec;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    if (timerfd_settime(tfd, 0, &ts, NULL) < 0) {
        LOG(Error) << "timerfd_settime failed: " << strerror(errno);
        close(tfd);
        wb_throw(TGpioDriverException, "unable to setup timer: timerfd_settime failed with " + string(strerror(errno)));
    }
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
    AddedToEpoll = true;

    FOR_EACH_LINE(this, line) {
        if (line->IsOutput() || line->GetInterruptSupport() != EInterruptSupport::YES) {
            return;
        }

        struct epoll_event ep_event {};

        ep_event.events = EPOLLIN | EPOLLPRI;
        ep_event.data.fd = line->GetFd();

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, line->GetFd(), &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno) << "' at " << line->DescribeShort();
        }

        auto timerFd = line->GetTimerFd();
        ep_event.events = EPOLLIN;
        ep_event.data.fd = timerFd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, timerFd, &ep_event) < 0) {
            LOG(Error) << "epoll_ctl error: '" << strerror(errno);
            wb_throw(TGpioDriverException, "unable to add timer to epoll: epoll_ctl failed with " + string(strerror(errno)));
        }
    });
}

bool TGpioChipDriver::HandleGpioInterrupt(const PGpioLine & line, const TInterruptionContext & ctx)
{
    bool isHandled = false;
    auto fd = line->GetFd();

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv {0}; // do not block

    while (auto retVal = select(fd + 1, &rfds, nullptr, nullptr, &tv)) {
        if (retVal < 0) {
            LOG(Error) << "select failed: " << strerror(errno);
            wb_throw(TGpioDriverException, "unable to read line event data: select failed with " + string(strerror(errno)));
        }

        gpioevent_data data {};
        if (read(fd, &data, sizeof(data)) < 0) {
            LOG(Error) << "Read gpioevent_data failed: " << strerror(errno);
            wb_throw(TGpioDriverException, "unable to read line event data: gpioevent_data failed with " + string(strerror(errno)));
        }

        auto edge = data.id == GPIOEVENT_EVENT_RISING_EDGE ? EGpioEdge::RISING : EGpioEdge::FALLING;
        auto time = ctx.ToSteadyClock(data.timestamp);

        if (line->HandleInterrupt(edge, time) == EInterruptStatus::Handled) {   // update gpioline's last interruption ts
            gpiohandle_data data;
            if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
                LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
                wb_throw(TGpioDriverException, "unable to get line value");
            }

            line->SetCachedValueUnfiltered(data.values[0]);  // all interrupt events
            SetIntervalTimer(line->GetTimerFd(), line->GetConfig()->DebounceTimeout);
            isHandled = true;
        }
    }
    return isHandled;
}

bool TGpioChipDriver::HandleTimerInterrupt(const PGpioLine & line)
{
    bool isHandled = false;
    auto tfd = line->GetTimerFd();
    auto interruptionTs = chrono::steady_clock::now();

    if (line->UpdateIfStable(interruptionTs)) {
        isHandled = true;
        SetIntervalTimer(tfd, std::chrono::microseconds(0)); // disarm timer

        const auto & gpioCounter = line->GetCounter();
        if (gpioCounter) {
            auto prevTimePoint = line->GetInterruptionTimepoint();
            auto intervalUs = prevTimePoint.time_since_epoch() == chrono::nanoseconds::zero() ? chrono::microseconds::zero()
                                                    : line->GetIntervalFromPreviousInterrupt(interruptionTs);
            gpioCounter->HandleInterrupt(line->GetInterruptEdge(), intervalUs);
        }
    }
    return isHandled;
}

bool TGpioChipDriver::HandleInterrupt(const TInterruptionContext & ctx)
{
    bool isHandled = false;

    for (int i = 0; i < ctx.Count; i++) {
        auto fd = ctx.Events[i].data.fd;

        auto itFdTimers = Timers.find(fd);
        auto itFdLines = Lines.find(fd);

        // timer event fired: check, is value stable or bouncing
        if (itFdTimers != Timers.end()) {
            const auto & line = itFdTimers->second.front();
            isHandled = HandleTimerInterrupt(line);

        // gpio interrupt event fired: set stable-val-check timer
        } else if (itFdLines != Lines.end()) {
            const auto & lines = itFdLines->second;
            assert(lines.size() == 1);
            const auto & line = lines.front();
            HandleGpioInterrupt(line, ctx);
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
            try {
                unexportGpio << Utils::ToSysfsGpio(line);
            } catch (const TGpioDriverException & e) {
                LOG(Error) << "During unexport: " << e.what();
            }
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
    req.lineoffset  = line->GetOffset();
    req.handleflags = GetFlagsFromConfig(*config);

    req.eventflags = 0;

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
    }

    Lines[req.fd].push_back(line);
    assert(Lines[req.fd].size() == 1);
    line->SetFd(req.fd);

    auto timerFd = CreateIntervalTimer();
    line->SetTimerFd(timerFd);
    Timers[timerFd].push_back(line);

    LOG(Info) << "Listening to " << line->DescribeShort();
    return true;
}

bool TGpioChipDriver::InitOutput(const PGpioLine & line)
{
    const auto & config = line->GetConfig();
    assert(config->Direction == EGpioDirection::Output);

    gpiohandle_request req;
    memset(&req, 0, sizeof(gpiohandle_request));
    req.lines = 1;
    req.lineoffsets[0] = line->GetOffset();
    req.default_values[0] = config->InitialState;
    req.flags = GetFlagsFromConfig(*config, line->IsOutput());
    strcpy(req.consumer_label, CONSUMER);

    if (ioctl(Chip->GetFd(), GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno) << " at " << line->DescribeShort();
        return false;
    }

    Lines[req.fd].push_back(line);
    assert(Lines[req.fd].size() == 1);
    line->SetFd(req.fd);

    if (Debug.IsEnabled()) {
        gpiohandle_data data;
        if (ioctl(line->GetFd(), GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) >= 0) {
            LOG(Debug) << "Initialized output " << line->DescribeShort() << " = " << static_cast<int>(data.values[0]);
        }
    } else {
        LOG(Info) << "Initialized output " << line->DescribeShort();
    }

    return true;
}

bool TGpioChipDriver::InitInputInterrupts(const PGpioLine & line)
{
    switch (line->GetInterruptSupport()) {
        case EInterruptSupport::UNKNOWN:
        case EInterruptSupport::YES: {
            if (TryListenLine(line)) {
                line->SetInterruptSupport(EInterruptSupport::YES);
                return true;
            }
            LOG(Info) << line->Describe() << " does not support interrupts. Polling will be used instead.";
            line->SetInterruptSupport(EInterruptSupport::NO);
            return false;
        }

        case EInterruptSupport::NO: {
            return false;
        }
    }
}

bool TGpioChipDriver::InitLinesPolling(uint32_t flags, const vector<PGpioLine> & lines)
{
    assert(lines.size() <= GPIOHANDLES_MAX);

    gpiohandle_request req;
    req.lines = 0;
    req.flags = flags;
    strcpy(req.consumer_label, CONSUMER);

    for (auto & line: lines) {
        req.lineoffsets[req.lines] = line->GetOffset();
        req.default_values[req.lines] = line->IsActiveLow();
        ++req.lines;
    }

    if (ioctl(Chip->GetFd(), GPIO_GET_LINEHANDLE_IOCTL, &req) < 0) {
        LOG(Error) << "GPIO_GET_LINEHANDLE_IOCTL failed: " << strerror(errno);
        return false;
    }

    auto & initialized = Lines[req.fd];

    assert(initialized.empty());
    initialized.reserve(lines.size());

    for (const auto & line: lines) {
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

    auto now = chrono::steady_clock::now();
    for (uint32_t i = 0; i < lines.size(); ++i) {
        const auto & line = lines[i];
        assert(line->GetFd() == fd);

        bool oldValue = line->GetValue();
        bool newValue = data.values[i];

        LOG(Debug) << "Poll " << line->DescribeShort() << " old value: " << oldValue << " new value: " << newValue;

        if (!line->IsOutput()) {
            /* if value changed for input we simulate interrupt */
            if (oldValue != newValue) {
                auto edge = newValue ? EGpioEdge::RISING : EGpioEdge::FALLING;
                if (line->HandleInterrupt(edge, now) == EInterruptStatus::Handled) {
                    line->SetCachedValue(newValue);
                }
            } else {    /* in other case let line do idle actions */
                line->Update();
            }
        } else {    /* for output just set value to cache: it will publish it if changed */
            line->SetCachedValue(newValue);
        }
    }
}

void TGpioChipDriver::ReadLinesValues(const TGpioLines & lines)
{
    if (lines.empty()) {
        return;
    }
    auto fd = lines.front()->GetFd();

    gpiohandle_data data;
    if (ioctl(fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        LOG(Error) << "GPIOHANDLE_GET_LINE_VALUES_IOCTL failed: " << strerror(errno);
        wb_throw(TGpioDriverException, "unable to get line value");
    }

    uint32_t i = 0;
    for (const auto & line: lines) {
        assert(line->GetFd() == fd);

        line->SetCachedValue(data.values[i++]);
    }
}

void TGpioChipDriver::ReListenLine(PGpioLine line)
{
    assert(!AddedToEpoll);

    auto oldFd = line->GetFd();
    auto oldTimerFd = line->GetTimerFd();

    assert(oldFd > -1);

    Lines.erase(oldFd);
    Timers.erase(oldTimerFd);
    close(oldFd);
    close(oldTimerFd);

    bool ok = TryListenLine(line);
    assert(ok);
    if (!ok) {
        LOG(Error) << "Unable to re-listen to " << line->DescribeShort();
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

        LOG(Info) << "Auto detected edge for line: " << line->DescribeShort() << ": " << GpioEdgeToString(edge);

        line->GetCounter()->SetInterruptEdge(edge);
        line->GetConfig()->InterruptEdge = edge;

        ReListenLine(line);
    }
}

void TGpioChipDriver::ReadInputValues()
{
    for (const auto & fdLines: Lines) {
        TGpioLines linesToRead;
        for (auto line: fdLines.second) {
            if (!line->IsOutput() && !line->GetCounter()) {
                linesToRead.push_back(line);
            }
        }
        ReadLinesValues(linesToRead);
    }
}
