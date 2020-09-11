#pragma once

#include "declarations.h"

#include <string>

namespace Utils
{
    std::string GpioChipNumberToPath(uint32_t number);
    uint32_t GpioPathToChipNumber(const std::string & path);
    uint32_t GpioChipLabelToNumber(const std::string & label);

    uint32_t ToSysfsGpio(const PGpioLine & line);
    std::pair<uint32_t, uint32_t> FromSysfsGpio(uint32_t gpio);

    std::string SetDecimalPlaces(float value, int decimal_points);

    void ClearMappingCache();
}
