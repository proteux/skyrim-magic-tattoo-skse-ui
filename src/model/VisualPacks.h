#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mtfui::model {
    struct VisualEntry {
        std::string id;
        std::string label;
        std::vector<std::string> textures;  // relative to Data/textures
        std::string subject;
        std::string style;
        std::string placement;
        std::string description;
    };

    struct VisualPack {
        std::string packId;
        std::string label;
        std::string area = "Body";  // Body, Face, Hand(s), Feet
        std::filesystem::path file;
        std::vector<VisualEntry> entries;

        const VisualEntry* Find(const std::string& entryId) const;
    };

    std::optional<VisualPack> LoadVisualPack(const std::filesystem::path& file);

    // Normalizes the pack area to the names MTF's SetOverlaySlot accepts: Body, Face, Hands, Feet.
    std::string NormalizeArea(const std::string& area);
}
