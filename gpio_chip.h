#pragma once

#include "config.h"
#include "declarations.h"

#include <wblib/driver.h>
#include <wblib/thread_safe.h>

#include <linux/gpio.h>

class TGpioChip: public std::enable_shared_from_this<TGpioChip>
{
    friend TGpioLine;

    struct TPollingLines
    {
        std::vector<PGpioLine> Lines;
        int                    Fd = -1;
    };

    int                                          Fd;
    std::string                                  Name,
                                                 Label,
                                                 Path;
    std::vector<PGpioLine>                       Lines;
    std::unordered_map<PGpioLine, int>           OutputLines;
    std::unordered_map<uint32_t, TPollingLines>  PollingLines;
    std::unordered_map<int, PGpioLine>           ListenedLines;
    std::vector<uint8_t>                         LinesValues;
    std::vector<bool>                            LinesValuesChanged;
    int8_t                                       SupportsInterrupts;

public:
    TGpioChip(const std::string & path);
    ~TGpioChip();

    void LoadLines(const TLinesConfig & linesConfigs);
    void PollLinesValues();
    void PollLinesValues(const TPollingLines &);

    const std::string & GetName() const;
    const std::string & GetLabel() const;
    const std::string & GetPath() const;
    std::string Describe() const;
    uint32_t GetLineCount() const;
    const std::vector<PGpioLine> & GetLines() const;
    std::vector<std::pair<PGpioLine, EGpioEdge>> HandleInterrupt(int count, struct epoll_event * events);
    uint32_t GetNumber() const;
    PGpioLine GetLine(uint32_t offset) const;
    bool DoesSupportInterrupts() const;
    void AddToEpoll(int epfd);

private:
    enum class EInterruptSupport {UNKNOWN, YES, NO};

    EInterruptSupport TryListenLine(const PGpioLine & line, const TGpioLineConfig & config);
    void AddToPolling(const PGpioLine & line, const TGpioLineConfig & config);
    void InitInputs();
    void InitOutput(const PGpioLine & line, const TGpioLineConfig & config);
    void AutoDetectInterruptEdges();
    int GetFd() const;

    uint8_t GetLineValue(uint32_t offset) const;
    bool IsLineValueChanged(uint32_t offset) const;
    void SetLineValue(uint32_t offset, uint8_t value);
    void AcceptLineValue(uint32_t offset, uint8_t value);
};
