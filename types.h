#pragma once

#include <string>

enum class EGpioEdge { RISING, FALLING, BOTH };

void EnumerateGpioEdge(const std::string &, EGpioEdge &);
std::string GpioEdgeToString(EGpioEdge);
