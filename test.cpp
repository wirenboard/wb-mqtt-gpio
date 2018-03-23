#include "gpio_chip.h"
#include "gpio_line.h"
#include "gpio_counter.h"
#include "config.h"
#include "utils.h"
#include "log.h"

#include <iostream>

#include <sys/epoll.h>

using namespace std;
using namespace chrono;

int main()
{
    Debug.SetEnabled(true);
    auto config = GetConvertConfig("./gpio_test_config.json");

    vector<PGpioChip> chips;

    for (const auto & chipConfig: config.Chips) {
        auto chip = make_shared<TGpioChip>(chipConfig.first);
        chips.push_back(chip);
        cout << chip->Describe() << endl;
        chip->LoadLines(chipConfig.second.Lines);

        for (const auto & line: chip->GetLines()) {
            if (line->IsHandled()) {
                cout << line->DescribeVerbose() << endl;
            }
        }

        chip->PollLinesValues();
    }

    for (const auto & chip: chips) {
        for (const auto & line: chip->GetLines()) {
            if (line->IsHandled()) {
                cout << "Line: " << line->DescribeShort() << " value: " << (int)line->GetValue() << endl;
            }
        }
    }

    cout << "Init epoll" << endl;

    auto epfd = epoll_create(1);    // creating epoll for Interrupts

    for (const auto & chip: chips) {
        chip->AddToEpoll(epfd);
    }

    while(1) {
        struct epoll_event events[20] {};

        int count = epoll_wait(epfd, events, 20, 500);
        if (count) {
            for (const auto & chip: chips) {
                const auto & linesEdges = chip->HandleInterrupt(count, events);

                for (const auto & lineEdge: linesEdges) {
                    const auto & line = lineEdge.first;
                    auto edge = lineEdge.second;
                    const auto & lineConfig = config.Chips.at(chip->GetPath()).Lines.at(line->GetOffset());

                    if (line->IsValueChanged()) {
                        cout << "interrupted line: " << lineConfig.Name << " (" << line->DescribeShort() << ") value: " << (int)line->GetValue() << " edge: " << GpioEdgeToString(edge);
                        if (const auto & counter = line->GetCounter()) {
                            cout << " count: " << counter->GetCounts();
                        }
                        cout << endl;
                    }
                }
            }
        }
    }
}
