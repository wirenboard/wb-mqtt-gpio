#include <wblib/testing/testlog.h>
#include "gpio_chip_driver.h"
#include "gpio_line.h"

using WBMQTT::Testing::TLoggedFixture;

class TFakeGpioChipDriverHandler: public IGpioChipDriverHandler
{
private:
    TLoggedFixture* logger;

public:
    TFakeGpioChipDriverHandler(TLoggedFixture* l): logger(l) {}

    void UnexportSysFs(const PGpioLine & line) override
    {
        logger->Emit() << "unexporting line " << line->Describe();
    }
};


class TFakeControlLine: public IControlLine
{
private:
    TLoggedFixture* logger;

public:
    TFakeControlLine(TLoggedFixture* l): logger(l) {}

    int control(int fd, int request_code, char* p) override
    {
        logger->Emit() << fd << " " << request_code;
        return 0;
    }
};