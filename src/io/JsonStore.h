#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace mtfui::io {
    namespace fs = std::filesystem;

    std::optional<std::string> ReadText(const fs::path& path);

    // Parses a JSON file. Returns nullopt and fills error when the file is missing or malformed.
    std::optional<nlohmann::json> ReadJson(const fs::path& path, std::string* error = nullptr);

    // Writes text to a sibling temp file and renames it over the target, so a crash mid-write
    // never leaves MTF a truncated preset.
    bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error = nullptr);

    bool WriteJsonStyled(const fs::path& path, const nlohmann::json& value, std::string* error = nullptr);

    // Copies path into backupDir as <stem>.<timestamp>.json and keeps the newest keep copies per stem.
    bool Backup(const fs::path& path, const fs::path& backupDir, int keep = 10);
}
