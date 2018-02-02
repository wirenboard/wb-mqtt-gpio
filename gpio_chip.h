#pragma once

#include "config.h"
#include "declarations.h"

#include <wblib/driver.h>

#include <linux/gpio.h>

class TGpioChip: public std::enable_shared_from_this<TGpioChip>
{
    friend TGpioLine;

    int                         Fd,
                                RequestFd;
    std::string                 Name,
                                Label;
    std::vector<PGpioChipLine>  Lines;
    std::vector<int>            LinesValues;
public:
    TGpioChip(const WBMQTT::PDeviceDriver & mqttDriver, const THandlerConfig & handlerConfig);
    ~TGpioChip();

    void LoadLines();
    void UpdateLinesValues();

    const std::string & GetName() const;
    const std::string & GetLabel() const;
    uint32_t GetLineCount() const;

private:
    int GetFd() const;
};
