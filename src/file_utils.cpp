#include "file_utils.h"
#include <filesystem>
#include <set>

TNoDirError::TNoDirError(const std::string& msg): std::runtime_error(msg)
{}

bool TryOpen(const std::vector<std::string>& fnames, std::ifstream& file)
{
    for (auto& fname: fnames) {
        file.open(fname);
        if (file.is_open()) {
            return true;
        }
        file.clear();
    }
    return false;
}

void WriteToFile(const std::string& fileName, const std::string& value)
{
    std::ofstream f;
    OpenWithException(f, fileName);
    f << value;
}

void IterateDir(const std::string& dirName, std::function<bool(const std::string&)> fn)
{
    try {
        const std::filesystem::path dirPath{dirName};
        std::set<std::filesystem::path> sortedByPath;

        for (auto& entry: std::filesystem::directory_iterator(dirPath))
            sortedByPath.insert(entry.path());

        for (auto& filePath: sortedByPath) {
            const auto filenameStr = filePath.filename().string();
            if (fn(filenameStr)) {
                return;
            }
        }
    } catch (std::filesystem::filesystem_error const& ex) {
        throw TNoDirError(ex.what());
    }
}

std::string IterateDirByPattern(const std::string& dirName,
                                const std::string& pattern,
                                std::function<bool(const std::string&)> fn)
{
    std::string res;
    IterateDir(dirName, [&](const auto& name) {
        if (name.find(pattern) != std::string::npos) {
            std::string d(dirName + "/" + name);
            if (fn(d)) {
                res = d;
                return true;
            }
        }
        return false;
    });
    return res;
}
