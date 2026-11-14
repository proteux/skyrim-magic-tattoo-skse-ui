#include "ui/Widgets.h"

#include "game/SpellLookup.h"
#include "io/UiSettings.h"
#include "model/Catalogs.h"

#include <cfloat>

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        enum SpellColumn { kColName, kColSchool, kColLevel, kColType, kColCost, kColTargets, kColCount };

        // Only one picker is open at a time, so they share this.
        struct SpellPickerState {
            std::string search;
            int school = -1;  // -1: every school
            bool knownOnly = false;
            bool reading = false;
            std::optional<std::unordered_set<RE::FormID>> known;
            std::vector<std::size_t> order;  // indices into SpellLookup::All(), in the table's sort order
        };

        SpellPickerState& Picker() {
            static SpellPickerState state;
            return state;
        }

        const char* SchoolName(int school) {
            static const char* keys[] = { "$MTFUI_Spells_Alteration", "$MTFUI_Spells_Conjuration", "$MTFUI_Spells_Destruction", "$MTFUI_Spells_Illusion",
                "$MTFUI_Spells_Restoration" };
            return school >= 0 && school < 5 ? T(keys[school]) : T("$MTFUI_Lookup_NoSchool");
        }

        const char* LevelName(int level) {
            static const char* keys[] = { "$MTFUI_Lookup_Novice", "$MTFUI_Lookup_Apprentice", "$MTFUI_Lookup_Adept", "$MTFUI_Lookup_Expert",
                "$MTFUI_Lookup_Master" };
            return level >= 0 && level < 5 ? T(keys[level]) : "";
        }

        const char* DeliveryName(int delivery) {
            static const char* keys[] = { "$MTFUI_Lookup_Self", "$MTFUI_Lookup_Touch", "$MTFUI_Lookup_Aimed", "$MTFUI_Lookup_TargetActor",
                "$MTFUI_Lookup_TargetLocation" };
            return delivery >= 0 && delivery < 5 ? T(keys[delivery]) : "";
        }

        std::string TypeText(const game::SpellEntry& e) {
            return TF("$MTFUI_Lookup_Type", T(e.concentration ? "$MTFUI_Lookup_Concentration" : "$MTFUI_Lookup_FireAndForget"), DeliveryName(e.delivery));
        }

        std::string CostText(const game::SpellEntry& e) {
            return e.concentration ? TF("$MTFUI_Lookup_CostPerSecond", e.cost) : std::to_string(e.cost);
        }

        const char* TargetsText(const game::SpellEntry& e) {
            return T(e.harmful ? "$MTFUI_Lookup_Harmful" : "$MTFUI_Lookup_Helpful");
        }

        const char* CaveatText(game::SpellCaveat caveat) {
            switch (caveat) {
                case game::SpellCaveat::Cloak: return T("$MTFUI_Lookup_CaveatCloak");
                case game::SpellCaveat::Summon: return T("$MTFUI_Lookup_CaveatSummon");
                case game::SpellCaveat::Corpse: return T("$MTFUI_Lookup_CaveatCorpse");
                case game::SpellCaveat::CasterOnly: return T("$MTFUI_Lookup_CaveatCasterOnly");
                case game::SpellCaveat::Ward: return T("$MTFUI_Lookup_CaveatWard");
                default: return nullptr;
            }
        }

        void SpellTooltip(const game::SpellEntry& e) {
            PushTextWrapPos(GetFontSize() * 28.0f);
            TextUnformatted(e.name.c_str());
            const std::string school = e.level < 0 ? std::string(SchoolName(e.school)) : TF("$MTFUI_Lookup_LevelSchool", LevelName(e.level), SchoolName(e.school));
            TextDisabled("%s", TF("$MTFUI_Lookup_TipSummary", school, TypeText(e), CostText(e)).c_str());
            Separator();
            for (const auto& fx : e.effects) {
                std::string line = fx.name;
                if (fx.magnitude > 0.0f) {
                    line += std::format(" {:g}", fx.magnitude);
                }
                if (fx.area > 0) {
                    line += ", " + TF("$MTFUI_Lookup_EffectArea", fx.area);
                }
                if (fx.duration > 0) {
                    line += ", " + TF("$MTFUI_Lookup_EffectDuration", fx.duration);
                }
                BulletText("%s", line.c_str());
            }
            Separator();
            TextWrapped("%s", T(e.concentration ? "$MTFUI_Lookup_TipConcentration" : "$MTFUI_Lookup_TipFireAndForget"));
            TextWrapped("%s", T(e.harmful ? "$MTFUI_Lookup_TipHarmful" : "$MTFUI_Lookup_TipHelpful"));
            if (const char* caveat = CaveatText(e.caveat)) {
                PushStyleColor(ImGuiCol_Text, kWarn);
                TextWrapped("%s", caveat);
                PopStyleColor();
            }
            TextDisabled("%s", TF("$MTFUI_Lookup_TipSource", e.value).c_str());
            PopTextWrapPos();
        }

        void SortSpells(std::vector<std::size_t>& order, const std::vector<game::SpellEntry>& all, int column, bool descending) {
            order.resize(all.size());
            for (std::size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            auto key = [column](const game::SpellEntry& e) -> int {
                switch (column) {
                    case kColSchool: return e.school;
                    case kColLevel: return e.level;
                    case kColType: return (e.concentration ? 10 : 0) + e.delivery;
                    case kColCost: return e.cost;
                    case kColTargets: return e.harmful ? 1 : 0;
                    default: return 0;
                }
            };
            // All() is sorted by name, so a stable sort keeps equal keys in name order.
            std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
                if (column == kColName) {
                    return descending ? b < a : a < b;
                }
                const int ka = key(all[a]);
                const int kb = key(all[b]);
                return descending ? kb < ka : ka < kb;
            });
        }

        void RequestKnown(SpellPickerState& st) {
            st.reading = true;
            game::SpellLookup::Get().ReadKnown([](std::unordered_set<RE::FormID> known) {
                auto& s = Picker();
                s.known = std::move(known);
                s.reading = false;
            });
        }

        bool SpellRow(const game::SpellEntry& e, bool selected) {
            TableNextRow();
            TableNextColumn();
            const bool warn = e.caveat != game::SpellCaveat::None;
            if (warn) {
                PushStyleColor(ImGuiCol_Text, kWarn);
            }
            const bool clicked = Selectable((e.name + "##" + e.value).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns);
            if (warn) {
                PopStyleColor();
            }
            if (BeginItemTooltip()) {
                SpellTooltip(e);
                EndTooltip();
            }
            if (selected && IsWindowAppearing()) {
                SetScrollHereY();
            }
            TableNextColumn();
            TextUnformatted(e.school < 0 ? "" : SchoolName(e.school));
            TableNextColumn();
            TextUnformatted(LevelName(e.level));
            TableNextColumn();
            TextUnformatted(TypeText(e).c_str());
            TableNextColumn();
            TextUnformatted(CostText(e).c_str());
            TableNextColumn();
            TextColored(e.harmful ? kError : kGood, "%s", TargetsText(e));
            return clicked;
        }

        bool SpellPicker(const char* label, model::ParamValue& value) {
            const auto& lookup = game::SpellLookup::Get();
            const auto current = model::ParamStr(value);
            const auto* chosen = current.empty() ? nullptr : lookup.Find(current);
            const std::string preview = current.empty() ? std::string(T("$MTFUI_Lookup_None")) : chosen ? chosen->name : TF("$MTFUI_Lookup_Missing", current);
            SetNextWindowSizeConstraints(ImVec2{ 760, 0 }, ImVec2{ FLT_MAX, 560 });
            if (!BeginCombo(label, preview.c_str(), 0)) {
                if (chosen && BeginItemTooltip()) {
                    SpellTooltip(*chosen);
                    EndTooltip();
                }
                return false;
            }
            auto& st = Picker();
            if (IsWindowAppearing()) {
                st.search.clear();
                st.known.reset();
                st.reading = false;
                if (st.knownOnly) {
                    RequestKnown(st);
                }
                SetKeyboardFocusHere();
            }
            SearchBox("##search", st.search, 200);
            if (BeginItemTooltip()) {
                TextUnformatted(T("$MTFUI_Lookup_SearchTip"));
                EndTooltip();
            }
            SameLine();
            SetNextItemWidth(140);
            if (BeginCombo("##school", st.school < 0 ? T("$MTFUI_Lookup_AllSchools") : SchoolName(st.school), 0)) {
                if (Selectable(T("$MTFUI_Lookup_AllSchools"), st.school < 0)) {
                    st.school = -1;
                }
                for (int s = 0; s < 5; ++s) {
                    if (Selectable(SchoolName(s), st.school == s)) {
                        st.school = s;
                    }
                }
                EndCombo();
            }
            SameLine();
            if (Checkbox(T("$MTFUI_Lookup_KnownOnly"), &st.knownOnly) && st.knownOnly && !st.known && !st.reading) {
                RequestKnown(st);
            }
            if (BeginItemTooltip()) {
                TextUnformatted(T("$MTFUI_Lookup_KnownOnlyTip"));
                EndTooltip();
            }

            bool changed = false;
            int shown = 0;
            const auto& all = lookup.All();
            constexpr int flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
            if (st.knownOnly && st.reading) {
                TextDisabled("%s", T("$MTFUI_Lookup_Reading"));
            } else if (BeginTable("##spells", kColCount, flags, ImVec2{ 0, 400 })) {
                TableSetupScrollFreeze(0, 1);
                TableSetupColumn(T("$MTFUI_Lookup_ColSpell"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort);
                TableSetupColumn(T("$MTFUI_Lookup_ColSchool"), ImGuiTableColumnFlags_WidthFixed, 95.0f);
                TableSetupColumn(T("$MTFUI_Lookup_ColLevel"), ImGuiTableColumnFlags_WidthFixed, 85.0f);
                TableSetupColumn(T("$MTFUI_Lookup_ColType"), ImGuiTableColumnFlags_WidthFixed, 175.0f);
                TableSetupColumn(T("$MTFUI_Lookup_ColCost"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
                TableSetupColumn(T("$MTFUI_Lookup_ColTargets"), ImGuiTableColumnFlags_WidthFixed, 70.0f);
                TableHeadersRow();
                if (auto* specs = TableGetSortSpecs(); specs && (specs->SpecsDirty || st.order.size() != all.size())) {
                    const bool any = specs->SpecsCount > 0;
                    SortSpells(st.order, all, any ? specs->Specs[0].ColumnIndex : kColName, any && specs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
                    specs->SpecsDirty = false;
                }
                for (const std::size_t i : st.order) {
                    const auto& e = all[i];
                    if (st.school >= 0 && e.school != st.school) {
                        continue;
                    }
                    if (st.knownOnly && (!st.known || !st.known->contains(e.formId))) {
                        continue;
                    }
                    if (!st.search.empty()) {
                        const auto haystack = std::format("{} {} {} {} {} {}", e.name, SchoolName(e.school), LevelName(e.level), TypeText(e), TargetsText(e), e.plugin);
                        if (!model::MatchesSearch(haystack, st.search)) {
                            continue;
                        }
                    }
                    ++shown;
                    if (SpellRow(e, chosen == &e)) {
                        value = e.value;
                        changed = true;
                        CloseCurrentPopup();
                    }
                }
                EndTable();
                if (shown == 0) {
                    TextDisabled("%s", T("$MTFUI_Lookup_NoMatch"));
                }
            }
            TextDisabled("%s", TF("$MTFUI_Lookup_Count", shown).c_str());
            EndCombo();
            return changed;
        }
    }

    bool InputString(const char* label, std::string& value, const char* hint, int flags, std::size_t capacity) {
        std::vector<char> buf(std::max(capacity, value.size() + 1), '\0');
        std::copy(value.begin(), value.end(), buf.begin());
        const bool result = hint ? InputTextWithHint(label, hint, buf.data(), buf.size(), flags) : InputText(label, buf.data(), buf.size(), flags);
        // With EnterReturnsTrue, ImGui still writes every edit to buf but only returns true on Enter.
        if (value != buf.data()) {
            value = buf.data();
        }
        return result;
    }

    namespace {
        bool ColorEditFlags(const char* label, std::uint32_t& rgb, int flags) {
            float c[3] = { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
            if (ColorEdit3(label, c, flags)) {
                auto to = [](float f) { return static_cast<std::uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
                rgb = (to(c[0]) << 16) | (to(c[1]) << 8) | to(c[2]);
                return true;
            }
            return false;
        }
    }

    bool ColorEditRgb(const char* label, std::uint32_t& rgb) {
        return ColorEditFlags(label, rgb, ImGuiColorEditFlags_DisplayHex);
    }

    bool ColorEditRgb(const char* label, std::uint32_t& rgb, const std::vector<model::NamedColor>& palette, float inputWidth, const char* help) {
        PushID(label);
        TextUnformatted(label);
        if (help) {
            HelpMarker(help);
        }
        SetNextItemWidth(inputWidth);
        bool changed = ColorEditFlags("##hex", rgb, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoLabel);
        SameLine();
        changed |= ColorEditFlags("##swatch", rgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        if (!palette.empty()) {
            SameLine();
            if (SmallButton(T("$MTFUI_Color_Palette"))) {
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
                if (InputString(label, s, T("$MTFUI_Param_TextHint"))) {
                    value = s;
                    changed = true;
                }
                break;
            }
            case model::ParamKind::Lookup: {
                if (spec.lookup == "spell" && game::SpellLookup::Get().Ready()) {
                    changed = SpellPicker(label, value);
                } else {
                    std::string s = model::ParamStr(value);
                    if (InputString(label, s)) {
                        value = s;
                        changed = true;
                    }
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
        const char* tag = s == model::Severity::Error ? T("$MTFUI_Severity_Error") : s == model::Severity::Warning ? T("$MTFUI_Severity_Warning") : T("$MTFUI_Severity_Note");
        PushStyleColor(ImGuiCol_Text, SeverityColor(s));
        TextUnformatted(tag);
        PopStyleColor();
        SameLine();
        PushTextWrapPos(0.0f);
        TextUnformatted(text.c_str());
        PopTextWrapPos();
    }

    bool ThumbSizeSlider(const char* label, int& size) {
        const char* word = size < 112 ? T("$MTFUI_Thumb_Tiny_Fmt")
                           : size < 208 ? T("$MTFUI_Thumb_Small_Fmt")
                           : size < 320 ? T("$MTFUI_Thumb_Medium_Fmt")
                           : size < 432 ? T("$MTFUI_Thumb_Large_Fmt")
                                        : T("$MTFUI_Thumb_ExtraLarge_Fmt");
        return SliderInt(label, &size, io::kThumbSizeMin, io::kThumbSizeMax, word, ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
    }

    bool SearchBox(const char* id, std::string& text, float width, const char* hint) {
        SetNextItemWidth(width);
        return InputString(id, text, hint ? hint : T("$MTFUI_Common_Search"), 0, 128);
    }

    NamePromptResult NamePrompt(std::string& displayName, const std::string& fileName, bool canConfirm, const char* confirmLabel) {
        if (IsWindowAppearing()) {
            SetKeyboardFocusHere();
        }
        SetNextItemWidth(280);
        const bool enter = InputString("##name", displayName, T("$MTFUI_NamePrompt_Hint"), ImGuiInputTextFlags_EnterReturnsTrue, 64);
        TextDisabled("%s", TF("$MTFUI_NamePrompt_File", fileName).c_str());
        const bool ok = canConfirm && !displayName.empty();
        auto result = NamePromptResult::Open;
        BeginDisabled(!ok);
        if (Button(confirmLabel ? confirmLabel : T("$MTFUI_Common_Create"), ImVec2{ 100, 0 }) || (enter && ok)) {
            result = NamePromptResult::Confirmed;
        }
        EndDisabled();
        SameLine();
        if (Button(T("$MTFUI_Common_Cancel"), ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
            result = NamePromptResult::Cancelled;
        }
        return result;
    }

}
