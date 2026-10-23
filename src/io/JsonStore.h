#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mtfui::io {
    namespace fs = std::filesystem;

    // For messages and logs: the path in the ANSI code page, or UTF-8 when it has characters the code page can't hold.
    std::string PathText(const fs::path& path);

    // The filename in the ANSI code page, which is how PapyrusUtil and MTF name preset files. nullopt when it can't be.
    std::optional<std::string> NarrowName(const fs::path& path, bool stemOnly);

    // Every regular file in dir with the given lowercase extension whose name fits the ANSI code page, sorted by name.
    // Never throws.
    std::vector<fs::path> ListFiles(const fs::path& dir, const std::string& extension);

    std::optional<std::string> ReadText(const fs::path& path, std::string* error = nullptr);

    // Parses a JSON file. Returns nullopt and fills error when the file is missing or malformed.
    std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string* error = nullptr);

    // Writes text to a sibling temp file and renames it over the target, so a crash mid-write
    // never leaves MTF a truncated preset.
    bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error = nullptr);

    bool WriteJsonStyled(const fs::path& path, const nlohmann::json& value, std::string* error = nullptr);

    // Copies path into backupDir as <stem>.<timestamp>.json and keeps the newest keep copies per stem.
    // Skips the copy when the newest backup already has the same bytes.
    bool Backup(const fs::path& path, const fs::path& backupDir, int keep = 10, std::string* error = nullptr);

    struct BackupInfo {
        fs::path path;
        std::string stem;  // preset file name
        std::string stamp;  // YYYYMMDD-HHMMSS-mmm
    };

    // Backups in backupDir, newest first.
    std::vector<BackupInfo> ListBackups(const fs::path& backupDir);
}
