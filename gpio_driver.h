#pragma once

#include "declarations.h"

#include <wblib/declarations.h>

#include <vector>


class TGpioDriver
{
    std::vector<PGpioChip> Chips;
public:
    static const char * const Name;

    TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const std::string & configFileName);
    ~TGpioDriver() = default;

private:

};
