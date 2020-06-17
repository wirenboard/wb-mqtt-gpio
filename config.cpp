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

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;
using namespace Utils;


THandlerConfig::THandlerConfig(const std::string &fileName)
{
    if (fileName.empty())
    {
        wb_throw(TGpioDriverException, "empty filename");
    }

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

        DeviceName = root["device_name"].asString();

        // Let's extract the array contained
        // in the root object
        const auto &array = root["channels"];

        if (!array.isArray()) {
            wb_throw(TGpioDriverException, "'channels' must be array");
        }

        // Iterate over sequence elements and
        // print its values
        for (unsigned int index = 0; index < array.size(); ++index) {
            const auto & item = array[index];

            if (!item.isMember("gpio")) {
                wb_throw(TGpioDriverException, "'gpio' is required field");
            }

            if (!item.isMember("name")) {
                wb_throw(TGpioDriverException, "'name' is required field");
            }

            TGpioDesc gpio_desc;
            gpio_desc.Gpio = item["gpio"].asInt();
            gpio_desc.Name = item["name"].asString();
            if (item.isMember("inverted"))
                gpio_desc.Inverted = item["inverted"].asBool();
            if (item.isMember("direction") && item["direction"].asString() == "input")
                gpio_desc.Direction = EGpioDirection::Input;
            if (item.isMember("type"))
                gpio_desc.Type = item["type"].asString();
            if (item.isMember("multiplier"))
                gpio_desc.Multiplier = item["multiplier"].asFloat();
            if (item.isMember("edge"))
                gpio_desc.InterruptEdge = item["edge"].asString();
            if (item.isMember("decimal_points_current")) {
                gpio_desc.DecimalPlacesCurrent = item["decimal_points_current"].asInt();
            }
            if (item.isMember("decimal_points_total")) {
                gpio_desc.DecimalPlacesTotal = item["decimal_points_total"].asInt();
            }

            gpio_desc.InitialState = item.get("initial_state", false).asBool();

            gpio_desc.Order = index;
            AddGpio(gpio_desc);
        }
    } catch (const exception & e) {
        wb_throw(TGpioDriverException, string("malformed JSON config: ") + e.what());
    }
}

void THandlerConfig::AddGpio(TGpioDesc &gpio_desc)
{
    if (Names.find(gpio_desc.Name) != Names.end())
        wb_throw(TGpioDriverException, "duplicate GPIO Name in config: " + gpio_desc.Name);

    if (GpioNums.find(gpio_desc.Gpio) != GpioNums.end())
        wb_throw(TGpioDriverException, "duplicate GPIO in config: " + to_string(gpio_desc.Gpio));

    Names.insert(gpio_desc.Name);
    GpioNums.insert(gpio_desc.Gpio);
    Gpios.push_back(gpio_desc);
};

TGpioDriverConfig::TGpioDriverConfig(const string &fileName)
{
    if (fileName.empty())
    {
        wb_throw(TGpioDriverException, "empty filename");
    }

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

        if (!root.isMember("chips")) {
            wb_throw(TGpioDriverException, "'chips' is required field");
        }

        const auto &chips = root["chips"];
        DeviceName = root["device_name"].asString();

        if (!chips.isArray()) {
            wb_throw(TGpioDriverException, "'chips' must be array");
        }

        int lineGlobalIndex = 0, chipConfigIndex = 0;

        Chips.reserve(chips.size());

        auto getPath = [](const Json::Value & json) {
            bool rejectedLabel = false;
            if (json.isMember("label")) {
                try {
                    return GpioChipNumberToPath(GpioChipLabelToNumber(json["label"].asString()));
                } catch (const TGpioDriverException & e) {
                    LOG(Warn) << "Ignoring specified chip label '" << json["label"].asString() << "': " << e.what();
                    rejectedLabel = true;
                }
            }

            if (json.isMember("path")) {
                if (rejectedLabel) {
                    LOG(Warn) << "Using 'path' as fallback";
                }
                return json["path"].asString();
            }

            if (json.isMember("number")) {
                if (rejectedLabel) {
                    LOG(Warn) << "Using 'number' as fallback";
                }
                return GpioChipNumberToPath(json["number"].asUInt());
            }

            if (rejectedLabel) {
                wb_throw(TGpioDriverException, "no alternatives to 'label' found for chip");
            } else {
                wb_throw(TGpioDriverException, "either 'label', 'path' or 'number' required for chip");
            }
        };

        for (const auto & chip : chips)
        {
            WB_SCOPE_EXIT( ++chipConfigIndex; )

            string path;

            try {
                path = getPath(chip);
            } catch (const TGpioDriverException & e) {
                LOG(Error) << "Skipping chip config " << chipConfigIndex << ": " << e.what();
                continue;
            }

            if (!chip.isMember("lines")) {
                wb_throw(TGpioDriverException, "'lines' is required field");
            }

            TGpioChipConfig chipConfig { move(path) };

            const auto &lines = chip["lines"];

            if (!lines.isArray()) {
                wb_throw(TGpioDriverException, "'lines' must be array");
            }

            if (lines.empty()) {
                LOG(Warn) << "No lines for chip at '" << chipConfig.Path << "'. Skipping.";
                continue;
            }

            chipConfig.Lines.reserve(lines.size());

            set<uint32_t> addedOffsets;
            set<string> addedNames;

            for (const auto &line : lines)
            {
                TGpioLineConfig lineConfig{};

                if (!line.isMember("offset")) {
                    wb_throw(TGpioDriverException, "'offset' is required field");
                }

                if (!line.isMember("name")) {
                    wb_throw(TGpioDriverException, "'name' is required field");
                }

                lineConfig.Offset = line["offset"].asUInt();
                lineConfig.Name = line["name"].asString();

                if (!addedOffsets.insert(lineConfig.Offset).second) {
                    wb_throw(TGpioDriverException, "duplicate GPIO offset in config: '" + to_string(lineConfig.Offset) + "' at chip '" + chipConfig.Path + "'");
                }

                if (!addedNames.insert(lineConfig.Name).second) {
                    wb_throw(TGpioDriverException, "duplicate GPIO name in config: '" + lineConfig.Name + "' at chip '" + chipConfig.Path + "'");
                }

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

                lineConfig.Order = lineGlobalIndex++;

                chipConfig.Lines.push_back(move(lineConfig));
            }

            Chips.push_back(chipConfig);
        }
    } catch (const exception & e) {
        wb_throw(TGpioDriverException, string("malformed JSON config: ") + e.what());
    }
}

