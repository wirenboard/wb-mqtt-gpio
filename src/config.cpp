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
#include <unordered_map>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;
using namespace WBMQTT::JSON;

namespace
{
    const string ProtectedProperties[] = {"gpio", "direction", "inverted", "open_drain", "open_source"};

    void AppendLine(TGpioDriverConfig& cfg, const std::string& gpioChipPath, const TGpioLineConfig& line)
    {
        auto chipConfig =
            find_if(cfg.Chips.begin(), cfg.Chips.end(), [&](const auto& c) { return c.Path == gpioChipPath; });
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
                     "duplicate GPIO offset in config: '" + to_string(line.Offset) + "' at chip '" + chipConfig->Path +
                         "' defined as '" + line.Name + "'. It is already defined as '" + itLine->Name +
                         "'. To override set similar MQTT id (name).");
        }

        chipConfig->Lines.push_back(line);
    }

    TGpioDriverConfig LoadFromJSON(const Json::Value& root)
    {
        TGpioDriverConfig cfg;
        const auto& channels = root["channels"];

        cfg.Debug = root.isMember("debug") && root["debug"].asBool();

        if (root.isMember("device_name")) {
            cfg.DeviceName = root["device_name"].asString();
        }

        int32_t maxUnchangedInterval = -1;
        Get(root, "max_unchanged_interval", maxUnchangedInterval);
        cfg.PublishParameters.Set(maxUnchangedInterval);

        Get(root, "publish_period_ms", cfg.PublishPeriod);

        for (const auto& channel: channels) {
            if (!channel.isMember("gpio")) {
                LOG(Warn) << "Skip GPIO \"" << channel["name"].asString()
                          << "\", it is unavailable or badly configured";
                continue;
            }
            TGpioLineConfig lineConfig;
            string path;
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
                path = channel["gpio"]["chip"].asString();
            }

            lineConfig.Name = channel["name"].asString();

            Get(channel, "inverted", lineConfig.IsActiveLow);
            Get(channel, "open_drain", lineConfig.IsOpenDrain);
            Get(channel, "open_source", lineConfig.IsOpenSource);
            Get(channel, "type", lineConfig.Type);
            Get(channel, "title", lineConfig.Title);
            Get(channel, "title_total", lineConfig.TitleTotal);
            Get(channel, "title_current", lineConfig.TitleCurrent);
            Get(channel, "multiplier", lineConfig.Multiplier);
            Get(channel, "decimal_points_current", lineConfig.DecimalPlacesCurrent);
            Get(channel, "decimal_points_total", lineConfig.DecimalPlacesTotal);
            Get(channel, "initial_state", lineConfig.InitialState);
            Get(channel, "load_previous_state", lineConfig.LoadPreviousState);
            Get(channel, "debounce", lineConfig.DebounceTimeout);

            if (channel.isMember("direction") && channel["direction"].asString() == "input")
                lineConfig.Direction = EGpioDirection::Input;

            if (channel.isMember("edge")) {
                if (lineConfig.Type.empty()) {
                    LOG(Warn) << "Edge setting for GPIO \"" << lineConfig.Name
                              << "\" is not used. It can be set only for GPIO with "
                                 "\"type\" option";
                } else {
                    EnumerateGpioEdge(channel["edge"].asString(), lineConfig.InterruptEdge);
                }
            }

            AppendLine(cfg, path, lineConfig);
        }
        return cfg;
    }

    // Title keys and postfixes of controls they are applied to (see TGpioCounter)
    const pair<const char*, const char*> TitleKeys[] = {{"title", ""},
                                                        {"title_total", "_total"},
                                                        {"title_current", "_current"}};

    // The title a control has when it is not set in the main config:
    // the one from system configs or the control id that is shown by homeui instead of a title
    string GetDefaultTitle(const Json::Value& channel,
                           const Json::Value& systemChannel,
                           const char* key,
                           const char* idPostfix)
    {
        if (systemChannel.isMember(key)) {
            return systemChannel[key].asString();
        }
        return channel["name"].asString() + idPostfix;
    }

    // "title" is filled for all channels. "title_total" and "title_current" are filled only for inputs,
    // as any input can be switched to a counter in confed and the fields must not appear empty
    void FillDefaultTitles(Json::Value& channel, const Json::Value& systemChannel)
    {
        const auto& direction = channel.isMember("direction") ? channel["direction"] : systemChannel["direction"];
        bool isInput = (direction.asString() == "input");
        for (const auto& [key, idPostfix]: TitleKeys) {
            bool isCounterTitle = (string(key) != "title");
            if (isCounterTitle && !isInput) {
                continue;
            }
            if (!channel.isMember(key)) {
                channel[key] = GetDefaultTitle(channel, systemChannel, key, idPostfix);
            }
        }
    }

    void RemoveDefaultTitles(Json::Value& channel, const Json::Value& systemChannel)
    {
        for (const auto& [key, idPostfix]: TitleKeys) {
            if (channel.isMember(key) && channel[key].isString() &&
                (channel[key].asString().empty() ||
                 channel[key].asString() == GetDefaultTitle(channel, systemChannel, key, idPostfix)))
            {
                channel.removeMember(key);
            }
        }
    }

    // Channels from all system configs in order of appearance
    vector<Json::Value> LoadSystemChannels(const string& systemConfigsDir, const Json::Value& noDeviceNameSchema)
    {
        vector<Json::Value> res;
        try {
            IterateDirByPattern(systemConfigsDir, ".conf", [&](const string& f) {
                auto cfg = Parse(f);
                Validate(cfg, noDeviceNameSchema);
                for (const auto& ch: cfg["channels"]) {
                    res.push_back(ch);
                }
                return false;
            });
        } catch (const TNoDirError&) {
        }
        return res;
    }

    Json::Value RemoveDeviceNameRequirement(const Json::Value& schema)
    {
        auto res = schema;
        Json::Value newArray = Json::arrayValue;
        for (auto& v: schema["required"]) {
            if (v.asString() != "device_name") {
                newArray.append(v);
            }
        }
        res["required"] = newArray;
        return res;
    }

    template<class T, class Pred> void erase_if(T& c, Pred pred)
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
    TGpioDriverConfig cfg(LoadConfigInternal(mainConfigFile, optionalConfigFile, systemConfigsDir, schemaFile));
    RemoveUnusedChips(cfg);
    if (validationHints.WarnAboutCountersWithInvertedInput) {
        for (const auto& chip: cfg.Chips) {
            for (const auto& line: chip.Lines) {
                if (!line.Type.empty() && line.IsActiveLow) {
                    LOG(Warn) << line.Name << "(" << chip.Path << ":" << to_string(line.Offset)
                              << ") is used as counter and has inverted option. "
                              << "Impulse counting could be wrong because of a kernel bug. It "
                                 "is recommended to upgrade kernel to v5.3 or newer";
                }
            }
        }
    }
    return cfg;
}

