#include "game/MtfBridge.h"
#include "game/TacticalBridge.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        constexpr const char* kCastKeys[] = { "$MTFUI_Spells_CastSelf", "$MTFUI_Spells_CastSelfChannel", "$MTFUI_Spells_CastTouch", "$MTFUI_Spells_CastRanged" };
        constexpr const char* kSchoolKeys[] = { "$MTFUI_Spells_Alteration", "$MTFUI_Spells_Conjuration", "$MTFUI_Spells_Destruction", "$MTFUI_Spells_Illusion",
            "$MTFUI_Spells_Restoration" };

        int g_lastFrame = -100;
        int g_readSession = -1;  // a read of the known spells is under way in this session

        void ReadKnown(App& app) {
            if (!app.inGame || g_readSession == game::MtfBridge::Session()) {
                return;
            }
            g_readSession = game::MtfBridge::Session();
            game::TacticalBridge::Get().ReadKnown([&app](game::KnownSpells known) {
                g_readSession = -1;
                app.spellsKnown = known;
            });
        }

        // A combo over translated labels. skip: a value the spell doesn't offer.
        template <class E, std::size_t N>
        bool KeyCombo(const char* id, E& value, const char* const (&keys)[N], int skip = -1) {
            bool changed = false;
            const int current = static_cast<int>(value);
            if (BeginCombo(id, T(keys[std::clamp(current, 0, static_cast<int>(N) - 1)]))) {
                for (int i = 0; i < static_cast<int>(N); ++i) {
                    if (i == skip) {
                        continue;
                    }
                    if (Selectable(T(keys[i]), i == current)) {
                        value = static_cast<E>(i);
                        changed = true;
                    }
                }
                EndCombo();
            }
            return changed;
        }

        bool PresetCombo(App& app, std::string& preset) {
            bool changed = false;
            const auto* info = preset.empty() ? nullptr : app.FindLibrary(preset);
            const std::string preview = preset.empty() ? T("$MTFUI_Spells_NoPreset") : info && !info->displayName.empty() ? info->displayName : preset;
            if (BeginCombo("##preset", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
                if (Selectable(T("$MTFUI_Spells_NoPreset"), preset.empty())) {
                    preset.clear();
                    changed = true;
                }
                Separator();
                auto item = [&](const model::PresetFileInfo& p) {
                    PushID(p.name.c_str());
                    const auto label = p.displayName.empty() || p.displayName == p.name ? p.name : p.displayName + "  (" + p.name + ")";
                    if (MenuItem(label.c_str(), nullptr, model::IEquals(p.name, preset))) {
                        preset = p.name;
                        changed = true;
                    }
                    PopID();
                };
                // Same layout as the library: a submenu per group, then ungrouped presets, then built-ins.
                for (const auto& group : app.Groups()) {
                    const bool any = std::any_of(app.library.begin(), app.library.end(),
                        [&](const model::PresetFileInfo& p) { return !p.builtin && model::IEquals(p.group, group); });
                    if (!any) {
                        continue;
                    }
                    const bool holds = info && !info->builtin && model::IEquals(info->group, group);
                    PushID(("grp_" + io::ToLower(group)).c_str());
                    if (BeginMenu(holds ? (group + "  *").c_str() : group.c_str())) {
                        for (const auto& p : app.library) {
                            if (!p.builtin && model::IEquals(p.group, group)) {
                                item(p);
                            }
                        }
                        ImGuiMCP::EndMenu();
                    }
                    PopID();
                }
                for (const auto& p : app.library) {
                    if (!p.builtin && p.group.empty()) {
                        item(p);
                    }
                }
                if (std::any_of(app.library.begin(), app.library.end(), [](const model::PresetFileInfo& p) { return p.builtin; })) {
                    Separator();
                    const std::string builtins = T("$MTFUI_Library_BuiltinMenu");
                    if (BeginMenu(info && info->builtin ? (builtins + "  *").c_str() : builtins.c_str())) {
                        for (const auto& p : app.library) {
                            if (p.builtin) {
                                item(p);
                            }
                        }
                        ImGuiMCP::EndMenu();
                    }
                }
                EndCombo();
            }
            if (!preset.empty() && !info) {
                TextColored(kWarn, "%s", T("$MTFUI_Spells_PresetMissing"));
            }
            return changed;
        }

        void CostSlider(int& cost, model::CastType cast) {
            SetNextItemWidth(-1.0f);
            SliderInt("##cost", &cost, 0, 300, model::IsChanneled(cast) ? T("$MTFUI_Spells_CostPerSecond_Fmt") : T("$MTFUI_Spells_Cost_Fmt"));
            cost = std::clamp(cost, 0, model::kTacticalCostMax);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_CostTip"));
            }
        }

        // Known or not, with Learn or Forget. ready: the spell's saved setup can be learned as it is.
        void KnownCell(App& app, int index, bool ready, const char* notReady) {
            const auto name = game::TacticalBridge::SpellName(game::TacticalBridge::Get().Config(), index);
            if (!app.inGame) {
                TextDisabled("%s", T("$MTFUI_Spells_NoGame"));
                return;
            }
            if (!app.spellsKnown) {
                TextDisabled("%s", T("$MTFUI_Spells_Reading"));
                return;
            }
            auto done = [&app](game::KnownSpells known) { app.spellsKnown = known; };
            if ((*app.spellsKnown)[index] >= 0) {
                TextColored(kGood, "%s", T("$MTFUI_Spells_Known"));
                SameLine();
                if (SmallButton(T("$MTFUI_Spells_Forget"))) {
                    game::TacticalBridge::Get().Forget(index, done);
                    app.Toast(TF("$MTFUI_Toast_SpellForgotten", name));
                }
                return;
            }
            TextDisabled("%s", T("$MTFUI_Spells_NotKnown"));
            SameLine();
            BeginDisabled(!ready);
            if (SmallButton(T("$MTFUI_Spells_Learn"))) {
                game::TacticalBridge::Get().Learn(index, done);
                app.Toast(TF("$MTFUI_Toast_SpellLearned", name));
            }
            EndDisabled();
            if (!ready && IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                SetTooltip("%s", notReady);
            }
        }

        void SlotRow(App& app, int i, model::TacticalSlot& s, const model::TacticalSlot& saved) {
            PushID(i);
            TableNextRow();

            TableNextColumn();
            SetNextItemWidth(-1.0f);
            InputString("##name", s.name, TF("$MTFUI_Spells_DefaultName", i + 1).c_str(), 0, 64);
            const bool ready = !saved.preset.empty() && s == saved;
            KnownCell(app, i, ready, saved.preset.empty() ? T("$MTFUI_Spells_LearnNeedsPreset") : T("$MTFUI_Spells_LearnNeedsSave"));

            TableNextColumn();
            SetNextItemWidth(-1.0f);
            PresetCombo(app, s.preset);
            int kind = s.hostile ? 0 : 1;
            RadioButton(T("$MTFUI_Spells_Hex"), &kind, 0);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_HexTip"));
            }
            SameLine();
            RadioButton(T("$MTFUI_Spells_Blessing"), &kind, 1);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_BlessingTip"));
            }
            s.hostile = kind == 0;

            TableNextColumn();
            SetNextItemWidth(-1.0f);
            KeyCombo("##cast", s.cast, kCastKeys);
            SetNextItemWidth(-1.0f);
            KeyCombo("##school", s.school, kSchoolKeys);

            TableNextColumn();
            CostSlider(s.cost, s.cast);
            Checkbox(T("$MTFUI_Spells_Permanent"), &s.permanent);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_PermanentTip"));
            }
            if (!s.permanent) {
                SetNextItemWidth(-1.0f);
                float decay = static_cast<float>(s.decay);
                if (SliderFloat("##decay", &decay, static_cast<float>(model::kDecayMin), 3600.0f, T("$MTFUI_Spells_Decay_Fmt"), ImGuiSliderFlags_Logarithmic)) {
                    s.decay = std::clamp(static_cast<double>(std::round(decay)), model::kDecayMin, model::kDecayMax);
                }
                if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    SetTooltip("%s", model::IsChanneled(s.cast) ? T("$MTFUI_Spells_DecayChannelTip") : T("$MTFUI_Spells_DecayTip"));
                }
            }
            PopID();
        }

        void DispelRow(App& app, model::TacticalDispel& d, const model::TacticalDispel& saved) {
            PushID("dispel");
            TableNextRow();
            TableNextColumn();
            SetNextItemWidth(-1.0f);
            InputString("##name", d.name, T("$MTFUI_Spells_DispelDefaultName"), 0, 64);
            KnownCell(app, game::kTacticalDispel, d == saved, T("$MTFUI_Spells_LearnNeedsSave"));

            TableNextColumn();
            TextWrapped("%s", T("$MTFUI_Spells_DispelAbout"));

            TableNextColumn();
            SetNextItemWidth(-1.0f);
            KeyCombo("##cast", d.cast, kCastKeys, static_cast<int>(model::CastType::SelfChannel));
            SetNextItemWidth(-1.0f);
            KeyCombo("##school", d.school, kSchoolKeys);

            TableNextColumn();
            CostSlider(d.cost, d.cast);
            PopID();
        }

        bool BeginSpellTable(const char* id) {
            if (!BeginTable(id, 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                return false;
            }
            TableSetupColumn(T("$MTFUI_Spells_ColSpell"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
            TableSetupColumn(T("$MTFUI_Spells_ColPreset"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
            TableSetupColumn(T("$MTFUI_Spells_ColCasting"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            TableSetupColumn(T("$MTFUI_Spells_ColCost"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
            TableHeadersRow();
            return true;
        }
    }

    void DrawSpellsTab(App& app) {
        auto& bridge = game::TacticalBridge::Get();
        const auto& saved = bridge.Config();
        if (!app.spellsDraft) {
            app.spellsDraft = saved;
        }
        auto& draft = *app.spellsDraft;
        // Opening the tab rereads what the player knows: the console or another mod may have changed it.
        if (GetFrameCount() - g_lastFrame > 1) {
            app.spellsKnown.reset();
        }
        g_lastFrame = GetFrameCount();
        if (!app.spellsKnown) {
            ReadKnown(app);
        }

        TextWrapped("%s", T("$MTFUI_Spells_Intro"));
        const bool dirty = draft != saved;
        BeginDisabled(!dirty);
        if (Button(T("$MTFUI_Spells_Save"))) {
            std::string error;
            if (bridge.Save(draft, &error, [&app](game::KnownSpells known) { app.spellsKnown = known; })) {
                app.Toast(T("$MTFUI_Toast_SpellsSaved"));
            } else {
                app.Toast(TF("$MTFUI_Toast_SpellsSaveFailed", error), true);
            }
        }
        SameLine();
        if (Button(T("$MTFUI_Spells_Revert"))) {
            draft = saved;
        }
        EndDisabled();
        if (dirty) {
            SameLine();
            TextColored(kWarn, "%s", T("$MTFUI_Spells_Unsaved"));
        }
        Spacing();

        if (BeginSpellTable("##dispel")) {
            DispelRow(app, draft.dispel, saved.dispel);
            EndTable();
        }
        Spacing();
        if (BeginSpellTable("##spells")) {
            for (int i = 0; i < model::kTacticalSlots; ++i) {
                SlotRow(app, i, draft.slots[i], saved.slots[i]);
            }
            EndTable();
        }
    }
}
