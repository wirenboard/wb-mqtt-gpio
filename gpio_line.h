#pragma once

#include "declarations.h"

#include <string>

class TGpioLine
{
    PWGpioChip    Chip;
    uint32_t      Offset,
                  Flags;
    std::string   Name,
                  Consumer;
public:
    TGpioLine(const PGpioChip & chip, uint32_t offset);

    std::string DescribeShort() const;
    std::string Describe() const;
    std::string DescribeVerbose() const;
    const std::string & GetName() const;
    const std::string & GetConsumer() const;
    uint32_t GetOffset() const;
    uint32_t GetFlags() const;
    bool IsOutput() const;
    bool IsActiveLow() const;
    bool IsUsed() const;
    bool IsOpenDrain() const;
    bool IsOpenSource() const;
    uint8_t GetValue() const;
    PGpioChip AccessChip() const;
};
