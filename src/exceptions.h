#pragma once

#include <wblib/exceptions.h>

class TGpioDriverException: public WBMQTT::TBaseException
{
public:
    TGpioDriverException(const char* file, int line, const std::string& message);
    ~TGpioDriverException() noexcept = default;
};
