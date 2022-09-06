#pragma once

#include "declarations.h"

#include <wblib/declarations.h>

#include <vector>
#include <mutex>


class TGpioDriver
{
    WBMQTT::PDeviceDriver               MqttDriver;
    WBMQTT::PDriverEventHandlerHandle   EventHandlerHandle;

    std::vector<PGpioChipDriver>    ChipDrivers;
    std::unique_ptr<std::thread>    Worker,
                                    DebouncePoll;
    bool                            Active;
    std::mutex                      ActiveMutex;

public:
    static const char * const Name;

    TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const TGpioDriverConfig & config);
    ~TGpioDriver();

    void Start();
    void Stop();
    void Clear() noexcept;
};
