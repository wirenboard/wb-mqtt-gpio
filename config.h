#pragma once

#include "types.h"

#include <set>
#include <unordered_map>
#include <string>
#include <vector>

enum class EGpioDirection {
    Input,
    Output
};

struct TGpioLineConfig
{
    uint32_t Offset;
    bool IsOpenDrain          = false;
    bool IsOpenSource         = false;
    bool IsActiveLow          = false;
    std::string Name          = "";
    EGpioDirection Direction  = EGpioDirection::Output;
    EGpioEdge InterruptEdge   = EGpioEdge::BOTH;
    std::string Type          = "";
    float Multiplier;
    int DecimalPlacesTotal    = -1;
    int DecimalPlacesCurrent  = -1;
    bool InitialState         = false;
};

using TLinesConfig = std::vector<TGpioLineConfig>;

struct TGpioChipConfig
{
    std::string  Path;
    TLinesConfig Lines;

    TGpioChipConfig(const std::string & path): Path(path)
    {}
};

struct TGpioDriverConfig
{
    std::string DeviceName;
    std::vector<TGpioChipConfig> Chips;

    TGpioDriverConfig() = default;
    explicit TGpioDriverConfig(const std::string & fileName);
};

TGpioDriverConfig GetConvertConfig(const std::string & fileName);
