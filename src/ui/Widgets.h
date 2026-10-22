#pragma once

#include "model/Lint.h"
#include "model/PluginCatalog.h"

namespace mtfui::ui {
    inline constexpr ImGuiMCP::ImVec4 kGood{ 0.45f, 0.85f, 0.45f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kWarn{ 1.0f, 0.75f, 0.3f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kError{ 1.0f, 0.4f, 0.4f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kMuted{ 0.6f, 0.6f, 0.6f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kAccent{ 0.55f, 0.75f, 1.0f, 1.0f };

    bool InputString(const char* label, std::string& value, const char* hint = nullptr, int flags = 0, std::size_t capacity = 256);
    bool ColorEditRgb(const char* label, std::uint32_t& rgb);
    // Edits one catalog parameter, converting between the int/string storage forms.
    bool ParamEditor(const char* id, const model::ParamSpec& spec, model::ParamValue& value, float width);
    void HelpMarker(const char* text);
    void SeverityText(model::Severity s, const std::string& text);
    const ImGuiMCP::ImVec4& SeverityColor(model::Severity s);
    // Filter box; returns true when text changed.
    bool SearchBox(const char* id, std::string& text, float width, const char* hint = "Search");
    bool MatchesSearch(const std::string& haystack, const std::string& needle);
}
