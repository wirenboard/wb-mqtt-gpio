#pragma once

#include <set>
#include <unordered_map>
#include <string>
#include <vector>

enum class TGpioDirection {
    Input,
    Output
};

struct TGpioDesc {
    int Gpio;
    bool Inverted = false;
    std::string Name = "";
    TGpioDirection Direction = TGpioDirection::Output;
    std::string InterruptEdge = "";
    std::string Type = "";
    float Multiplier;
    int Order;
    int DecimalPlacesTotal = -1;
    int DecimalPlacesCurrent = -1;
    bool InitialState = false;
};

class THandlerConfig
{
public:
    std::set<std::string> Names;
    std::set<int> GpioNums;
    std::vector<TGpioDesc> Gpios;
    std::string DeviceName;

    explicit THandlerConfig(const std::string & fileName);

    void AddGpio(TGpioDesc &gpio_desc);
};

enum class EGpioEdge { RISING, FALLING, BOTH };

void EnumerateGpioEdge(const std::string &, EGpioEdge &);
std::string GpioEdgeToString(EGpioEdge);

struct TGpioLineConfig
{
    uint32_t Offset;
    bool IsOpenDrain          = false;
    bool IsOpenSource         = false;
    bool IsActiveLow          = false;
    std::string Name          = "";
    TGpioDirection Direction  = TGpioDirection::Output;
    EGpioEdge InterruptEdge   = EGpioEdge::BOTH;
    std::string Type          = "";
    float Multiplier;
    int Order;
    int DecimalPlacesTotal    = -1;
    int DecimalPlacesCurrent  = -1;
    bool InitialState         = false;
};

using TLinesConfig = std::unordered_map<uint32_t, TGpioLineConfig>;

struct TGpioChipConfig
{
    std::string  Path;
    TLinesConfig Lines;
};

struct TGpioDriverConfig
{
    std::string DeviceName;
    std::unordered_map<std::string, TGpioChipConfig> Chips;

    TGpioDriverConfig() = default;
    explicit TGpioDriverConfig(const std::string & fileName);
};

TGpioDriverConfig GetConvertConfig(const std::string & fileName);
