#include "config.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "utils.h"
#include "log.h"
#include "exceptions.h"

#include <wblib/utils.h>
#include <wblib/json_utils.h>

#include <iostream>
#include <fstream>
#include <cassert>
#include <string.h>
#include <algorithm>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;
using namespace WBMQTT::JSON;

TGpioDriverConfig::TGpioDriverConfig(const string &fileName, const string& schemaFile)
{
    Json::Value root(Parse(fileName));
    Json::Value schema(Parse(schemaFile));

    Validate(root, schema);

    const auto &channels = root["channels"];
    DeviceName = root["device_name"].asString();

    for (const auto & channel : channels)
    {
        TGpioLineConfig lineConfig;
        string path;
        if (channel["gpio"].isUInt()) {
            uint32_t gpioNumber = channel["gpio"].asUInt();
            uint32_t chipNumber;
            try {
                tie(chipNumber, lineConfig.Offset) = FromSysfsGpio(gpioNumber);
            } catch (const TGpioDriverException & e) {
                LOG(Error) << "Skipping GPIO " << gpioNumber << " reason: " << e.what();
                continue;
            }
            path = GpioChipNumberToPath(chipNumber);
        } else {
            lineConfig.Offset = channel["gpio"]["offset"].asUInt();
            path = channel["gpio"]["chip"].asString();
        }

        auto chipConfig = find_if(Chips.begin(), Chips.end(), [&](const auto& c) { return c.Path == path; });
        if(chipConfig == Chips.end()) {
            Chips.emplace_back(path);
            chipConfig = Chips.end();
            --chipConfig;
        }

        lineConfig.Name = channel["name"].asString();

        Get(channel, "inverted",               lineConfig.IsActiveLow);
        Get(channel, "open_drain",             lineConfig.IsOpenDrain);
        Get(channel, "open_source",            lineConfig.IsOpenSource);
        Get(channel, "type",                   lineConfig.Type);
        Get(channel, "multiplier",             lineConfig.Multiplier);
        Get(channel, "decimal_points_current", lineConfig.DecimalPlacesCurrent);
        Get(channel, "decimal_points_total",   lineConfig.DecimalPlacesTotal);
        Get(channel, "initial_state",          lineConfig.InitialState);

        if (channel.isMember("direction") && channel["direction"].asString() == "input")
            lineConfig.Direction = EGpioDirection::Input;

        if (channel.isMember("edge"))
            EnumerateGpioEdge(channel["edge"].asString(), lineConfig.InterruptEdge);

        auto itName = find_if(chipConfig->Lines.begin(), chipConfig->Lines.end(), [&](const auto& l) { return l.Name == lineConfig.Name; });
        if (itName != chipConfig->Lines.end()) {
            wb_throw(TGpioDriverException, "duplicate GPIO name in config: '" + lineConfig.Name + "' at chip '" + chipConfig->Path + "'");
        }

        auto itLine = find_if(chipConfig->Lines.begin(), chipConfig->Lines.end(), [&](const auto& l) { return l.Offset == lineConfig.Offset; });
        if (itLine != chipConfig->Lines.end()) {
            wb_throw(TGpioDriverException, "duplicate GPIO offset in config: '" + to_string(lineConfig.Offset) + "' at chip '" + chipConfig->Path + "'");
        }

        chipConfig->Lines.push_back(move(lineConfig));
    }
}
