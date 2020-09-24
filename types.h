#pragma once


#include <string>
#include <chrono>

enum class EGpioEdge: uint8_t
{
    RISING,
    FALLING,
    BOTH
};

void EnumerateGpioEdge(const std::string &, EGpioEdge &);
std::string GpioEdgeToString(EGpioEdge);

enum class EInterruptSupport: uint8_t
{
    UNKNOWN,
    YES,
    NO
};

enum class EInterruptStatus: uint8_t
{
    SKIP,
    DEBOUNCE,
    Handled
};

#define TS() std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
template <typename T>
class TValue
{
    T    Value;
    bool Changed;
    long UpdateTimestamp;

public:
    TValue()
        : Value(TValue())
        , Changed(false)
        , UpdateTimestamp(0)
    {}

    TValue(T value)
        : Value(value)
        , Changed(false)
        , UpdateTimestamp(0)
    {}

    void Set(T value)
    {
        Changed |= Value != value;
        if (Changed)
          UpdateTimestamp = TS();
        Value = value;
    }

    T Get() const           { return Value; }
    T GetAndStampTime()     { UpdateTimestamp = TS(); return Value; }
    bool IsChanged() const  { return Changed; }
    void ResetChanged()     { Changed = false; }
    long Timestamp() const  { return UpdateTimestamp; }
    // duration in seconds
    bool IsExpired(long duration) const { return UpdateTimestamp + duration < TS(); }
};
