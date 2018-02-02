#pragma once

#include "declarations.h"

class TGpioLine
{
    PWGpioChipDriver    Chip;
    uint32_t            Offset;
    bool                IsOutput,
                        IsActiveLow,
                        IsUsed,
                        IsOpenDrain,
                        IsOpenSource;
    std::string         Name,
                        Consumer;
public:
    TGpioLine(const PGpioChipDriver & chip, uint32_t offset);

    std::string Describe() const;
    const std::string & GetName() const;
    const std::string & GetConsumer() const;

private:
    PGpioChipDriver AccessChip() const;
}
