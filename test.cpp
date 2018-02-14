#include "gpio_chip.h"
#include "gpio_line.h"
#include "config.h"
#include "utils.h"
#include "log.h"

#include <iostream>

using namespace std;

int main()
{
    Debug.SetEnabled(true);
    auto config = GetConvertConfig("./gpio_test_config.json");
    for (const auto & chipConfig: config.Chips) {
        auto chip = make_shared<TGpioChip>(chipConfig.Path);

        cout << chip->Describe() << endl;

        chip->LoadLines(chipConfig.Lines);

        for (const auto & line: chip->GetLines()) {
            if (line) {
                cout << line->DescribeVerbose() << endl;
            }
        }
    }
}
