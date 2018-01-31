#pragma once

#include "config.h"

#include <wblib/driver.h>

class TMqttGpioDriver
{
public:
    TMqttGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const THandlerConfig & handler_config);
    ~TMqttGpioDriver();
};
