#include "gpio_chip.h"
#include "gpio_line.h"
#include "utils.h"

#include <iostream>

using namespace std;

int main()
{
    for (const auto & name: EnumerateGpioChipsSorted()) {
        auto chip = make_shared<TGpioChip>("/dev/" + name);

        cout << chip->Describe() << endl;
        for (uint32_t offset = 0; offset < chip->GetLineCount(); ++offset) {
            auto line = make_shared<TGpioLine>(chip, offset);
            if (line->GetConsumer() == "sysfs") {
                cout << line->DescribeVerbose() << endl;
            }
        }
    }
}
