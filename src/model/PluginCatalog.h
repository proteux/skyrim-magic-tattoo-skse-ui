#pragma once

#include "model/Preset.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mtfui::model {
    enum class ParamKind { None, Slider, Menu, Text, Color };

    struct MenuOption {
        std::string id;
        std::string label;
    };

    struct ParamSpec {
        ParamKind kind = ParamKind::None;
        std::string label;
        std::int64_t min = 0;
        std::int64_t max = 100;
        std::int64_t step = 1;
        std::string format;  // "{0} m"
        ParamValue def = std::int64_t{ 0 };
        std::vector<MenuOption> menu;

        bool IsString() const { return kind == ParamKind::Menu || kind == ParamKind::Text; }
        std::string Display(const ParamValue& v) const;
        // Coerces a stored value to the type this spec writes.
        ParamValue Coerce(const ParamValue& v) const;
    };

    struct CatalogEntry {
        std::string id;
        std::string label;
        std::string description;  // "{param1}" placeholders
        std::string kind;  // effects: "burst" or empty (toggle)
        std::array<ParamSpec, 5> params;
        int ParamCount() const;
    };

    struct PluginCatalog {
        std::string pluginId;
        std::string label;
        std::filesystem::path file;
        std::vector<CatalogEntry> conditions;
        std::vector<CatalogEntry> effects;
    };

    std::optional<PluginCatalog> LoadPluginCatalog(const std::filesystem::path& file);

    // Splits "plugin:id" into its halves.
    std::pair<std::string, std::string> SplitKey(const std::string& key);

    // Fills "{param1}".."{param5}" with displayed values.
    std::string FillDescription(const CatalogEntry& entry, const std::array<ParamValue, 5>& values);
}
