#include "io/JsonStore.h"

#include "io/StyledJson.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace mtfui::io {
    std::optional<std::string> ReadText(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string* error) {
        auto text = ReadText(path);
        if (!text) {
            if (error) {
                *error = "File not found";
            }
            return std::nullopt;
        }
        auto parsed = nlohmann::json::parse(*text, nullptr, false, true);
        if (parsed.is_discarded()) {
            if (error) {
                *error = "Not valid JSON";
            }
            return std::nullopt;
        }
        return parsed;
    }

    bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path;
        tmp += ".mtfui.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) {
                if (error) {
                    *error = "Cannot open " + tmp.string() + " for writing";
                }
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out) {
                if (error) {
                    *error = "Write failed for " + tmp.string();
                }
                return false;
            }
        }
        fs::rename(tmp, path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            if (error) {
                *error = "Cannot replace " + path.string();
            }
            return false;
        }
        return true;
    }

    bool WriteJsonStyled(const fs::path& path, const nlohmann::json& value, std::string* error) {
        return WriteTextAtomic(path, WriteStyled(value), error);
    }

    bool Backup(const fs::path& path, const fs::path& backupDir, int keep) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            return true;
        }
        fs::create_directories(backupDir, ec);
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
        localtime_s(&tm, &now);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        const std::string stem = path.stem().string();
        fs::copy_file(path, backupDir / (stem + "." + stamp + ".json"), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return false;
        }

        std::vector<fs::path> mine;
        for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
            const auto name = entry.path().filename().string();
            if (name.size() > stem.size() + 1 && name.compare(0, stem.size() + 1, stem + ".") == 0 &&
                entry.path().extension() == ".json") {
                const auto rest = name.substr(stem.size() + 1);
                if (rest.size() == std::string("YYYYMMDD-HHMMSS.json").size()) {
                    mine.push_back(entry.path());
                }
            }
        }
        std::sort(mine.begin(), mine.end());
        while (static_cast<int>(mine.size()) > keep) {
            fs::remove(mine.front(), ec);
            mine.erase(mine.begin());
        }
        return true;
    }
}
