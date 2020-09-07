#pragma once

#include "types.h"

#include <vector>

enum class EGpioDirection
{
    Input,
    Output
};

struct TGpioLineConfig
{
    uint32_t       Offset;
    bool           IsOpenDrain  = false;
    bool           IsOpenSource = false;
    bool           IsActiveLow  = false;
    std::string    Name;
    EGpioDirection Direction     = EGpioDirection::Output;
    EGpioEdge      InterruptEdge = EGpioEdge::BOTH;
    std::string    Type;
    float          Multiplier           = 1.0;
    int            DecimalPlacesTotal   = -1;
    int            DecimalPlacesCurrent = -1;
    bool           InitialState         = false;
};

using TLinesConfig = std::vector<TGpioLineConfig>;

struct TGpioChipConfig
{
    std::string  Path;
    TLinesConfig Lines;

    TGpioChipConfig(const std::string& path) : Path(path) {}
};

struct TGpioDriverConfig
{
    std::string                  DeviceName;
    std::vector<TGpioChipConfig> Chips;
};

TGpioDriverConfig LoadConfig(const std::string& mainConfigFile,
                             const std::string& optionalConfigFile,
                             const std::string& schemaFile);