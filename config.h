#pragma once

#include <set>
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
    std::string Path;

    explicit THandlerConfig(const std::string & fileName);

    void AddGpio(TGpioDesc &gpio_desc);
};
