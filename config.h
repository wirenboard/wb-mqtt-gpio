#pragma once

#include "types.h"
#include <wblib/driver_args.h>
#include <vector>

enum class EGpioDirection
{
    Input,
    Output
};

struct TGpioLineConfig
{
    uint32_t       Offset;
    bool           IsOpenDrain  = false;
    bool           IsOpenSource = false;
    bool           IsActiveLow  = false;
    std::string    Name;
    EGpioDirection Direction     = EGpioDirection::Output;
    EGpioEdge      InterruptEdge = EGpioEdge::BOTH;
    std::string    Type;
    float          Multiplier           = 1.0;
    int            DecimalPlacesTotal   = -1;
    int            DecimalPlacesCurrent = -1;
    bool           InitialState         = false;
};

using TLinesConfig = std::vector<TGpioLineConfig>;

struct TGpioChipConfig
{
    std::string  Path;
    TLinesConfig Lines;

    TGpioChipConfig(const std::string& path) : Path(path) {}
};

struct TGpioDriverConfig
{
    std::string                  DeviceName;
    WBMQTT::TPublishParameters   PublishParameters;
    std::vector<TGpioChipConfig> Chips;
};

/**
 * @brief Load configuration from config files. Throws TBadConfigError on validation error.
 *
 * @param mainConfigFile - path and name of a main config file.
 * It will be loaded if optional config file is empty.
 * @param optionalConfigFile - path and name of an optional config file. It will be loaded instead
 * of all other config files
 * @param systemConfigsDir - folder with system generated config files. 
 * They will be loaded if optional config file is empty.
 * @param schemaFile - path and name of a file with JSONSchema for configs
 */
TGpioDriverConfig LoadConfig(const std::string& mainConfigFile,
                             const std::string& optionalConfigFile,
                             const std::string& systemConfigsDir,
                             const std::string& schemaFile);