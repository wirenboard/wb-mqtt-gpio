#pragma once

#include <chrono>

struct TInterruptionContext
{
    const int                      Count;
    const struct epoll_event *     Events;
    const std::chrono::nanoseconds Diff;

    TInterruptionContext(int count, struct epoll_event * events)
        : Count(count)
        , Events(events)
        , Diff(std::chrono::steady_clock::now().time_since_epoch() - std::chrono::system_clock::now().time_since_epoch())
    {}

    TInterruptionContext(const TInterruptionContext &) = delete;
    TInterruptionContext(TInterruptionContext &&) = delete;

    inline std::chrono::steady_clock::time_point ToSteadyClock(const std::chrono::system_clock::time_point & systemTime) const
    {
        return std::chrono::steady_clock::time_point(systemTime.time_since_epoch() + Diff);
    }
};
