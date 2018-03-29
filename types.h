#pragma once

#include <string>

enum class EGpioEdge: uint8_t
{
    RISING,
    FALLING,
    BOTH
};

void EnumerateGpioEdge(const std::string &, EGpioEdge &);
std::string GpioEdgeToString(EGpioEdge);

enum class EInterruptSupport: uint8_t {
    UNKNOWN,
    YES,
    NO
};

template <typename T>
class TValue
{
    T    Value;
    bool Changed;

public:
    TValue()
        : Value(TValue())
        , Changed(false)
    {}

    TValue(T value)
        : Value(value)
        , Changed(false)
    {}

    void Set(T value)
    {
        Changed |= Value != value;
        Value = value;
    }

    T Get() const           { return Value; }
    bool IsChanged() const  { return Changed; }
    void ResetChanged()     { Changed = false; }
};
