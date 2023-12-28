#include "utils.h"
#include "exceptions.h"
#include "gpio_chip.h"
#include "gpio_line.h"
#include "log.h"

#include <wblib/utils.h>

#include <cassert>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <set>
#include <unordered_map>

#define LOG(logger) ::logger.Log() << "[utils] "

using namespace std;

namespace Utils
{
    namespace
    {
        int chip_filter(const dirent* dir)
        {
            return string(dir->d_name).find("gpiochip") == 0;
        }

        vector<pair<uint32_t, uint32_t>> EnumerateGpioChipsSorted()
        {
            static const auto sysfsBase = string("/sys/class/gpio/");

            dirent** dirs;
            auto chipCount = scandir(sysfsBase.c_str(), &dirs, chip_filter, alphasort);

            if (chipCount < 0) {
                wb_throw(TGpioDriverException, "scandir of '" + sysfsBase + "' failed: " + strerror(errno));
            }

            vector<pair<uint32_t, uint32_t>> chipPaths;

            chipPaths.reserve(chipCount);

            for (size_t i = 0; i < static_cast<size_t>(chipCount); ++i) {
                const auto dirName = dirs[i]->d_name;

                uint32_t base;
                {
                    auto baseFileName = sysfsBase + dirName + "/base";
                    ifstream baseFile(baseFileName);

                    if (!baseFile.is_open()) {
                        wb_throw(TGpioDriverException, "unable to read base of gpio chip (" + baseFileName + ")");
                    }

                    baseFile >> base;
                }

                uint32_t number;
                {
                    dirent** devDirs;
                    auto deviceDir = sysfsBase + dirName + "/device";
                    auto retVal = scandir(deviceDir.c_str(), &devDirs, chip_filter, alphasort);
                    if (retVal < 0) {
                        wb_throw(TGpioDriverException, "scandir of '" + deviceDir + "' failed: " + strerror(errno));
                    } else if (retVal != 1) {
                        wb_throw(TGpioDriverException,
                                 "too many gpiochips in '" + deviceDir + "': " + to_string(retVal) +
                                     "; expected only one");
                    }
                    number = GpioPathToChipNumber("/dev/" + string(devDirs[0]->d_name));
                }

                chipPaths.push_back({number, base});
            }

            return chipPaths;
        }

        struct TAbstractRange
        {
            virtual ~TAbstractRange() = default;
        };

        using PAbstractRange = shared_ptr<TAbstractRange>;

        struct TChipRange: virtual TAbstractRange
        {
            uint32_t ChipNumber;

            TChipRange(uint32_t chipNumber): ChipNumber(chipNumber)
            {}

            TChipRange(const PGpioChip& chip): ChipNumber(chip->GetNumber())
            {}
        };

        struct TGpioRange: virtual TAbstractRange
        {
            uint32_t Begin, End;

            TGpioRange(uint32_t begin, uint32_t end): Begin(begin), End(end)
            {}

            bool InRange(uint32_t gpio) const
            {
                return Begin <= gpio && gpio < End;
            }
        };

        struct TRange: TChipRange, TGpioRange
        {
            TRange(const PGpioChip& chip, uint32_t positionOffset)
                : TChipRange(chip->GetNumber()),
                  TGpioRange(positionOffset, positionOffset + chip->GetLineCount())
            {}
        };

        struct TRangeCompare
        {
            bool operator()(const PAbstractRange& a, const PAbstractRange& b) const
            {
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
        unordered_map<string, uint32_t> ChipLabelToNumber;
        bool MappingsAreReady = false, MappingsWereCleared = false;

        void EnsureMappingsAreReady()
        {
            assert(!MappingsWereCleared);

            if (MappingsAreReady) {
                return;
            }

            for (const auto& numberBase: EnumerateGpioChipsSorted()) {
                auto number = numberBase.first;
                auto base = numberBase.second;

                const auto& chip = make_shared<TGpioChip>(GpioChipNumberToPath(number));

                ChipSet.insert(make_shared<TRange>(chip, base));
                ChipLabelToNumber[chip->GetLabel()] = chip->GetNumber();
            }

            MappingsAreReady = true;
        }
    } // namespace

    string GpioChipNumberToPath(uint32_t number)
    {
        return "/dev/gpiochip" + to_string(number);
    }

    uint32_t GpioPathToChipNumber(const string& path)
    {
        return stoul(path.substr(13));
    }

    uint32_t GpioChipLabelToNumber(const string& label)
    {
        EnsureMappingsAreReady();

        auto itLabelNumber = ChipLabelToNumber.find(label);
        if (itLabelNumber == ChipLabelToNumber.end()) {
            wb_throw(TGpioDriverException, "unable to find chip with label '" + label + "'");
        }
        return itLabelNumber->second;
    }

    uint32_t ToSysfsGpio(const PGpioLine& line)
    {
        EnsureMappingsAreReady();

        auto itRange = ChipSet.find(make_shared<TChipRange>(line->AccessChip()));

        if (itRange == ChipSet.end()) {
            wb_throw(TGpioDriverException, "unable to convert to sysfs " + line->DescribeShort() + ": out of range");
        }

        auto pRange = dynamic_pointer_cast<TRange>(*itRange);

        assert(pRange);

        uint32_t gpio = pRange->Begin + line->GetOffset();

        LOG(Debug) << line->DescribeShort() << " => sysfs GPIO number " << gpio;

        return gpio;
    }

    pair<uint32_t, uint32_t> FromSysfsGpio(uint32_t gpio)
    {
        EnsureMappingsAreReady();

        auto itRange = ChipSet.find(make_shared<TGpioRange>(gpio, gpio));

        if (itRange == ChipSet.end()) {
            wb_throw(TGpioDriverException, "unable to convert from sysfs GPIO " + to_string(gpio) + ": out of range");
        }

        auto pRange = dynamic_pointer_cast<TRange>(*itRange);

        pair<uint32_t, uint32_t> res{pRange->ChipNumber, gpio - pRange->Begin};

        LOG(Info) << "Sysfs GPIO number " << gpio << " => GPIO line " << res.first << ": " << res.second;

        return res;
    }

    string SetDecimalPlaces(float value, int set_decimal_places)
    {
        ostringstream out;
        out << fixed << setprecision(set_decimal_places) << value;
        return out.str();
    }

    void ClearMappingCache()
    {
        ChipSet.clear();
        ChipLabelToNumber.clear();
        MappingsAreReady = false;
        MappingsWereCleared = true;
    }
} // namespace Utils
