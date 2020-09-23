#pragma once

#include "declarations.h"
#include "types.h"

#include <string>

class TGpioLine
{
    PWGpioChip          Chip;
    PUGpioCounter       Counter;
    PUGpioLineConfig    Config;

    uint32_t      Offset,
                  Flags;
    int           Fd;
    std::string   Name,
                  Consumer;

    TTimePoint    PreviousInterruptionTimePoint;

    TValue<uint8_t> Value;

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
    bool IsValueChanged() const;
    bool IsValueExpired(long duration) const;
    void ResetIsChanged();
    uint8_t GetValue() const;
    uint8_t GetValueAndStampTime();
    void SetValue(uint8_t);
    void SetCachedValue(uint8_t);
    PGpioChip AccessChip() const;
    bool IsHandled() const;
    void SetFd(int);
    int GetFd() const;
    EGpioEdge GetInterrruptEdge() const;
    EInterruptStatus HandleInterrupt(EGpioEdge, const TTimePoint &);
    void Update();
    const PUGpioCounter & GetCounter() const;
    const PUGpioLineConfig & GetConfig() const;
};
