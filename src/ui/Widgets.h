#pragma once

#include "io/Translations.h"
#include "model/Colors.h"
#include "model/Lint.h"
#include "model/PluginCatalog.h"
#include "model/Text.h"

namespace mtfui::ui {
    inline constexpr ImGuiMCP::ImVec4 kGood{ 0.45f, 0.85f, 0.45f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kWarn{ 1.0f, 0.75f, 0.3f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kError{ 1.0f, 0.4f, 0.4f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kMuted{ 0.6f, 0.6f, 0.6f, 1.0f };
    inline constexpr ImGuiMCP::ImVec4 kAccent{ 0.55f, 0.75f, 1.0f, 1.0f };

    bool InputString(const char* label, std::string& value, const char* hint = nullptr, int flags = 0, std::size_t capacity = 256);
    bool ColorEditRgb(const char* label, std::uint32_t& rgb);
    // Label and help marker on one line, then hex box (inputWidth wide), swatch, Palette button, nearest palette name.
    bool ColorEditRgb(const char* label, std::uint32_t& rgb, const std::vector<model::NamedColor>& palette, float inputWidth, const char* help = nullptr);
    // Edits one catalog parameter, converting between the int/string storage forms.
    bool ParamEditor(const char* id, const model::ParamSpec& spec, model::ParamValue& value, float width);
    void HelpMarker(const char* text);
    void SeverityText(model::Severity s, const std::string& text);
    const ImGuiMCP::ImVec4& SeverityColor(model::Severity s);
    // Thumbnail size slider labelled from small to large rather than in pixels.
    bool ThumbSizeSlider(const char* label, int& size);
    // Filter box; returns true when text changed.
    bool SearchBox(const char* id, std::string& text, float width, const char* hint = nullptr);  // nullptr: "Search"

    enum class NamePromptResult { Open, Confirmed, Cancelled };
    // Body of a "name this preset" modal: the display name box, the file it will get, and Create/Cancel. Call
    // between BeginPopupModal and EndPopup; close the popup on Confirmed or Cancelled.
    NamePromptResult NamePrompt(std::string& displayName, const std::string& fileName, bool canConfirm = true, const char* confirmLabel = nullptr);  // nullptr: "Create"
    using model::MatchesSearch;
}
