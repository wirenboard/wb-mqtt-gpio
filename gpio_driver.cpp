#include "gpio_driver.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "exceptions.h"
#include "config.h"
#include "log.h"

#include <wblib/wbmqtt.h>

#define LOG(logger) ::logger.Log() << "[gpio driver] "

using namespace std;
using namespace WBMQTT;

const char * const TGpioDriver::Name = "wb-gpio";

TGpioDriver::TGpioDriver(const WBMQTT::PDeviceDriver & mqttDriver, const string & configFileName)
{
    try {
        auto config = GetConvertConfig(configFileName);
        auto tx = mqttDriver->BeginTx();
        auto device = tx->CreateDevice(TLocalDeviceArgs{}
            .SetId(Name)
            .SetTitle(config.DeviceName)
        ).GetValue();

        for (const auto & chipConfig: config.Chips) {
            Chips.push_back(make_shared<TGpioChip>(chipConfig.Path));
            const auto & chip = Chips.back();
            chip->LoadLines(chipConfig.Lines);

            size_t lineNumber = 0;
            for (const auto & offsetLineConfig: chipConfig.Lines) {
                const auto & lineConfig = offsetLineConfig.second;
                const auto & futureControl = device->CreateControl(tx, TControlArgs{}
                    .SetId(lineConfig.Name)
                    .SetType(lineConfig.Type)
                    .SetOrder(lineConfig.Order)
                    .SetReadonly(lineConfig.Direction == TGpioDirection::Input)
                    .SetUserData(chip->GetLine(lineConfig.Offset))
                );

                ++lineNumber;

                if (lineNumber == chipConfig.Lines.size()) {
                    futureControl.Wait();   // wait for last control
                }
            }
        }

    } catch (const exception & e) {
        LOG(Error) << "unable to create GPIO driver: " << e.what();
        throw;
    }

    mqttDriver->On<TControlOnValueEvent>([](const TControlOnValueEvent & event){
        event.Control->GetUserData().As<PGpioLine>()->SetValue();
    });
}
