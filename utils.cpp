#include "utils.h"
#include "exceptions.h"

#include <wblib/utils.h>

#include <dirent.h>
#include <cstring>

using namespace std;

static int chip_filter(const dirent * dir)
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

    for (size_t i = 0; i < chipCount; ++i) {
        chipPaths.push_back(dirs[i]->d_name);
    }

    return move(chipPaths);
}
