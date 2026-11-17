#include "game/MtfBridge.h"
#include "game/SpellbookBridge.h"
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
        std::array<bool, model::kSpellbookSlots> g_added{};  // empty spells opened with New, shown until removed or reverted
        int g_scrollTo = -1;

        void ReadKnown(App& app) {
            if (!app.inGame || g_readSession == game::MtfBridge::Session()) {
                return;
            }
            g_readSession = game::MtfBridge::Session();
            game::SpellbookBridge::Get().ReadKnown([&app](game::KnownSpells known) {
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
                    const auto& label = p.displayName.empty() ? p.name : p.displayName;
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
            cost = std::clamp(cost, 0, model::kSpellbookCostMax);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_CostTip"));
            }
        }

        // Width of the Known/Learn/Forget cluster so the name field can leave room for it.
        float KnownTrailWidth(App& app, int index) {
            const float frame = GetStyle()->FramePadding.x * 2.0f;
            const float gap = GetStyle()->ItemSpacing.x;
            if (!app.inGame) {
                return CalcTextSize(T("$MTFUI_Spells_NoGame")).x;
            }
            if (!app.spellsKnown) {
                return CalcTextSize(T("$MTFUI_Spells_Reading")).x;
            }
            if ((*app.spellsKnown)[index] >= 0) {
                return CalcTextSize(T("$MTFUI_Spells_Known")).x + gap + CalcTextSize(T("$MTFUI_Spells_Forget")).x + frame;
            }
            return CalcTextSize(T("$MTFUI_Spells_NotKnown")).x + gap + CalcTextSize(T("$MTFUI_Spells_Learn")).x + frame;
        }

        // Known or not, with Learn or Forget. ready: the spell's saved setup can be learned as it is.
        void KnownCell(App& app, int index, bool ready, const char* notReady) {
            const auto name = game::SpellbookBridge::SpellName(game::SpellbookBridge::Get().Config(), index);
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
                    game::SpellbookBridge::Get().Forget(index, done);
                    app.Toast(TF("$MTFUI_Toast_SpellForgotten", name));
                }
                return;
            }
            TextDisabled("%s", T("$MTFUI_Spells_NotKnown"));
            SameLine();
            BeginDisabled(!ready);
            if (SmallButton(T("$MTFUI_Spells_Learn"))) {
                game::SpellbookBridge::Get().Learn(index, done);
                app.Toast(TF("$MTFUI_Toast_SpellLearned", name));
            }
            EndDisabled();
            if (!ready && IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                SetTooltip("%s", notReady);
            }
        }

        // A bordered box around one spell, as tall as its contents. Pair with EndCard.
        bool BeginCard(const char* id) {
            PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 6.0f, 4.0f });
            PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 8.0f, 6.0f });
            const bool open = BeginChild(id, ImVec2{ 0, 0 }, ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            PopStyleVar();  // WindowPadding is applied at BeginChild; keep ItemSpacing for the body
            return open;
        }

        void EndCard() {
            EndChild();
            PopStyleVar();  // ItemSpacing from BeginCard
        }

        void Caption(const char* key) {
            TextDisabled("%s", T(key));
        }

        // Cast and school side by side so a narrow card still fits both.
        void CastAndSchool(model::CastType& cast, model::School& school, int skipCast = -1) {
            const float gap = GetStyle()->ItemSpacing.x;
            const float half = std::max(60.0f, (GetContentRegionAvail().x - gap) * 0.5f);
            SetNextItemWidth(half);
            KeyCombo("##cast", cast, kCastKeys, skipCast);
            SameLine(0.0f, gap);
            SetNextItemWidth(-1.0f);
            KeyCombo("##school", school, kSchoolKeys);
        }

        void KindRadios(bool& hostile) {
            int kind = hostile ? 0 : 1;
            RadioButton(T("$MTFUI_Spells_Hex"), &kind, 0);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_HexTip"));
            }
            SameLine();
            RadioButton(T("$MTFUI_Spells_Blessing"), &kind, 1);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_BlessingTip"));
            }
            hostile = kind == 0;
        }

        void DecayRow(bool& permanent, double& decay, model::CastType cast) {
            Checkbox(T("$MTFUI_Spells_Permanent"), &permanent);
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_PermanentTip"));
            }
            if (permanent) {
                return;
            }
            SameLine(0.0f, 8.0f);
            SetNextItemWidth(-1.0f);
            float value = static_cast<float>(decay);
            if (SliderFloat("##decay", &value, static_cast<float>(model::kDecayMin), 3600.0f, T("$MTFUI_Spells_Decay_Fmt"), ImGuiSliderFlags_Logarithmic)) {
                decay = std::clamp(static_cast<double>(std::round(value)), model::kDecayMin, model::kDecayMax);
            }
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", model::IsChanneled(cast) ? T("$MTFUI_Spells_DecayChannelTip") : T("$MTFUI_Spells_DecayTip"));
            }
        }

        // Name with Learn/Forget on the same row.
        void CardHeader(App& app, int index, std::string& name, const char* hint, bool ready, const char* notReady) {
            SetNextItemWidth(-(KnownTrailWidth(app, index) + GetStyle()->ItemSpacing.x));
            InputString("##name", name, hint, 0, 64);
            SameLine();
            KnownCell(app, index, ready, notReady);
        }

        bool RemoveButton() {
            Dummy(ImVec2{ 0.0f, 8.0f });
            const bool clicked = Button(T("$MTFUI_Spells_Remove"), ImVec2{ -1.0f, 0.0f });
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", T("$MTFUI_Spells_RemoveTip"));
            }
            Dummy(ImVec2{ 0.0f, 2.0f });
            return clicked;
        }

        void SlotFields(App& app, model::SpellbookSlot& s) {
            Caption("$MTFUI_Spells_ColPreset");
            SetNextItemWidth(-1.0f);
            PresetCombo(app, s.preset);
            KindRadios(s.hostile);
            CastAndSchool(s.cast, s.school);
            CostSlider(s.cost, s.cast);
            DecayRow(s.permanent, s.decay, s.cast);
        }

        // Returns true when the spell is removed.
        bool SlotCard(App& app, int i, model::SpellbookSlot& s, const model::SpellbookSlot& saved, const model::SpellbookConfig& draft) {
            PushID(i);
            bool removed = false;
            if (BeginCard("##card")) {
                const bool ready = !saved.preset.empty() && s == saved;
                CardHeader(app, i, s.name, TF("$MTFUI_Spells_DefaultName", i + 1).c_str(), ready,
                    saved.preset.empty() ? T("$MTFUI_Spells_LearnNeedsPreset") : T("$MTFUI_Spells_LearnNeedsSave"));
                SlotFields(app, s);
                if (const int other = model::DuplicatePresetSlot(draft, i); other >= 0) {
                    TextColored(kWarn, "%s", TF("$MTFUI_Spells_DuplicatePreset", other + 1).c_str());
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        SetTooltip("%s", T("$MTFUI_Spells_DuplicatePresetTip"));
                    }
                }
                removed = RemoveButton();
            }
            EndCard();
            PopID();
            return removed;
        }

        void DispelCard(App& app, model::SpellbookDispel& d, const model::SpellbookDispel& saved) {
            PushID("dispel");
            if (BeginCard("##card")) {
                CardHeader(app, game::kSpellbookDispel, d.name, T("$MTFUI_Spells_DispelDefaultName"), d == saved, T("$MTFUI_Spells_LearnNeedsSave"));
                TextWrapped("%s", T("$MTFUI_Spells_DispelAbout"));
                PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{ 8.0f, 2.0f });
                if (BeginTable("##dispel_fields", 2, ImGuiTableFlags_SizingStretchSame)) {
                    TableNextColumn();
                    Caption("$MTFUI_Spells_ColCasting");
                    CastAndSchool(d.cast, d.school, static_cast<int>(model::CastType::SelfChannel));
                    TableNextColumn();
                    Caption("$MTFUI_Spells_ColCostOnly");
                    CostSlider(d.cost, d.cast);
                    EndTable();
                }
                PopStyleVar();
            }
            EndCard();
            PopID();
        }

        // Empty slot at the end of the grid; click to open the next free spell.
        bool NewSpellCard() {
            PushID("new");
            bool clicked = false;
            if (BeginCard("##card")) {
                Dummy(ImVec2{ 0.0f, 12.0f });
                clicked = Button(T("$MTFUI_Spells_New"), ImVec2{ -1.0f, 0.0f });
                Dummy(ImVec2{ 0.0f, 12.0f });
            }
            EndCard();
            PopID();
            return clicked;
        }

        void Gap() {
            Dummy(ImVec2{ 0.0f, 4.0f });
        }
    }

    void DrawSpellsTab(App& app) {
        auto& bridge = game::SpellbookBridge::Get();
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

        int shown = 0;
        for (int i = 0; i < model::kSpellbookSlots; ++i) {
            if (g_added[i] || draft.slots[i] != model::SpellbookSlot{}) {
                ++shown;
            }
        }
        const bool full = shown >= model::kSpellbookSlots;
        const bool dirty = draft != saved;

        // List scrolls above a fixed bar: top border, padding, Revert / Save.
        constexpr float kBarPad = 8.0f;
        const float footer = GetStyle()->ItemSpacing.y + 1.0f + kBarPad + GetFrameHeight() + kBarPad;
        if (!BeginChild("##spells_list", ImVec2{ 0, -footer })) {
            EndChild();
            return;
        }
        TextWrapped("%s", T("$MTFUI_Spells_Intro"));
        Gap();

        SeparatorText(T("$MTFUI_Spells_DispelSection"));
        DispelCard(app, draft.dispel, saved.dispel);
        Gap();

        SeparatorText(T("$MTFUI_Spells_SpellsSection"));
        PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{ 6.0f, 6.0f });
        if (BeginTable("##spell_grid", 3, ImGuiTableFlags_SizingStretchSame)) {
            for (int i = 0; i < model::kSpellbookSlots; ++i) {
                if (!g_added[i] && draft.slots[i] == model::SpellbookSlot{}) {
                    continue;
                }
                TableNextColumn();
                if (SlotCard(app, i, draft.slots[i], saved.slots[i], draft)) {
                    draft.slots[i] = {};
                    g_added[i] = false;
                }
                if (i == g_scrollTo) {
                    SetScrollHereY(1.0f);
                    g_scrollTo = -1;
                }
            }
            if (!full) {
                TableNextColumn();
                if (NewSpellCard()) {
                    for (int i = 0; i < model::kSpellbookSlots; ++i) {
                        if (!g_added[i] && draft.slots[i] == model::SpellbookSlot{}) {
                            g_added[i] = true;
                            g_scrollTo = i;
                            break;
                        }
                    }
                }
            }
            EndTable();
        }
        PopStyleVar();
        EndChild();

        Separator();
        Dummy(ImVec2{ 0, kBarPad });
        BeginDisabled(!dirty);
        if (Button(T("$MTFUI_Spells_Revert"))) {
            draft = saved;
            g_added.fill(false);
        }
        EndDisabled();
        SameLine();
        const std::string count = TF("$MTFUI_Spells_Count", shown, model::kSpellbookSlots);
        TextDisabled("%s", count.c_str());

        const char* saveLabel = T("$MTFUI_Spells_Save");
        const char* unsaved = T("$MTFUI_Spells_Unsaved");
        const float gap = GetStyle()->ItemSpacing.x;
        const float saveW = CalcTextSize(saveLabel).x + GetStyle()->FramePadding.x * 2.0f;
        float rightW = saveW;
        if (dirty) {
            rightW += gap + CalcTextSize(unsaved).x;
        }
        SameLine();
        SetCursorPosX(std::max(GetCursorPosX(), GetWindowContentRegionMax().x - rightW));
        if (dirty) {
            TextColored(kWarn, "%s", unsaved);
            SameLine(0.0f, gap);
        }
        BeginDisabled(!dirty);
        if (Button(saveLabel)) {
            std::string error;
            if (bridge.Save(draft, &error, [&app](game::KnownSync sync) {
                    app.spellsKnown = sync.known;
                    if (sync.forgotten > 0) {
                        app.Toast(sync.forgotten == 1 ? std::string(T("$MTFUI_Toast_SpellForgottenOne"))
                                                      : TF("$MTFUI_Toast_SpellForgottenN", sync.forgotten),
                            true);
                    }
                    if (sync.swapped > 0) {
                        app.Toast(sync.swapped == 1 ? std::string(T("$MTFUI_Toast_SpellSwappedOne"))
                                                   : TF("$MTFUI_Toast_SpellSwappedN", sync.swapped));
                    }
                })) {
                app.Toast(T("$MTFUI_Toast_SpellsSaved"));
            } else {
                app.Toast(TF("$MTFUI_Toast_SpellsSaveFailed", error), true);
            }
        }
        EndDisabled();
        Dummy(ImVec2{ 0, kBarPad });
    }
}
