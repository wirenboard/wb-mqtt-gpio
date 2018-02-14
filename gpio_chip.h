#pragma once

#include "config.h"
#include "declarations.h"

#include <wblib/driver.h>

#include <linux/gpio.h>

class TGpioChip: public std::enable_shared_from_this<TGpioChip>
{
    friend TGpioLine;

    struct TPollingLines
    {
        std::vector<PGpioLine> Lines;
        int                    Fd = -1;
    };

    struct TListenedLine
    {
        PGpioLine Line;
        int       Fd;

        TListenedLine(const PGpioLine & line, int fd = -1)
            : Line(line)
            , Fd(fd)
        {}
    };

    int                                          Fd;
    std::string                                  Name,
                                                 Label,
                                                 Path;
    std::vector<PGpioLine>                       Lines;
    std::unordered_map<uint32_t, TPollingLines>  PollingLines;
    std::vector<TListenedLine>                   ListenedLines;
    std::vector<uint8_t>                         LinesValues;

    int8_t                                       SupportsInterrupts;
public:
    TGpioChip(const std::string & path);
    ~TGpioChip();

    void LoadLines(const TLinesConfig & linesConfigs);
    void PollLinesValues();

    const std::string & GetName() const;
    const std::string & GetLabel() const;
    const std::string & GetPath() const;
    std::string Describe() const;
    uint32_t GetLineCount() const;
    const std::vector<PGpioLine> & GetLines() const;
    uint32_t GetNumber() const;
    PGpioLine GetLine(uint32_t offset) const;
    bool DoesSupportInterrupts() const;
    void AddToEpoll(int epfd);

private:
    enum class EInterruptSupport {UNKNOWN, YES, NO};

    EInterruptSupport TryListenLine(const PGpioLine & line, const TGpioLineConfig & config);
    void AddToPolling(const PGpioLine & line, const TGpioLineConfig & config);
    void InitPolling();
    uint8_t GetLineValue(uint32_t offset) const;
    int GetFd() const;
};
