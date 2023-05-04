#pragma once

#include <string>

enum class EGpioEdge : uint8_t
{
    RISING,
    FALLING,
    BOTH
};

void EnumerateGpioEdge(const std::string&, EGpioEdge&);
std::string GpioEdgeToString(EGpioEdge);

enum class EInterruptSupport : uint8_t
{
    UNKNOWN,
    YES,
    NO
};

enum class EInterruptStatus : uint8_t
{
    SKIP,
    Handled
};

template<typename T> class TValue
{
    T Value;

public:
    TValue(): Value(TValue())
    {}

    TValue(T value): Value(value)
    {}

    void Set(T value)
    {
        Value = value;
    }

    T Get() const
    {
        return Value;
    }
};
