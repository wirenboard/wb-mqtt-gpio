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
#include <fstream>
#include <iostream>
#include <unordered_set>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;
using namespace WBMQTT::JSON;

namespace
{
    ostream& operator<<(ostream& ss, const TGpioLineConfig& line)
    {
        ss << "    Offset: " << line.Offset << endl
           << "    IsOpenDrain: " << line.IsOpenDrain << endl
           << "    IsOpenSource: " << line.IsOpenSource << endl
           << "    IsActiveLow: " << line.IsActiveLow << endl
           << "    Direction: " << (line.Direction == EGpioDirection::Input ? "input" : "output")
           << endl
           << "    InterruptEdge: " << GpioEdgeToString(line.InterruptEdge) << endl
           << "    Type: " << line.Type << endl
           << "    Multiplier: " << line.Multiplier << endl
           << "    DecimalPlacesTotal: " << line.DecimalPlacesTotal << endl
           << "    DecimalPlacesCurrent: " << line.DecimalPlacesCurrent << endl
           << "    InitialState: " << line.InitialState << endl;
        return ss;
    }

    void RemoveDuplicateLine(TGpioDriverConfig&     cfg,
                             const std::string&     gpioChipPath,
                             const TGpioLineConfig& line,
                             unordered_set<string>& lineNames)
    {
        if (!lineNames.count(line.Name)) {
            return;
        }
        for (auto& chip : cfg.Chips) {
            auto itLine = find_if(chip.Lines.begin(), chip.Lines.end(), [&](const auto& l) {
                return l.Name == line.Name;
            });
            if (itLine != chip.Lines.end()) {
                stringstream ss;
                ss << "duplicate GPIO name : '" << line.Name << "'" << endl
                   << "  old settings:" << endl
                   << "    Chip: " << chip.Path << endl
                   << (*itLine) << "  new settings:" << endl
                   << "    Chip: " << gpioChipPath << endl
                   << line;

                LOG(Warn) << ss.str();
                chip.Lines.erase(itLine);
                return;
            }
        }
    }

    void AppendLine(TGpioDriverConfig&     cfg,
                    const std::string&     gpioChipPath,
                    const TGpioLineConfig& line,
                    unordered_set<string>& lineNames)
    {
        RemoveDuplicateLine(cfg, gpioChipPath, line, lineNames);

        auto chipConfig = find_if(cfg.Chips.begin(), cfg.Chips.end(), [&](const auto& c) {
            return c.Path == gpioChipPath;
        });
        if (chipConfig == cfg.Chips.end()) {
            cfg.Chips.emplace_back(gpioChipPath);
            chipConfig = cfg.Chips.end();
            --chipConfig;
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
        lineNames.emplace(line.Name);
    }

    TGpioDriverConfig LoadFromJSON(const Json::Value&     root,
                                   const Json::Value&     schema,
                                   unordered_set<string>& lineNames)
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

            AppendLine(cfg, path, lineConfig, lineNames);
        }
        return cfg;
    }

    void RemoveDeviceNameRequirement(Json::Value& schema)
    {
        Json::Value newArray = Json::arrayValue;
        for (auto& v : schema["required"]) {
            if (v.asString() != "device_name") {
                newArray.append(v);
            }
        }
        schema["required"] = newArray;
    }

    void Append(const TGpioDriverConfig& src,
                TGpioDriverConfig&       dst,
                unordered_set<string>&   lineNames)
    {
        dst.DeviceName = src.DeviceName;
        for (const auto& v : src.Chips) {
            for (const auto& line : v.Lines) {
                AppendLine(dst, v.Path, line, lineNames);
            }
        }
    }

    template <class T, class Pred> void erase_if(T& c, Pred pred)
    {
        c.erase(std::remove_if(c.begin(), c.end(), pred), c.end());
    }

    void RemoveUnusedChips(TGpioDriverConfig& cfg)
    {
        erase_if(cfg.Chips, [](const auto& c) { return c.Lines.empty(); });
    }

    TGpioDriverConfig LoadConfigInternal(const std::string& mainConfigFile,
                                         const std::string& optionalConfigFile,
                                         const std::string& systemConfigsDir,
                                         const std::string& schemaFile)
    {
        Json::Value schema = Parse(schemaFile);

        unordered_set<string> lineNames;

        if (!optionalConfigFile.empty())
            return LoadFromJSON(Parse(optionalConfigFile), schema, lineNames);

        Json::Value noDeviceNameSchema = schema;
        RemoveDeviceNameRequirement(noDeviceNameSchema);
        TGpioDriverConfig cfg;
        try {
            IterateDirByPattern(systemConfigsDir, ".conf", [&](const string& f) {
                Append(LoadFromJSON(Parse(f), noDeviceNameSchema, lineNames), cfg, lineNames);
                return false;
            });
        } catch (const TNoDirError&) {
        }
        Append(LoadFromJSON(Parse(mainConfigFile), schema, lineNames), cfg, lineNames);
        return cfg;
    }
} // namespace

TGpioDriverConfig LoadConfig(const std::string& mainConfigFile,
                             const std::string& optionalConfigFile,
                             const std::string& systemConfigsDir,
                             const std::string& schemaFile)
{
    TGpioDriverConfig cfg(
        LoadConfigInternal(mainConfigFile, optionalConfigFile, systemConfigsDir, schemaFile));
    RemoveUnusedChips(cfg);
    return cfg;
}