#include "config.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "utils.h"
#include "log.h"
#include "exceptions.h"

#include <wblib/utils.h>
#include <jsoncpp/json/json.h>

#include <iostream>
#include <fstream>
#include <cassert>
#include <string.h>
#include <algorithm>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;

void ReadLine(const Json::Value& line, TGpioLineConfig& lineConfig)
{
    if (!line.isMember("name")) {
        wb_throw(TGpioDriverException, "'name' is required field");
    }
    lineConfig.Name = line["name"].asString();

    if (line.isMember("inverted"))
        lineConfig.IsActiveLow = line["inverted"].asBool();
    if (line.isMember("open_drain"))
        lineConfig.IsOpenDrain = line["open_drain"].asBool();
    if (line.isMember("open_source"))
        lineConfig.IsOpenSource = line["open_source"].asBool();
    if (line.isMember("direction") && line["direction"].asString() == "input")
        lineConfig.Direction = EGpioDirection::Input;
    if (line.isMember("type"))
        lineConfig.Type = line["type"].asString();
    if (line.isMember("multiplier"))
        lineConfig.Multiplier = line["multiplier"].asFloat();
    if (line.isMember("edge"))
        EnumerateGpioEdge(line["edge"].asString(), lineConfig.InterruptEdge);
    if (line.isMember("decimal_points_current"))
        lineConfig.DecimalPlacesCurrent = line["decimal_points_current"].asInt();
    if (line.isMember("decimal_points_total"))
        lineConfig.DecimalPlacesTotal = line["decimal_points_total"].asInt();

    lineConfig.InitialState = line.get("initial_state", false).asBool();
}

TGpioDriverConfig::TGpioDriverConfig(const string &fileName)
{
    Json::Value root;
    { // read and parse file
        Json::Reader reader;
        ifstream file(fileName);

        if (!file.is_open()) {
            wb_throw(TGpioDriverException, "unable to open config file at '" + fileName + "': " + strerror(errno));
        }

        if (!reader.parse(file, root, false))
        {
            // Report failures and their locations
            // in the document.
            wb_throw(TGpioDriverException, "failed to parse JSON \n" + reader.getFormattedErrorMessages());
        }
    }

    try {

        if (!root.isMember("device_name")) {
            wb_throw(TGpioDriverException, "'device_name' is required field");
        }

        if (!root.isMember("channels")) {
            wb_throw(TGpioDriverException, "'channels' is required field");
        }

        const auto &channels = root["channels"];
        DeviceName = root["device_name"].asString();

        if (!channels.isArray()) {
            wb_throw(TGpioDriverException, "'channels' must be array");
        }

        for (const auto & channel : channels)
        {
            if (!channel.isMember("gpio")) {
                wb_throw(TGpioDriverException, "'gpio' is required field");
            }

            TGpioLineConfig lineConfig;
            string path;
            if (channel["gpio"].isUInt()) {
                uint32_t gpioNumber = channel["gpio"].asUInt();
                if (gpioNumber == 0) {
                        wb_throw(TGpioDriverException, "'gpio' must be greater than zero");
                }
                uint32_t chipNumber;
                try {
                    tie(chipNumber, lineConfig.Offset) = FromSysfsGpio(gpioNumber);
                } catch (const TGpioDriverException & e) {
                    LOG(Error) << "Skipping GPIO " << gpioNumber << " reason: " << e.what();
                    continue;
                }
                path = GpioChipNumberToPath(chipNumber);
            } else {
                if (!channel["gpio"].isObject()) {
                    wb_throw(TGpioDriverException, "'gpio' must be number or object");
                }
                if (!channel["gpio"].isMember("offset")) {
                    wb_throw(TGpioDriverException, "'offset' is required field");
                }
                if (!channel["gpio"].isMember("chip")) {
                    wb_throw(TGpioDriverException, "'chip' is required field");
                }
                lineConfig.Offset = channel["gpio"]["offset"].asUInt();
                path = channel["gpio"]["chip"].asString();
            }

            auto chipConfig = find_if(Chips.begin(), Chips.end(), [&](const auto& c) { return c.Path == path; });
            if(chipConfig == Chips.end()) {
                Chips.emplace_back(path);
                chipConfig = Chips.end();
                --chipConfig;
            }

            ReadLine(channel, lineConfig);

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
    } catch (const exception & e) {
        wb_throw(TGpioDriverException, string("malformed JSON config: ") + e.what());
    }
}

TGpioDriverConfig GetConvertConfig(const std::string & fileName)
{
    WB_SCOPE_NO_THROW_EXIT( LOG(Info) << "Read config ok"; )

    try {
        return TGpioDriverConfig(fileName);
    } catch (const exception & e) {
        LOG(Error) << "Unable to read config: '" << e.what() << "'";
        wb_throw(TGpioDriverException, "unable to read config");
    }
}
