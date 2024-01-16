#pragma once

#include "declarations.h"

#include <wblib/declarations.h>
#include <wblib/promise.h>

#include <mutex>
#include <vector>

class TGpioDriver
{
    WBMQTT::PDeviceDriver MqttDriver;
    WBMQTT::PDriverEventHandlerHandle EventHandlerHandle;

    std::vector<PGpioChipDriver> ChipDrivers;
    std::unique_ptr<std::thread> Worker;

    bool Active;
    std::mutex ActiveMutex;

public:
    static const char* const Name;

    TGpioDriver(const WBMQTT::PDeviceDriver& mqttDriver, const TGpioDriverConfig& config);
    ~TGpioDriver();

    void Start();
    void Stop();
    void Clear() noexcept;
};

WBMQTT::TFuture<WBMQTT::PControl> CreateOutputControl(WBMQTT::PLocalDevice device,
                                                      const WBMQTT::PDriverTx& tx,
                                                      PGpioLine line,
                                                      const TGpioLineConfig& lineConfig,
                                                      std::function<void(uint8_t)> setLineValueFn,
                                                      const std::string& error = "");
