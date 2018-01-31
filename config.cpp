#include "config.h"
#include "log.h"
#include "exceptions.h"

#include <wblib/utils.h>
#include <jsoncpp/json/json.h>

#include <iostream>
#include <fstream>

using namespace std;

THandlerConfig::THandlerConfig(const std::string & fileName)
{
    if (fileName.empty()) {
        wb_throw(TGpioDriverException, "empty filename");
    }

    Json::Value root;
    {   // read and parse file
        Json::Reader reader;
        ifstream file(fileName);

        if (!reader.parse(file, root, false)) {
            // Report failures and their locations
            // in the document.
            wb_throw(TGpioDriverException, "failed to parse JSON \n"
                + reader.getFormatedErrorMessages()
            );
        }
    }

    {
        DeviceName = root["device_name"].asString();

        // Let's extract the array contained
        // in the root object
        const auto & array = root["channels"];

        // Iterate over sequence elements and
        // print its values
        for(unsigned int index = 0; index < array.size(); ++index) {
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
    }
}

void THandlerConfig::AddGpio(TGpioDesc &gpio_desc)
{
    if (Names.find(gpio_desc.Name) != Names.end()) {
        Error.Log() << "Duplicate GPIO Name in config: " << gpio_desc.Name;
        exit(1);
    }
    if (GpioNums.find(gpio_desc.Gpio) != GpioNums.end()) {
        Error.Log() << "Duplicate GPIO in config: " << gpio_desc.Gpio;
        exit(1);
    }
    Names.insert(gpio_desc.Name);
    GpioNums.insert(gpio_desc.Gpio);
    Gpios.push_back(gpio_desc);
};
