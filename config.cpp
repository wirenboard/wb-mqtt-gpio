#include "config.h"
#include "exceptions.h"
#include "file_utils.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "log.h"
#include "utils.h"

#include <wblib/json_utils.h>
#include <wblib/utils.h>

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string.h>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;
using namespace WBMQTT::JSON;

namespace
{
    void AppendLine(TGpioDriverConfig& cfg, const std::string& path, const TGpioLineConfig& line)
    {
        auto chipConfig = find_if(cfg.Chips.begin(), cfg.Chips.end(), [&](const auto& c) {
            return c.Path == path;
        });
        if (chipConfig == cfg.Chips.end()) {
            cfg.Chips.emplace_back(path);
            chipConfig = cfg.Chips.end();
            --chipConfig;
        }

        auto itName = find_if(chipConfig->Lines.begin(), chipConfig->Lines.end(), [&](const auto& l) {
            return l.Name == line.Name;
        });
        if (itName != chipConfig->Lines.end()) {
            wb_throw(TGpioDriverException,
                     "duplicate GPIO name in config: '" + line.Name + "' at chip '" +
                         chipConfig->Path + "'");
        }

        auto itLine = find_if(chipConfig->Lines.begin(), chipConfig->Lines.end(), [&](const auto& l) {
            return l.Offset == line.Offset;
        });
        if (itLine != chipConfig->Lines.end()) {
            wb_throw(TGpioDriverException,
                     "duplicate GPIO offset in config: '" + to_string(line.Offset) + "' at chip '" +
                         chipConfig->Path + "'");
        }

        chipConfig->Lines.push_back(line);
    }

    TGpioDriverConfig LoadFromJSON(const Json::Value& root, const Json::Value& schema)
    {
        Validate(root, schema);

        TGpioDriverConfig cfg;
        const auto&       channels = root["channels"];
        if (root.isMember("device_name")) {
            cfg.DeviceName = root["device_name"].asString();
        }

        for (const auto& channel : channels) {
            TGpioLineConfig lineConfig;
            string          path;
            if (channel["gpio"].isUInt()) {
                uint32_t gpioNumber = channel["gpio"].asUInt();
                uint32_t chipNumber;
                try {
                    tie(chipNumber, lineConfig.Offset) = FromSysfsGpio(gpioNumber);
                } catch (const TGpioDriverException& e) {
                    LOG(Error) << "Skipping GPIO " << gpioNumber << " reason: " << e.what();
                    continue;
                }
                path = GpioChipNumberToPath(chipNumber);
            } else {
                lineConfig.Offset = channel["gpio"]["offset"].asUInt();
                path              = channel["gpio"]["chip"].asString();
            }

            lineConfig.Name = channel["name"].asString();

            Get(channel, "inverted", lineConfig.IsActiveLow);
            Get(channel, "open_drain", lineConfig.IsOpenDrain);
            Get(channel, "open_source", lineConfig.IsOpenSource);
            Get(channel, "type", lineConfig.Type);
            Get(channel, "multiplier", lineConfig.Multiplier);
            Get(channel, "decimal_points_current", lineConfig.DecimalPlacesCurrent);
            Get(channel, "decimal_points_total", lineConfig.DecimalPlacesTotal);
            Get(channel, "initial_state", lineConfig.InitialState);

            if (channel.isMember("direction") && channel["direction"].asString() == "input")
                lineConfig.Direction = EGpioDirection::Input;

            if (channel.isMember("edge"))
                EnumerateGpioEdge(channel["edge"].asString(), lineConfig.InterruptEdge);

            AppendLine(cfg, path, lineConfig);
        }
        return cfg;
    }

    void RemoveDeviceNameRequirement(Json::Value& shema)
    {
        Json::Value newArray = Json::arrayValue;
        for (auto& v : shema["required"]) {
            if (v.asString() != "device_name") {
                newArray.append(v);
            }
        }
        shema["required"] = newArray;
    }

    void Append(const TGpioDriverConfig& src, TGpioDriverConfig& dst)
    {
        dst.DeviceName = src.DeviceName;
        for (const auto& v : src.Chips) {
            for (const auto& line : v.Lines) {
                AppendLine(dst, v.Path, line);
            }
        }
    }
} // namespace

TGpioDriverConfig LoadConfig(const string& mainConfigFile,
                             const string& optionalConfigFile,
                             const string& shemaFile)
{
    Json::Value shema = Parse(shemaFile);

    if (!optionalConfigFile.empty())
        return LoadFromJSON(Parse(optionalConfigFile), shema);

    Json::Value noDeviceNameShema = shema;
    RemoveDeviceNameRequirement(noDeviceNameShema);
    TGpioDriverConfig cfg;
    try {
        IterateDirByPattern(mainConfigFile + ".d", ".conf", [&](const string& f) {
            Append(LoadFromJSON(Parse(f), noDeviceNameShema), cfg);
            return false;
        });
    } catch (const TNoDirError&) {
    }
    Append(LoadFromJSON(Parse(mainConfigFile), shema), cfg);
    return cfg;
}