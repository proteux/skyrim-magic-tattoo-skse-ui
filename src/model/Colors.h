#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mtfui::model {
    struct NamedColor {
        std::string name;
        std::uint32_t rgb = 0;
    };

    std::vector<NamedColor> LoadColors(const std::filesystem::path& file);

    // Nearest palette name by squared RGB distance, as MTF's descriptions do.
    std::string NearestColorName(const std::vector<NamedColor>& palette, std::uint32_t rgb);
}
