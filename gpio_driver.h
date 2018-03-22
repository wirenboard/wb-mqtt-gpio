#pragma once

#include "declarations.h"

#include <wblib/declarations.h>

#include <vector>


class TGpioDriver
{
    std::vector<PGpioChip>          Chips;
    std::unique_ptr<std::thread>    Worker;
    std::atomic_bool                Active;

public:
    static const char * const Name;

    TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const TGpioDriverConfig & config);
    ~TGpioDriver() = default;

    void Start();
    void Stop();

private:
    void Poll();
};
