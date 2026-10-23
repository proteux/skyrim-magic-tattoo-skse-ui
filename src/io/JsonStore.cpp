#include "io/JsonStore.h"

#include "io/StyledJson.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace mtfui::io {
    namespace {
        std::string Utf8(const fs::path& path) {
            const auto u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string LowerAscii(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        std::string WinError(DWORD code) {
            return std::system_category().message(static_cast<int>(code));
        }

        // Accepts YYYYMMDD-HHMMSS and YYYYMMDD-HHMMSS-mmm.
        bool IsStamp(const std::string& s) {
            if (s.size() != 15 && s.size() != 19) {
                return false;
            }
            for (std::size_t i = 0; i < s.size(); ++i) {
                const bool dash = i == 8 || i == 15;
                if (dash ? s[i] != '-' : !std::isdigit(static_cast<unsigned char>(s[i]))) {
                    return false;
                }
            }
            return true;
        }
    }

    std::string PathText(const fs::path& path) {
        try {
            return path.string();
        } catch (...) {
            return Utf8(path);
        }
    }

    std::optional<std::string> NarrowName(const fs::path& path, bool stemOnly) {
        try {
            return stemOnly ? path.stem().string() : path.filename().string();
        } catch (...) {
            return std::nullopt;
        }
    }

    std::vector<fs::path> ListFiles(const fs::path& dir, const std::string& extension) {
        std::vector<std::pair<std::string, fs::path>> found;
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            return {};
        }
        fs::directory_iterator it(dir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const auto name = NarrowName(it->path(), false);
            if (!name) {
                continue;
            }
            const auto lower = LowerAscii(*name);
            if (lower.size() > extension.size() && lower.compare(lower.size() - extension.size(), extension.size(), extension) == 0) {
                found.emplace_back(lower, it->path());
            }
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<fs::path> out;
        out.reserve(found.size());
        for (auto& [_, p] : found) {
            out.push_back(std::move(p));
        }
        return out;
    }

    std::optional<std::string> ReadText(const fs::path& path, std::string* error) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) {
                std::error_code ec;
                *error = fs::exists(path, ec) ? "Cannot open the file (is another program using it?)" : "File not found";
            }
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string* error) {
        auto text = ReadText(path, error);
        if (!text) {
            return std::nullopt;
        }
        try {
            return nlohmann::json::parse(*text, nullptr, true, true);
        } catch (const nlohmann::json::parse_error& e) {
            if (error) {
                *error = "Not valid JSON (byte " + std::to_string(e.byte) + ")";
            }
            return std::nullopt;
        }
    }

    bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path;
        tmp += ".mtfui.tmp";
        auto fail = [&](const std::string& what, DWORD code) {
            DeleteFileW(tmp.c_str());
            if (error) {
                *error = what + ": " + WinError(code);
            }
            return false;
        };

        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (error) {
                *error = "Cannot open " + PathText(tmp) + " for writing: " + WinError(code);
            }
            return false;
        }
        DWORD written = 0;
        const bool wrote = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
        const DWORD writeCode = wrote ? 0 : GetLastError();
        // Without the flush, a power loss after the rename can leave a zero-byte preset.
        const bool flushed = wrote && FlushFileBuffers(h);
        const DWORD flushCode = flushed ? 0 : GetLastError();
        CloseHandle(h);
        if (!wrote) {
            return fail("Write failed for " + PathText(tmp), writeCode);
        }
        if (!flushed) {
            return fail("Flush failed for " + PathText(tmp), flushCode);
        }
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return fail("Cannot replace " + PathText(path), GetLastError());
        }
        return true;
    }

    bool WriteJsonStyled(const fs::path& path, const nlohmann::json& value, std::string* error) {
        return WriteTextAtomic(path, WriteStyled(value), error);
    }

    std::vector<BackupInfo> ListBackups(const fs::path& backupDir) {
        std::vector<BackupInfo> out;
        for (const auto& p : ListFiles(backupDir, ".json")) {
            const auto stemAndStamp = NarrowName(p, true);
            if (!stemAndStamp) {
                continue;
            }
            const auto dot = stemAndStamp->rfind('.');
            if (dot == std::string::npos || dot == 0) {
                continue;
            }
            BackupInfo b{ p, stemAndStamp->substr(0, dot), stemAndStamp->substr(dot + 1) };
            if (IsStamp(b.stamp)) {
                out.push_back(std::move(b));
            }
        }
        std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.stamp > b.stamp; });
        return out;
    }

    bool Backup(const fs::path& path, const fs::path& backupDir, int keep, std::string* error) {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            return true;
        }
        const auto stem = NarrowName(path, true);
        if (!stem) {
            if (error) {
                *error = "The file name can't be used for a backup";
            }
            return false;
        }
        fs::create_directories(backupDir, ec);

        std::vector<BackupInfo> mine;
        for (auto& b : ListBackups(backupDir)) {
            if (_stricmp(b.stem.c_str(), stem->c_str()) == 0) {
                mine.push_back(std::move(b));
            }
        }
        const auto current = ReadText(path);
        if (current && !mine.empty() && ReadText(mine.front().path) == current) {
            return true;
        }

        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm tm{};
        localtime_s(&tm, &t);
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        char full[40];
        std::snprintf(full, sizeof(full), "%s-%03d", stamp, static_cast<int>(ms));
        const auto target = backupDir / (*stem + "." + full + ".json");
        fs::copy_file(path, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (error) {
                *error = "Could not copy to " + PathText(backupDir) + ": " + ec.message();
            }
            return false;
        }

        mine.insert(mine.begin(), BackupInfo{ target, *stem, full });
        while (static_cast<int>(mine.size()) > keep) {
            fs::remove(mine.back().path, ec);
            mine.pop_back();
        }
        return true;
    }
}
