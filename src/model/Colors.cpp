#include "model/Colors.h"

#include "io/JsonStore.h"
#include "model/Preset.h"

#include <limits>

namespace mtfui::model {
    std::vector<NamedColor> LoadColors(const std::filesystem::path& file) {
        std::vector<NamedColor> out;
        auto j = io::ReadJson(file);
        if (!j || !j->is_object()) {
            return out;
        }
        if (auto it = j->find("colors"); it != j->end() && it->is_array()) {
            for (const auto& c : *it) {
                if (c.is_object()) {
                    out.push_back({ c.value("name", std::string{}), ParseHexColor(c.value("hex", Json{}), 0) });
                }
            }
        }
        return out;
    }

    std::string NearestColorName(const std::vector<NamedColor>& palette, std::uint32_t rgb) {
        if (palette.empty()) {
            return FormatHexColor(rgb);
        }
        const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        int best = std::numeric_limits<int>::max();
        const NamedColor* hit = &palette.front();
        for (const auto& c : palette) {
            const int dr = r - static_cast<int>((c.rgb >> 16) & 0xFF);
            const int dg = g - static_cast<int>((c.rgb >> 8) & 0xFF);
            const int db = b - static_cast<int>(c.rgb & 0xFF);
            const int d = dr * dr + dg * dg + db * db;
            if (d < best) {
                best = d;
                hit = &c;
            }
        }
        return hit->name;
    }
}
