#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mtfui::io {
    // Our own settings, stored apart from MTF's data in SKSE/Plugins/MagicTattoosUI/settings.json.
    struct UiSettings {
        std::vector<std::string> favorites;  // "packid/entryid", lowercase
        std::vector<std::string> recents;    // newest first
        bool pauseGame = true;
        bool livePreviewEnabled = true;
        int thumbSize = 96;
        std::string lastPreset;
        // Actors that may still carry a live preview: cleaned up on the next load.
        std::vector<std::uint32_t> previewActors;

        bool IsFavorite(const std::string& key) const;
        void ToggleFavorite(const std::string& key);
        void PushRecent(const std::string& key, std::size_t max = 24);

        void Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;
    };

    std::string ToLower(std::string s);
}
