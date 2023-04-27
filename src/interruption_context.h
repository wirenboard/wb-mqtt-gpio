#pragma once

#include <chrono>
#include <linux/types.h>

struct TInterruptionContext
{
    static bool InterruptTimestampClockIsMonotonic;

    const int Count;
    const struct epoll_event* Events;
    std::chrono::nanoseconds Diff;

    TInterruptionContext(int count, struct epoll_event* events);
    TInterruptionContext(const TInterruptionContext&) = delete;
    TInterruptionContext(TInterruptionContext&&) = delete;

    std::chrono::steady_clock::time_point ToSteadyClock(__u64 timestamp) const;

    static void SetMonotonicClockForInterruptTimestamp();
};
