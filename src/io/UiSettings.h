#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mtfui::io {
    // Where one of the standalone window's panels sits. w == 0 means it has never been placed.
    struct PanelState {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool open = false;
    };

    // Our own settings, stored apart from MTF's data in SKSE/Plugins/MagicTattoosUI/settings.json.
    struct UiSettings {
        std::vector<std::string> favorites;  // "packid/entryid", lowercase
        std::vector<std::string> recents;    // newest first
        bool pauseGame = true;
        bool livePreviewEnabled = true;
        bool holdTarget = true;  // NPC targets stand still while the editor is open
        int thumbSize = 96;
        bool showDetails = true;  // Tattoos tab details pane
        bool splitPanels = true;  // standalone window as separate panels instead of one tabbed window
        int panelOpacity = 100;   // percent, panel backgrounds only
        std::map<std::string, PanelState> panels;
        std::string lastPreset;
        // Actors that have worn the live preview, newest first: every load removes it from them.
        std::vector<std::uint32_t> previewActors;
        // NPC whose AI this menu switched off: switched back on at the next load if it's still set.
        std::uint32_t heldActor = 0;

        bool IsFavorite(const std::string& key) const;
        void ToggleFavorite(const std::string& key);
        void PushRecent(const std::string& key, std::size_t max = 24);

        void Load(const std::filesystem::path& path);
        bool Save(const std::filesystem::path& path) const;
    };

    std::string ToLower(std::string s);
}
