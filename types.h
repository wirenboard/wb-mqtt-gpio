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
