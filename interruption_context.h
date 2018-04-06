#pragma once

#include <chrono>

struct TInterruptionContext
{
    const int                                     Count;
    const struct epoll_event *                    Events;
    const std::chrono::system_clock::time_point   SystemTime;
    const std::chrono::steady_clock::time_point   SteadyTime;

    TInterruptionContext(int count, struct epoll_event * events)
        : Count(count)
        , Events(events)
        , SystemTime(std::chrono::system_clock::now())
        , SteadyTime(std::chrono::steady_clock::now())
    {}

    TInterruptionContext(const TInterruptionContext &) = delete;
    TInterruptionContext(TInterruptionContext &&) = delete;

    inline std::chrono::steady_clock::time_point ToSteadyClock(const std::chrono::system_clock::time_point & systemTime) const
    {
        return SteadyTime + (SystemTime - systemTime);
    }
};
