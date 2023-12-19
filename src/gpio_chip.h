#pragma once

#include "config.h"
#include "declarations.h"

// #ifndef LINUX_GPIO
// #define LINUX_GPIO
#include <linux/gpio.h>
// #endif

class TGpioChip: public std::enable_shared_from_this<TGpioChip>
{
    int Fd;
    std::string Name, Label, Path;
    uint32_t LineCount;

public:
    TGpioChip(); // dummy gpiochip for tests
    TGpioChip(const std::string& path);
    ~TGpioChip();

    std::vector<PGpioLine> LoadLines(const TLinesConfig& linesConfigs);

    const std::string& GetName() const;
    const std::string& GetLabel() const;
    const std::string& GetPath() const;
    std::string Describe() const;
    uint32_t GetLineCount() const;
    uint32_t GetNumber() const;
    int GetFd() const;
};
