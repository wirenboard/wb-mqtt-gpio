#pragma once

#include "declarations.h"
#include "types.h"

#include <functional>
#include <unordered_map>
#include <vector>

using TGpioLinesByOffsetMap = std::unordered_map<uint32_t, PGpioLine>;

class TGpioChipDriver
{
    using TGpioLines = std::vector<PGpioLine>;
    using TGpioLinesMap = std::unordered_map<int, TGpioLines>;
    using TGpioTimersMap = std::unordered_map<int, TGpioLines>;
    using TGpioLinesByOffsetMap = std::unordered_map<uint32_t, PGpioLine>;

    TGpioLinesMap Lines;
    TGpioLinesByOffsetMap InitiallyDisconnectedLines;
    TGpioTimersMap Timers;
    PGpioChip Chip;
    bool AddedToEpoll;

public:
    using TGpioLineHandler = std::function<void(const PGpioLine&)>;

    explicit TGpioChipDriver(const TGpioChipConfig&);
    ~TGpioChipDriver();

    TGpioLinesByOffsetMap MapLinesByOffset() const;
    const TGpioLinesByOffsetMap& MapInitiallyDisconnectedLinesByOffset() const;

    void AddToEpoll(int epfd);
    bool HandleInterrupt(const TInterruptionContext&);

    int CreateIntervalTimer();
    void SetIntervalTimer(int tfd, std::chrono::microseconds intervalUs);

    bool PollLines();

    void ForEachLine(const TGpioLineHandler&) const;

private:
    bool ReleaseLineIfUsed(const PGpioLine&);
    bool TryListenLine(const PGpioLine&);
    bool InitOutput(const PGpioLine&, bool value);
    bool InitInputInterrupts(const PGpioLine&);
    bool InitLinesPolling(uint32_t flags, const TGpioLines& lines);

    void PollLinesValues(const TGpioLines&);
    void ReadLinesValues(const TGpioLines&);

    void ReListenLine(PGpioLine);
    void AutoDetectInterruptEdges();
    void ReadInputValues();

    bool HandleTimerInterrupt(const PGpioLine&);
    bool HandleGpioInterrupt(const PGpioLine& line, const TInterruptionContext& ctx);
};

#define FOR_EACH_LINE(driver, line) driver->ForEachLine([&](const PGpioLine & line)
