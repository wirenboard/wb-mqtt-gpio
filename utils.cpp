#include "utils.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "exceptions.h"
#include "log.h"

#include <wblib/utils.h>

#include <dirent.h>
#include <cstring>
#include <cassert>

#define LOG(logger) ::logger.Log() << "[utils] "

using namespace std;

namespace
{
    int chip_filter(const dirent * dir)
    {
        return string(dir->d_name).find("gpiochip") != string::npos;
    }

    vector<string> EnumerateGpioChipsSorted()
    {
        dirent ** dirs;
        auto chipCount = scandir("/dev", &dirs, chip_filter, alphasort);

        if (chipCount < 0) {
            wb_throw(TGpioDriverException, string("scandir failed: ") + strerror(errno));
        }

        vector<string> chipPaths;

        chipPaths.reserve(chipCount);

        for (size_t i = 0; i < static_cast<size_t>(chipCount); ++i) {
            chipPaths.push_back(dirs[i]->d_name);
        }

        return move(chipPaths);
    }

    struct TAbstractRange
    {
        virtual ~TAbstractRange() = default;
    };

    using PAbstractRange = shared_ptr<TAbstractRange>;

    struct TChipRange: virtual TAbstractRange
    {
        uint32_t ChipNumber;

        TChipRange(uint32_t chipNumber)
            : ChipNumber(chipNumber)
        {}

        TChipRange(const PGpioChip & chip)
            : ChipNumber(chip->GetNumber())
        {}
    };

    struct TGpioRange: virtual TAbstractRange
    {
        uint32_t Begin, End;

        TGpioRange(uint32_t begin, uint32_t end)
            : Begin(begin)
            , End(end)
        {}

        bool InRange(uint32_t gpio) const
        {
            return Begin <= gpio && gpio < End;
        }
    };

    struct TRange: TChipRange, TGpioRange
    {
        TRange(const PGpioChip & chip, uint32_t positionOffset)
            : TChipRange(chip->GetNumber())
            , TGpioRange(positionOffset, positionOffset + chip->GetLineCount())
        {}
    };

    struct TRangeCompare
    {
        bool operator()(const PAbstractRange & a, const PAbstractRange & b) const {
            if (auto rangeA = dynamic_pointer_cast<TRange>(a)) {
                if (auto rangeB = dynamic_pointer_cast<TGpioRange>(b)) {
                    return rangeA->End <= rangeB->Begin;
                }
                if (auto rangeB = dynamic_pointer_cast<TChipRange>(b)) {
                    return rangeA->ChipNumber < rangeB->ChipNumber;
                }
            } else if (auto rangeB = dynamic_pointer_cast<TRange>(b)) {
                if (auto rangeA = dynamic_pointer_cast<TGpioRange>(b)) {
                    return rangeA->End <= rangeB->Begin;
                }
                if (auto rangeA = dynamic_pointer_cast<TChipRange>(b)) {
                    return rangeA->ChipNumber < rangeB->ChipNumber;
                }
            }

            assert(false);

            LOG(Error) << "bug: invariant 1 violation";

            return false;
        }
    };

    set<PAbstractRange, TRangeCompare> ChipSet;
    bool ChipSetReady = false;

    void EnsureChipSetReady()
    {
        if (ChipSetReady) {
            return;
        }

        for (const auto & name: EnumerateGpioChipsSorted()) {
            const auto & chip = make_shared<TGpioChip>("/dev/" + name);

            uint32_t positionOffset = 0;

            if (!ChipSet.empty()) {
                positionOffset = dynamic_pointer_cast<TRange>(*--ChipSet.end())->End;
            }

            ChipSet.insert(make_shared<TRange>(chip, positionOffset));
        }

        ChipSetReady = true;
    }
}

string GpioChipNumberToPath(uint32_t number)
{
    return "/dev/gpiochip" + to_string(number);
}

uint32_t GpioPathToChipNumber(const string & path)
{
    return stoul(path.substr(13));
}

uint32_t ToSysfsGpio(const PGpioLine & line)
{
    EnsureChipSetReady();

    auto itRange = ChipSet.find(make_shared<TChipRange>(line->AccessChip()));

    if (itRange == ChipSet.end()) {
        wb_throw(TGpioDriverException, "unable to convert to sysfs GPIO: out of range");
    }

    auto pRange = dynamic_pointer_cast<TRange>(*itRange);

    uint32_t gpio = pRange->Begin + line->GetOffset();

    LOG(Debug) << "Converted " << line->DescribeShort() << " to GPIO number " << gpio;

    return gpio;
}

pair<uint32_t, uint32_t> FromSysfsGpio(uint32_t gpio)
{
    EnsureChipSetReady();

    auto itRange = ChipSet.find(make_shared<TGpioRange>(gpio, gpio));

    if (itRange == ChipSet.end()) {
        wb_throw(TGpioDriverException, "unable to convert to sysfs GPIO: out of range");
    }

    auto pRange = dynamic_pointer_cast<TRange>(*itRange);

    pair<uint32_t, uint32_t> res { pRange->ChipNumber, gpio - pRange->Begin };

    LOG(Debug) << "Converted sysfs GPIO number " << gpio << " to <chip number: offset>: " << res.first << ": " << res.second;

    return res;
}

string SetDecimalPlaces(float value, int set_decimal_places)
{
    ostringstream out;
    out << fixed << setprecision(set_decimal_places) << value;
    return out.str();
}
