#include "ui/Widgets.h"

#include "io/UiSettings.h"
#include "model/Catalogs.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    bool InputString(const char* label, std::string& value, const char* hint, int flags, std::size_t capacity) {
        std::vector<char> buf(std::max(capacity, value.size() + 1), '\0');
        std::copy(value.begin(), value.end(), buf.begin());
        const bool changed = hint ? InputTextWithHint(label, hint, buf.data(), buf.size(), flags) : InputText(label, buf.data(), buf.size(), flags);
        if (changed) {
            value = buf.data();
        }
        return changed;
    }

    bool ColorEditRgb(const char* label, std::uint32_t& rgb) {
        float c[3] = { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
        if (ColorEdit3(label, c, ImGuiColorEditFlags_DisplayHex)) {
            auto to = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            rgb = (to(c[0]) << 16) | (to(c[1]) << 8) | to(c[2]);
            return true;
        }
        return false;
    }

    bool ColorEditRgb(const char* label, std::uint32_t& rgb, const std::vector<model::NamedColor>& palette) {
        PushID(label);
        bool changed = ColorEditRgb(label, rgb);
        if (!palette.empty()) {
            SameLine();
            if (SmallButton("Palette")) {
                OpenPopup("##palette");
            }
            SameLine();
            TextDisabled("%s", model::NearestColorName(palette, rgb).c_str());
            if (BeginPopup("##palette")) {
                const float size = GetFrameHeight();
                const float maxX = GetCursorScreenPos().x + 10 * (size + GetStyle()->ItemSpacing.x);
                for (std::size_t i = 0; i < palette.size(); ++i) {
                    const auto& c = palette[i];
                    PushID(static_cast<int>(i));
                    const ImVec4 col{ ((c.rgb >> 16) & 0xFF) / 255.0f, ((c.rgb >> 8) & 0xFF) / 255.0f, (c.rgb & 0xFF) / 255.0f, 1.0f };
                    if (ColorButton(c.name.c_str(), col, 0, ImVec2{ size, size })) {
                        rgb = c.rgb & 0xFFFFFF;
                        changed = true;
                        CloseCurrentPopup();
                    }
                    PopID();
                    if (i + 1 < palette.size() && GetItemRectMax().x + size + GetStyle()->ItemSpacing.x < maxX) {
                        SameLine();
                    }
                }
                EndPopup();
            }
        }
        PopID();
        return changed;
    }

    bool ParamEditor(const char* id, const model::ParamSpec& spec, model::ParamValue& value, float width) {
        PushID(id);
        bool changed = false;
        SetNextItemWidth(width);
        const char* label = spec.label.empty() ? "##v" : spec.label.c_str();
        switch (spec.kind) {
            case model::ParamKind::Slider: {
                int v = static_cast<int>(model::ParamInt(value));
                const std::string fmt = model::SliderPrintfFormat(spec.format);
                if (SliderInt(label, &v, static_cast<int>(spec.min), static_cast<int>(spec.max), fmt.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
                    if (spec.step > 1) {
                        v = static_cast<int>(spec.min + ((v - spec.min + spec.step / 2) / spec.step) * spec.step);
                        v = static_cast<int>(std::clamp<std::int64_t>(v, spec.min, spec.max));
                    }
                    value = static_cast<std::int64_t>(v);
                    changed = true;
                }
                break;
            }
            case model::ParamKind::Menu: {
                const std::string preview = spec.Display(value);
                if (BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
                    const auto current = model::ParamStr(value);
                    for (const auto& o : spec.menu) {
                        const bool selected = model::IEquals(o.id, current);
                        if (Selectable((o.label + "##" + o.id).c_str(), selected)) {
                            value = o.id;
                            changed = true;
                        }
                        if (selected) {
                            SetItemDefaultFocus();
                        }
                    }
                    EndCombo();
                }
                break;
            }
            case model::ParamKind::Text: {
                std::string s = model::ParamStr(value);
                if (InputString(label, s, "comma-separated, empty = any")) {
                    value = s;
                    changed = true;
                }
                break;
            }
            case model::ParamKind::Color: {
                auto rgb = static_cast<std::uint32_t>(model::ParamInt(value));
                if (ColorEditRgb(label, rgb)) {
                    value = static_cast<std::int64_t>(rgb);
                    changed = true;
                }
                break;
            }
            default:
                break;
        }
        PopID();
        return changed;
    }

    void HelpMarker(const char* text) {
        SameLine();
        TextDisabled("(?)");
        if (BeginItemTooltip()) {
            PushTextWrapPos(GetFontSize() * 30.0f);
            TextUnformatted(text);
            PopTextWrapPos();
            EndTooltip();
        }
    }

    const ImVec4& SeverityColor(model::Severity s) {
        switch (s) {
            case model::Severity::Error: return kError;
            case model::Severity::Warning: return kWarn;
            default: return kMuted;
        }
    }

    void SeverityText(model::Severity s, const std::string& text) {
        const char* tag = s == model::Severity::Error ? "Error" : s == model::Severity::Warning ? "Warning" : "Note";
        PushStyleColor(ImGuiCol_Text, SeverityColor(s));
        TextUnformatted(tag);
        PopStyleColor();
        SameLine();
        PushTextWrapPos(0.0f);
        TextUnformatted(text.c_str());
        PopTextWrapPos();
    }

    bool SearchBox(const char* id, std::string& text, float width, const char* hint) {
        SetNextItemWidth(width);
        return InputString(id, text, hint, 0, 128);
    }

    NamePromptResult NamePrompt(std::string& displayName, const std::string& fileName, bool canConfirm, const char* confirmLabel) {
        if (IsWindowAppearing()) {
            SetKeyboardFocusHere();
        }
        SetNextItemWidth(280);
        const bool enter = InputString("##name", displayName, "Display name", ImGuiInputTextFlags_EnterReturnsTrue, 64);
        TextDisabled("File: %s.json", fileName.c_str());
        const bool ok = canConfirm && !displayName.empty();
        auto result = NamePromptResult::Open;
        BeginDisabled(!ok);
        if (Button(confirmLabel, ImVec2{ 100, 0 }) || (enter && ok)) {
            result = NamePromptResult::Confirmed;
        }
        EndDisabled();
        SameLine();
        if (Button("Cancel", ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
            result = NamePromptResult::Cancelled;
        }
        return result;
    }

}
