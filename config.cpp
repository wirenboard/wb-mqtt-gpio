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
    const string ProtectedProperties[] = {"gpio", "direction", "inverted", "open_drain", "open_source"};

    void AppendLine(TGpioDriverConfig&     cfg,
                    const std::string&     gpioChipPath,
                    const TGpioLineConfig& line)
    {
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
                     chipConfig->Path + "' defined as '" + line.Name +
                     "'. It is already defined as '" + itLine->Name + "'. To override set similar MQTT id (name).");
        }

        chipConfig->Lines.push_back(line);
    }

    TGpioDriverConfig LoadFromJSON(const Json::Value& root)
    {
        TGpioDriverConfig cfg;
        const auto&       channels = root["channels"];
        if (root.isMember("device_name")) {
            cfg.DeviceName = root["device_name"].asString();
        }

        int32_t maxUnchangedInterval = -1;
        Get(root, "max_unchanged_interval", maxUnchangedInterval);
        cfg.PublishParameters.Set(maxUnchangedInterval);

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
            Get(channel, "debounce", lineConfig.DebounceTimeout);

            if (channel.isMember("direction") && channel["direction"].asString() == "input")
                lineConfig.Direction = EGpioDirection::Input;

            if (channel.isMember("edge")) {
                if (lineConfig.Type.empty()) {
                    LOG(Warn) << "Edge setting for GPIO \"" << lineConfig.Name << "\" is not used. It can be set only for GPIO with \"type\" option";
                } else {
                    EnumerateGpioEdge(channel["edge"].asString(), lineConfig.InterruptEdge);
                }
            }

            AppendLine(cfg, path, lineConfig);
        }
        return cfg;
    }

    Json::Value RemoveDeviceNameRequirement(const Json::Value& schema)
    {
        auto res = schema;
        Json::Value newArray = Json::arrayValue;
        for (auto& v : schema["required"]) {
            if (v.asString() != "device_name") {
                newArray.append(v);
            }
        }
        res["required"] = newArray;
        return res;
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

        if (!optionalConfigFile.empty()) {
            auto cfg = Parse(optionalConfigFile);
            Validate(cfg, schema);
            return LoadFromJSON(cfg);
        }

        TMergeParams mergeParams;
        mergeParams.LogPrefix = "[config] ";
        mergeParams.InfoLogger = &Info;
        mergeParams.WarnLogger = &Warn;
        mergeParams.MergeArraysOn["/channels"] = "name";

        Json::Value resultingConfig;
        resultingConfig["channels"] = Json::Value(Json::arrayValue);

        Json::Value noDeviceNameSchema = RemoveDeviceNameRequirement(schema);
        try {
            IterateDirByPattern(systemConfigsDir, ".conf", [&](const string& f) {
                auto cfg = Parse(f);
                Validate(cfg, noDeviceNameSchema);
                Merge(resultingConfig, cfg, mergeParams);
                return false;
            });
        } catch (const TNoDirError&) {
        }
        {
            for (const auto& pr: ProtectedProperties) {
                mergeParams.ProtectedParameters.insert("/channels/" + pr);
            }
            auto cfg = Parse(mainConfigFile);
            Validate(cfg, schema);
            Merge(resultingConfig, cfg, mergeParams);
        }
        return LoadFromJSON(resultingConfig);
    }
} // namespace

TGpioDriverConfig LoadConfig(const std::string& mainConfigFile,
                             const std::string& optionalConfigFile,
                             const std::string& systemConfigsDir,
                             const std::string& schemaFile,
                             const TConfigValidationHints& validationHints)
{
    TGpioDriverConfig cfg(
        LoadConfigInternal(mainConfigFile, optionalConfigFile, systemConfigsDir, schemaFile));
    RemoveUnusedChips(cfg);
    if (validationHints.WarnAboutCountersWithInvertedInput) {
        for (const auto& chip: cfg.Chips) {
            for (const auto& line: chip.Lines) {
                if (!line.Type.empty() && line.IsActiveLow) {
                    LOG(Warn) << line.Name << "(" << chip.Path << ":" << to_string(line.Offset)
                              << ") is used as counter and has inverted option. "
                              << "Impulse counting could be wrong because of a kernel bug. It is recommended to upgrade kernel to v5.3 or newer";
                }
            }
        }
    }
    return cfg;
}

void MakeJsonForConfed(const string& configFile,
                       const string& systemConfigsDir,
                       const string& schemaFile)
{
    Json::Value schema = Parse(schemaFile);
    Json::Value noDeviceNameSchema = RemoveDeviceNameRequirement(schema);
    auto config = Parse(configFile);
    Validate(config, schema);
    unordered_map<string, Json::Value> configuredChannels;
    for (const auto& ch: config["channels"]) {
        configuredChannels.emplace(ch["name"].asString(), ch);
    }
    Json::Value newChannels(Json::arrayValue);
    try {
        IterateDirByPattern(systemConfigsDir, ".conf", [&](const string& f) {
            auto cfg = Parse(f);
            Validate(cfg, noDeviceNameSchema);
            for (const auto& ch: cfg["channels"]) {
                auto name = ch["name"].asString();
                auto it = configuredChannels.find(name);
                if (it != configuredChannels.end()) {
                    newChannels.append(it->second);
                    configuredChannels.erase(name);
                } else {
                    Json::Value v;
                    v["name"] = ch["name"];
                    v["direction"] = ch["direction"];
                    newChannels.append(v);
                }
            }
            return false;
        });
    } catch (const TNoDirError&) {
    }
    for (const auto& ch: config["channels"]) {
        auto it = configuredChannels.find(ch["name"].asString());
        if (it != configuredChannels.end()) {
            newChannels.append(ch);
        }
    }
    config["channels"].swap(newChannels);
    MakeWriter("", "None")->write(config, &cout);
}

void MakeConfigFromConfed(const string& systemConfigsDir, const string& schemaFile)
{
    Json::Value noDeviceNameSchema = RemoveDeviceNameRequirement(Parse(schemaFile));
    unordered_set<string> systemChannels;
    try {
        IterateDirByPattern(systemConfigsDir, ".conf", [&](const string& f) {
            auto cfg = Parse(f);
            Validate(cfg, noDeviceNameSchema);
            for (const auto& ch: cfg["channels"]) {
                systemChannels.insert(ch["name"].asString());
            }
            return false;
        });
    } catch (const TNoDirError&) {
    }

    Json::Value config;
    Json::CharReaderBuilder readerBuilder;
    Json::String errs;

    if (!Json::parseFromStream(readerBuilder, cin, &config, &errs)) {
        throw runtime_error("Failed to parse JSON:" + errs);
    }

    Json::Value newChannels(Json::arrayValue);
    for (auto& ch: config["channels"]) {
        auto it = systemChannels.find(ch["name"].asString());
        if (it != systemChannels.end()) {
            for (const auto& pr: ProtectedProperties) {
                ch.removeMember(pr);
            }
            if (ch.size() > 1) {
                newChannels.append(ch);
            }
        } else {
            newChannels.append(ch);
        }
    }
    config["channels"].swap(newChannels);
    MakeWriter("  ", "None")->write(config, &cout);
}