Json::Value BuildJsonForConfed(const string& configFile, const string& systemConfigsDir, const string& schemaFile)
{
    Json::Value schema = Parse(schemaFile);
    auto config = Parse(configFile);
    Validate(config, schema);
    unordered_map<string, Json::Value> configuredChannels;
    for (const auto& ch: config["channels"]) {
        configuredChannels.emplace(ch["name"].asString(), ch);
    }
    Json::Value newChannels(Json::arrayValue);
    for (const auto& ch: LoadSystemChannels(systemConfigsDir, RemoveDeviceNameRequirement(schema))) {
        auto name = ch["name"].asString();
        auto it = configuredChannels.find(name);
        Json::Value v;
        if (it != configuredChannels.end()) {
            v = it->second;
            configuredChannels.erase(name);
        } else {
            v["name"] = ch["name"];
            v["direction"] = ch["direction"];
        }
        FillDefaultTitles(v, ch);
        newChannels.append(v);
    }

    // Add custom channels.
    // They must contain "gpio" property,
    // otherwise it is a config for unavailable channel and must be skipped
    for (const auto& ch: config["channels"]) {
        auto it = configuredChannels.find(ch["name"].asString());
        if (it != configuredChannels.end() && ch.isMember("gpio")) {
            Json::Value v = ch;
            FillDefaultTitles(v, Json::Value());
            newChannels.append(v);
        }
    }
    config["channels"].swap(newChannels);
    return config;
}

Json::Value BuildConfigFromConfed(const Json::Value& confedConfig,
                                  const string& systemConfigsDir,
                                  const string& schemaFile)
{
    unordered_map<string, Json::Value> systemChannels;
    for (auto& ch: LoadSystemChannels(systemConfigsDir, RemoveDeviceNameRequirement(Parse(schemaFile)))) {
        systemChannels[ch["name"].asString()] = std::move(ch);
    }

    Json::Value config = confedConfig;
    Json::Value newChannels(Json::arrayValue);
    for (auto& ch: config["channels"]) {
        auto it = systemChannels.find(ch["name"].asString());
        if (it != systemChannels.end()) {
            RemoveDefaultTitles(ch, it->second);
            for (const auto& pr: ProtectedProperties) {
                ch.removeMember(pr);
            }
            if (ch.size() > 1) {
                newChannels.append(ch);
            }
        } else {
            RemoveDefaultTitles(ch, Json::Value());
            newChannels.append(ch);
        }
    }
    config["channels"].swap(newChannels);
    return config;
}

void MakeJsonForConfed(const string& configFile, const string& systemConfigsDir, const string& schemaFile)
{
    MakeWriter("", "None")->write(BuildJsonForConfed(configFile, systemConfigsDir, schemaFile), &cout);
}

void MakeConfigFromConfed(const string& systemConfigsDir, const string& schemaFile)
{
    Json::Value confedConfig;
    Json::CharReaderBuilder readerBuilder;
    Json::String errs;

    if (!Json::parseFromStream(readerBuilder, cin, &confedConfig, &errs)) {
        throw runtime_error("Failed to parse JSON:" + errs);
    }
    MakeWriter("  ", "None")->write(BuildConfigFromConfed(confedConfig, systemConfigsDir, schemaFile), &cout);
}
