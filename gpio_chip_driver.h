#pragma once

#include "declarations.h"
#include "types.h"

#include <vector>
#include <unordered_map>


class TGpioChipDriver
{
    using TGpioLines    = std::vector<PGpioLine>;
    using TGpioLinesMap = std::unordered_map<int, TGpioLines>;

    TGpioLinesMap Lines;
    PGpioChip     Chip;

public:
    using TGpioLinesByOffsetMap = std::unordered_map<uint32_t, PGpioLine>;
    using TGpioLineHandler = std::function<void(const PGpioLine &)>;

    explicit TGpioChipDriver(const TGpioChipConfig &);

    TGpioLinesByOffsetMap MapLinesByOffset() const;

    void AddToEpoll(int epfd);
    bool HandleInterrupt(int count, struct epoll_event * events);

    bool PollLines();

    void ForEachLine(const TGpioLineHandler &) const;

private:
    bool ReleaseLineIfUsed(const PGpioLine &);
    bool TryListenLine(const PGpioLine &);
    bool InitOutput(const PGpioLine &);
    bool InitLinesPolling(uint32_t flags, const TGpioLines & lines);

    void PollLinesValues(const TGpioLines &);
    void ReadLinesValues(const TGpioLines &);

    void AutoDetectInterruptEdges();
};

#define FOR_EACH_LINE(driver, line) driver->ForEachLine([&](const PGpioLine & line)
