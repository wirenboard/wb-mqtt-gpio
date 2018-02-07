#include "config.h"
#include "log.h"
#include "exceptions.h"

#include <wblib/utils.h>
#include <jsoncpp/json/json.h>

#include <iostream>
#include <fstream>

#define LOG(logger) ::logger.Log() << "[config] "

using namespace std;

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

        if (!reader.parse(file, root, false))
        {
            // Report failures and their locations
            // in the document.
            wb_throw(TGpioDriverException, "failed to parse JSON \n" + reader.getFormatedErrorMessages());
        }
    }

    try {
        DeviceName = root["device_name"].asString();

        // Let's extract the array contained
        // in the root object
        const auto &array = root["channels"];

        // Iterate over sequence elements and
        // print its values
        for (unsigned int index = 0; index < array.size(); ++index) {
            const auto & item = array[index];
            TGpioDesc gpio_desc;
            gpio_desc.Gpio = item["gpio"].asInt();
            gpio_desc.Name = item["name"].asString();
            if (item.isMember("inverted"))
                gpio_desc.Inverted = item["inverted"].asBool();
            if (item.isMember("direction") && item["direction"].asString() == "input")
                gpio_desc.Direction = TGpioDirection::Input;
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

bool THandlerConfig::IsOldFormat() const
{
    return true;
}

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

        if (!reader.parse(file, root, false))
        {
            // Report failures and their locations
            // in the document.
            wb_throw(TGpioDriverException, "failed to parse JSON \n" + reader.getFormatedErrorMessages());
        }
    }

    try {

        if (!root.isMember("chips")) {
            wb_throw(TGpioDriverException, "'chips' is required field");
        }

        const auto &chips = root["chips"];

        if (!chips.isArray()) {
            wb_throw(TGpioDriverException, "'chips' must be array");
        }

        Chips.reserve(chips.size());

        for (const auto &chip : chips)
        {
            TGpioChipConfig chipConfig{};

            if (chip.isMember("path")) {
                chipConfig.Path = chip["path"].asString();
            } else if (chip.isMember("number")) {
                chipConfig.Path = "/dev/gpiochip" + to_string(chip["number"].asUInt());
            } else {
                wb_throw(TGpioDriverException, "either 'path' or 'number' required for chip");
            }

            if (!chip.isMember("lines")) {
                wb_throw(TGpioDriverException, "'lines' is required field");
            }

            const auto &lines = chip["lines"];

            if (!lines.isArray()) {
                wb_throw(TGpioDriverException, "'lines' must be array");
            }

            if (lines.empty()) {
                LOG(Warn) << "no lines for chip at '" << chipConfig.Path << "'. Skipping.";
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
                    lineConfig.Direction = TGpioDirection::Input;
                if (line.isMember("type"))
                    lineConfig.Type = line["type"].asString();
                if (line.isMember("multiplier"))
                    lineConfig.Multiplier = line["multiplier"].asFloat();
                if (line.isMember("edge"))
                {
                    const auto &edge = line["edge"].asString();
                    if (edge == "rising")
                        lineConfig.InterruptEdge = TGpioEdge::RISING;
                    else if (edge == "falling")
                        lineConfig.InterruptEdge = TGpioEdge::FALLING;
                    else if (edge == "both")
                        lineConfig.InterruptEdge = TGpioEdge::BOTH;
                }
                if (line.isMember("decimal_points_current"))
                {
                    lineConfig.DecimalPlacesCurrent = line["decimal_points_current"].asInt();
                }
                if (line.isMember("decimal_points_total"))
                {
                    lineConfig.DecimalPlacesTotal = line["decimal_points_total"].asInt();
                }

                lineConfig.InitialState = line.get("initial_state", false).asBool();

                chipConfig.Lines[lineConfig.Offset] = (move(lineConfig));
            }

            Chips.push_back(move(chipConfig));
        }
    } catch (const exception & e) {
        wb_throw(TGpioDriverException, string("malformed JSON config: ") + e.what());
    }
}

bool TGpioDriverConfig::IsOldFormat() const
{
    return false;
}