TGpioDriverConfig ToNewFormat(const THandlerConfig & oldConfig, TGpioDriverConfig newConfig)
{
    newConfig.DeviceName = oldConfig.DeviceName;
    map<uint32_t, pair<size_t, set<uint32_t>>> indexMapping;

    for (const auto & desc: oldConfig.Gpios) {
        uint32_t chipNumber, lineOffset;

        assert(desc.Gpio >= 0);

        try {
            tie(chipNumber, lineOffset) = FromSysfsGpio((uint32_t)desc.Gpio);
        } catch (const TGpioDriverException & e) {
            LOG(Error) << "Skipping GPIO " << desc.Gpio << " reason: " << e.what();
            continue;
        }

        const auto & chipInsRes = indexMapping.insert({ chipNumber, { newConfig.Chips.size(), {} } });

        auto & chipMapping = chipInsRes.first->second;
        {
            const auto inserted = chipInsRes.second;

            if (inserted) {
                newConfig.Chips.emplace_back(GpioChipNumberToPath(chipNumber));
            }
        }

        const auto chipIndex = chipMapping.first;
        auto & linesMapping  = chipMapping.second;

        const auto & lineInsRes = linesMapping.insert(lineOffset);
        const auto inserted = lineInsRes.second;

        assert(inserted); _unused(inserted);

        auto & chipConfig = newConfig.Chips[chipIndex];

        chipConfig.Lines.emplace_back();
        auto & lineConfig = chipConfig.Lines.back();

        lineConfig.Offset = lineOffset;
        lineConfig.IsActiveLow = desc.Inverted;
        lineConfig.Name = desc.Name;
        lineConfig.Direction = desc.Direction;
        EnumerateGpioEdge(desc.InterruptEdge, lineConfig.InterruptEdge);
        lineConfig.Type = desc.Type;
        lineConfig.Multiplier = desc.Multiplier;
        lineConfig.Order = desc.Order;
        lineConfig.DecimalPlacesTotal = desc.DecimalPlacesTotal;
        lineConfig.DecimalPlacesCurrent = desc.DecimalPlacesCurrent;
        lineConfig.InitialState = desc.InitialState;
    }

    return move(newConfig);
}

TGpioDriverConfig GetConvertConfig(const std::string & fileName)
{
    WB_SCOPE_NO_THROW_EXIT( LOG(Info) << "Read config ok"; )

    auto newConfigOK = true;
    auto oldConfigOK = true;
    TGpioDriverConfig newConfig;
    try {
        newConfig = TGpioDriverConfig(fileName);
    } catch (const exception & e) {
        newConfigOK = false;
        LOG(Warn) << "Unable to read config in new format: '" << e.what() << "'";
    }

    LOG(Info) << "Trying to read config in deprecated format...";
    try {
        newConfig = ToNewFormat(THandlerConfig(fileName), newConfig);
    } catch (const exception & e) {
        oldConfigOK = false;
        LOG(Warn) << "Unable to read config in deprecated format: '" << e.what() << "'";
    }

    if (newConfigOK || oldConfigOK) {
        return newConfig;
    }

    wb_throw(TGpioDriverException, "unable to read config");
}
