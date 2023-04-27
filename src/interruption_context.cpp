#include "interruption_context.h"

bool TInterruptionContext::InterruptTimestampClockIsMonotonic = false;

TInterruptionContext::TInterruptionContext(int count, struct epoll_event* events)
    : Count(count),
      Events(events),
      Diff(std::chrono::nanoseconds::zero())
{
    if (!InterruptTimestampClockIsMonotonic) {
        Diff = std::chrono::nanoseconds(std::chrono::steady_clock::now().time_since_epoch() -
                                        std::chrono::system_clock::now().time_since_epoch());
    }
}

std::chrono::steady_clock::time_point TInterruptionContext::ToSteadyClock(__u64 timestamp) const
{
    return std::chrono::steady_clock::time_point(std::chrono::nanoseconds(timestamp) + Diff);
}

void TInterruptionContext::SetMonotonicClockForInterruptTimestamp()
{
    InterruptTimestampClockIsMonotonic = true;
}
