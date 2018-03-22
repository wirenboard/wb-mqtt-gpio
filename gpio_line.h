#pragma once

#include "declarations.h"

#include <string>

class TGpioLine
{
    PWGpioChip    Chip;
    PUGpioCounter Counter;
    uint32_t      Offset,
                  Flags;
    std::string   Name,
                  Consumer;

    bool          IsHandledByDriver;

public:
    TGpioLine(const PGpioChip & chip, const TGpioLineConfig & config);

    void UpdateInfo();
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
    void SetValue(uint8_t);
    PGpioChip AccessChip() const;
    bool IsHandled() const;
    void SetIsHandled(bool);
    void HandleInterrupt(EGpioEdge);

private:
    uint8_t InvertIfNeeded(uint8_t) const;
};
