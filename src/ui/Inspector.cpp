#include "game/MtfBridge.h"
#include "game/Targets.h"
#include "model/Describe.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        struct InspectorState {
            std::string pickerSearch;
            game::SkinInfo skin;  // the target's skin textures, drawn behind the tattoo preview
            RE::FormID skinFor = 0;
            bool skinLoading = false;
        };
        InspectorState g;

        bool PluginEnabled(const std::string& pid) {
            const auto& snap = game::MtfBridge::Get().Snapshot();
            auto it = snap.pluginEnabled.find(io::ToLower(pid));
            return it == snap.pluginEnabled.end() || it->second;
        }

        // Width taken by a SameLine'd SmallButton, including the spacing before it.
        float SmallButtonReserve(const char* label) {
            const auto* style = GetStyle();
            return CalcTextSize(label).x + style->FramePadding.x * 2.0f + style->ItemSpacing.x;
        }

        // Searchable, plugin-grouped picker for conditions or effects. Returns true when key changes.
        // Sized by the caller's PushItemWidth, which leaves room for the row's buttons.
        bool CatalogPicker(App& app, const char* id, std::string& key, bool conditions) {
            const model::PluginCatalog* owner = nullptr;
            const auto* current = conditions ? app.cat.FindCondition(key, &owner) : app.cat.FindEffect(key, &owner);
            std::string preview = key.empty() ? T("$MTFUI_Common_Choose") : current ? current->label : TF("$MTFUI_Picker_NotInstalled", key);
            bool changed = false;
            if (BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLargest)) {
                if (IsWindowAppearing()) {
                    g.pickerSearch.clear();
                    SetKeyboardFocusHere();
                }
                SearchBox("##pick", g.pickerSearch, -1.0f);
                for (const auto& plugin : app.cat.plugins) {
                    const auto& list = conditions ? plugin.conditions : plugin.effects;
                    bool header = false;
                    const bool enabled = PluginEnabled(plugin.pluginId);
                    for (const auto& e : list) {
                        if (!MatchesSearch(plugin.label + " " + e.label + " " + e.description, g.pickerSearch)) {
                            continue;
                        }
                        if (!header) {
                            SeparatorText(enabled ? plugin.label.c_str() : TF("$MTFUI_Picker_PluginDisabled", plugin.label).c_str());
                            header = true;
                        }
                        const std::string k = plugin.pluginId + ":" + e.id;
                        if (!enabled) {
                            PushStyleColor(ImGuiCol_Text, kMuted);
                        }
                        if (Selectable((e.label + "##" + k).c_str(), current == &e)) {
                            key = k;
                            changed = true;
                        }
                        if (!enabled) {
                            PopStyleColor();
                        }
                        if (IsItemHovered(ImGuiHoveredFlags_DelayShort) && !e.description.empty()) {
                            SetTooltip("%s", e.description.c_str());
                        }
                    }
                }
                EndCombo();
            }
            return changed;
        }

        void DrawConditionTab(App& app, model::Slot& s, int slot) {
            InputString(T("$MTFUI_Condition_TierName"), s.name, T("$MTFUI_Condition_TierNameHint"), 0, 33);
            HelpMarker(T("$MTFUI_Condition_TierNameHelp"));
            Spacing();
            if (s.conditions.size() > 1) {
                RadioButton(T("$MTFUI_Condition_All"), &s.op, 0);
                SameLine();
                RadioButton(T("$MTFUI_Condition_Any"), &s.op, 1);
            }
            int remove = -1;
            for (std::size_t i = 0; i < s.conditions.size(); ++i) {
                auto& c = s.conditions[i];
                PushID(static_cast<int>(i));
                if (i > 0) {
                    TextDisabled("%s", s.op == 1 ? T("$MTFUI_Condition_Or") : T("$MTFUI_Condition_And"));
                }
                BeginGroup();
                const float avail = GetContentRegionAvail().x;
                PushItemWidth(avail - SmallButtonReserve("x"));
                std::string key = c.pluginId;
                if (CatalogPicker(app, "##cond", key, true)) {
                    c.pluginId = key;
                    if (const auto* e = app.cat.FindCondition(key)) {
                        model::ApplyConditionDefaults(*e, c);
                    }
                }
                PopItemWidth();
                SameLine();
                if (SmallButton("x")) {
                    remove = static_cast<int>(i);
                }
                if (const auto* e = app.cat.FindCondition(c.pluginId)) {
                    model::ParamValue* values[] = { &c.param, &c.param2, &c.param3 };
                    for (int p = 0; p < 3; ++p) {
                        if (e->params[p].kind != model::ParamKind::None) {
                            // Only an edit writes back: viewing a tier must not change a stored "50" into 50.
                            auto v = e->params[p].Coerce(*values[p]);
                            if (ParamEditor(("p" + std::to_string(p)).c_str(), e->params[p], v, std::max(160.0f, avail * 0.55f))) {
                                *values[p] = v;
                            }
                        }
                    }
                    PushStyleColor(ImGuiCol_Text, kMuted);
                    TextWrapped("%s", TF("$MTFUI_Condition_Sentence", model::DescribeCondition(app.cat, c)).c_str());
                    PopStyleColor();
                }
                EndGroup();
                Separator();
                PopID();
            }
            if (remove >= 0) {
                s.conditions.erase(s.conditions.begin() + remove);
            }
            BeginDisabled(static_cast<int>(s.conditions.size()) >= model::kConditionCap);
            if (Button(s.conditions.empty() ? T("$MTFUI_Condition_Add") : T("$MTFUI_Condition_AddAnother"))) {
                s.conditions.push_back({});
            }
            EndDisabled();
            if (s.conditions.empty()) {
                SameLine();
                TextDisabled("%s", T("$MTFUI_Condition_NoneHint"));
            }
        }

        void EnsureSkin(App& app) {
            if (g.skinFor == app.targetId || g.skinLoading) {
                return;
            }
            g.skinLoading = true;
            const auto id = app.targetId;
            game::MtfBridge::RunOnGame([id] {
                auto skin = game::SkinFor(id);
                game::MtfBridge::PostToUi([id, skin] {
                    g.skin = skin;
                    g.skinFor = id;
                    g.skinLoading = false;
                });
            });
        }

        void DrawVisualChoice(App& app, model::Slot& s, int slot) {
            enum Mode { Inherit, None, Choose };
            int mode = Choose;
            if (slot > 0 && s.packId.empty()) {
                mode = Inherit;
            } else if (s.packId == model::kNoTexture || (slot == 0 && s.packId.empty())) {
                mode = None;
            }
            if (slot > 0) {
                if (RadioButton(T("$MTFUI_Look_SameAsDefault"), mode == Inherit)) {
                    s.packId.clear();
                    s.entryId.clear();
                }
                SameLine();
            }
            if (RadioButton(T("$MTFUI_Look_NoTattoo"), mode == None)) {
                s.packId = slot == 0 ? "" : model::kNoTexture;
                s.entryId.clear();
            }
            SameLine();
            if (RadioButton(T("$MTFUI_Look_Choose"), mode == Choose) && mode != Choose && !app.cat.packs.empty()) {
                s.packId = app.cat.packs.front().packId;
                s.entryId = app.cat.packs.front().entries.empty() ? "" : app.cat.packs.front().entries.front().id;
            }

            const auto vis = model::ResolveVisual(app.Preset(), slot);
            const auto* entry = vis.none ? nullptr : app.cat.FindEntry(vis.packId, vis.entryId);
            if (mode == Choose) {
                const auto* pack = app.cat.FindPack(s.packId);
                SetNextItemWidth(-FLT_MIN);
                if (BeginCombo("##pack", pack ? pack->label.c_str() : s.packId.c_str())) {
                    for (const auto& p : app.cat.packs) {
                        if (Selectable((TF("$MTFUI_Look_PackOption", p.label, model::AreaLabel(p.area)) + "##" + p.packId).c_str(), pack == &p)) {
                            s.packId = p.packId;
                            s.entryId = p.entries.empty() ? "" : p.entries.front().id;
                        }
                    }
                    EndCombo();
                }
                SetNextItemWidth(-FLT_MIN);
                if (BeginCombo("##entry", entry ? entry->label.c_str() : (s.entryId.empty() ? T("$MTFUI_Common_Choose") : s.entryId.c_str()), ImGuiComboFlags_HeightLarge)) {
                    if (pack) {
                        for (const auto& e : pack->entries) {
                            if (Selectable((e.label + "##" + e.id).c_str(), entry == &e)) {
                                app.UseEntry(slot, pack->packId, e.id);
                            }
                        }
                    }
                    EndCombo();
                }
            }
            if (Button(T("$MTFUI_Look_Browse"), ImVec2(-FLT_MIN, 0))) {
                app.picking = true;
                app.pickSlot = slot;
                app.RequestTab(Tab::Tattoos);
            }
        }

        void DrawLookTab(App& app, model::Slot& s, int slot) {
            DrawVisualChoice(app, s, slot);
            const auto vis = model::ResolveVisual(app.Preset(), slot);
            const auto* entry = vis.none ? nullptr : app.cat.FindEntry(vis.packId, vis.entryId);
            const auto* pack = vis.none ? nullptr : app.cat.FindPack(vis.packId);
            const int usedLayers = entry ? static_cast<int>(std::min<std::size_t>(entry->textures.size(), model::kLayerCount)) : 0;

            if (entry) {
                EnsureSkin(app);
                render::PreviewInput in;
                if (pack && pack->area == "Body") {
                    in.backdrop = g.skin.body;
                } else if (pack && pack->area == "Hands") {
                    in.backdrop = g.skin.hands;
                } else if (pack && pack->area == "Feet") {
                    in.backdrop = g.skin.feet;
                }
                in.textures = entry->textures;
                in.layers = s.layers;
                in.pulse = s.pulse;
                in.waveform = app.cat.FindWaveform(s.pulse.waveform);
                const float size = std::min(GetContentRegionAvail().x, 380.0f);
                render::DrawPreview("##preview2d", in, size);
                if (!entry->description.empty() && TreeNodeEx("##description", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", T("$MTFUI_Look_Description"))) {
                    PushStyleColor(ImGuiCol_Text, kMuted);
                    TextWrapped("%s", entry->description.c_str());
                    PopStyleColor();
                    TreePop();
                }
            } else {
                TextDisabled("%s", vis.none ? T("$MTFUI_Look_NoneShown") : T("$MTFUI_Look_NotFound"));
            }

            // Layers past the tattoo's texture count draw nothing, so they aren't offered.
            if (usedLayers > 0) {
                SeparatorText(T("$MTFUI_Look_Layers"));
            }
            for (int l = 0; l < usedLayers; ++l) {
                auto& layer = s.layers[l];
                PushID(l);
                const std::string header = TF("$MTFUI_Look_Layer", l + 1);
                SetNextItemOpen(true, ImGuiCond_Appearing);
                const bool open = CollapsingHeader((header + "###layer").c_str());
                if (BeginPopupContextItem("##layerctx")) {
                    if (MenuItem(T("$MTFUI_Look_CopyLayer"))) {
                        app.clip.layer = layer;
                    }
                    if (MenuItem(T("$MTFUI_Look_PasteLayer"), nullptr, false, app.clip.layer.has_value())) {
                        layer = *app.clip.layer;
                    }
                    if (MenuItem(T("$MTFUI_Look_ColorsToAll"))) {
                        for (auto& other : s.layers) {
                            other = layer;
                        }
                    }
                    EndPopup();
                }
                if (open) {
                    constexpr float kFieldWidth = 220;
                    ColorEditRgb(T("$MTFUI_Look_Tint"), layer.tint, app.cat.colors, kFieldWidth, T("$MTFUI_Look_TintHelp"));
                    if (usedLayers > 1) {
                        SameLine();
                        if (SmallButton(T("$MTFUI_Look_ToAll"))) {
                            for (auto& other : s.layers) {
                                other.tint = layer.tint;
                            }
                        }
                        if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                            SetTooltip("%s", T("$MTFUI_Look_ToAllTip"));
                        }
                    }
                    int alpha = static_cast<int>(layer.alpha);
                    SetNextItemWidth(kFieldWidth);
                    if (SliderInt(T("$MTFUI_Look_Opacity"), &alpha, 0, 100, "%d%%")) {
                        layer.alpha = alpha;
                    }
                    HelpMarker(T("$MTFUI_Look_OpacityHelp"));
                    ColorEditRgb(T("$MTFUI_Look_GlowColor"), layer.emissive, app.cat.colors, kFieldWidth, T("$MTFUI_Look_GlowColorHelp"));
                    float mult = static_cast<float>(layer.emissiveMult);
                    SetNextItemWidth(kFieldWidth);
                    if (SliderFloat(T("$MTFUI_Look_GlowStrength"), &mult, 0.0f, 10.0f, "%.2f")) {
                        layer.emissiveMult = mult;
                    }
                    HelpMarker(T("$MTFUI_Look_GlowStrengthHelp"));
                }
                PopID();
            }
        }

        void DrawPulseTab(App& app, model::Slot& s) {
            if (SmallButton(T("$MTFUI_Pulse_Copy"))) {
                app.clip.pulse = s.pulse;
            }
            SameLine();
            BeginDisabled(!app.clip.pulse.has_value());
            if (SmallButton(T("$MTFUI_Pulse_Paste"))) {
                s.pulse = *app.clip.pulse;
            }
            EndDisabled();
            float rate = static_cast<float>(s.pulse.rate);
            SetNextItemWidth(240);
            if (SliderFloat(T("$MTFUI_Pulse_Speed"), &rate, 0.0f, 5.0f, rate > 0.0f ? T("$MTFUI_Pulse_PerSecond_Fmt") : T("$MTFUI_Pulse_Off_Fmt"))) {
                s.pulse.rate = std::round(rate * 100.0f) / 100.0f;
            }
            HelpMarker(T("$MTFUI_Pulse_SpeedHelp"));
            int depth = static_cast<int>(s.pulse.depth);
            SetNextItemWidth(240);
            if (SliderInt(T("$MTFUI_Pulse_Depth"), &depth, 0, 100, "%d%%")) {
                s.pulse.depth = depth;
            }
            HelpMarker(T("$MTFUI_Pulse_DepthHelp"));
            float pause = static_cast<float>(s.pulse.pause);
            SetNextItemWidth(240);
            if (SliderFloat(T("$MTFUI_Pulse_Pause"), &pause, 0.0f, 10.0f, pause > 0.0f ? T("$MTFUI_Format_Seconds1_Fmt") : T("$MTFUI_Pulse_NoPause_Fmt"))) {
                s.pulse.pause = std::round(pause * 10.0f) / 10.0f;
            }
            HelpMarker(T("$MTFUI_Pulse_PauseHelp"));
            SetNextItemWidth(240);
            if (BeginCombo(T("$MTFUI_Pulse_Shape"), s.pulse.waveform.empty() ? T("$MTFUI_Pulse_Smooth") : s.pulse.waveform.c_str())) {
                if (Selectable(T("$MTFUI_Pulse_Smooth"), s.pulse.waveform.empty())) {
                    s.pulse.waveform.clear();
                }
                for (const auto& w : app.cat.waveforms) {
                    if (Selectable(w.name.c_str(), model::IEquals(w.name, s.pulse.waveform))) {
                        s.pulse.waveform = w.name;
                    }
                }
                EndCombo();
            }
            HelpMarker(T("$MTFUI_Pulse_ShapeHelp"));
            Spacing();
            render::DrawPulseGraph(s.pulse, app.cat.FindWaveform(s.pulse.waveform), GetContentRegionAvail().x, 90.0f);
            bool glows = false;
            for (const auto& l : s.layers) {
                glows = glows || l.emissiveMult > 0.0;
            }
            if (s.pulse.rate > 0.0 && !glows) {
                TextColored(kWarn, "%s", T("$MTFUI_Pulse_NoGlow"));
            }
        }

        void DrawEffectsTab(App& app, model::Slot& s, int slot) {
            TextWrapped("%s", slot == 0 ? T("$MTFUI_Effects_DefaultIntro") : T("$MTFUI_Effects_TierIntro"));
            int remove = -1, moveUp = -1;
            for (std::size_t i = 0; i < s.effects.size(); ++i) {
                auto& fx = s.effects[i];
                PushID(static_cast<int>(i));
                const float avail = GetContentRegionAvail().x;
                PushItemWidth(avail - SmallButtonReserve("^") - SmallButtonReserve("x"));
                std::string key = fx.key;
                if (CatalogPicker(app, "##fx", key, false)) {
                    fx.key = key;
                    if (const auto* e = app.cat.FindEffect(key)) {
                        model::ApplyEffectDefaults(*e, fx);
                    }
                }
                PopItemWidth();
                SameLine();
                BeginDisabled(i == 0);
                if (SmallButton("^")) {
                    moveUp = static_cast<int>(i);
                }
                EndDisabled();
                SameLine();
                if (SmallButton("x")) {
                    remove = static_cast<int>(i);
                }
                if (const auto* e = app.cat.FindEffect(fx.key)) {
                    for (int p = 0; p < 5; ++p) {
                        if (e->params[p].kind != model::ParamKind::None) {
                            auto v = e->params[p].Coerce(fx.params[p]);
                            if (ParamEditor(("p" + std::to_string(p)).c_str(), e->params[p], v, std::max(160.0f, avail * 0.55f))) {
                                fx.params[p] = v;
                            }
                        }
                    }
                    PushStyleColor(ImGuiCol_Text, kMuted);
                    const auto description = model::DescribeEffect(app.cat, fx);
                    TextWrapped("%s", e->kind == "burst" ? TF("$MTFUI_Effects_Burst", description).c_str() : description.c_str());
                    PopStyleColor();
                }
                Separator();
                PopID();
            }
            if (remove >= 0) {
                s.effects.erase(s.effects.begin() + remove);
            } else if (moveUp > 0) {
                std::swap(s.effects[moveUp], s.effects[moveUp - 1]);
            }
            BeginDisabled(static_cast<int>(s.effects.size()) >= 32);
            if (Button(T("$MTFUI_Effects_Add"))) {
                s.effects.push_back({});
            }
            EndDisabled();
            if (static_cast<int>(s.effects.size()) > model::kEffectCap) {
                SameLine();
                TextColored(kWarn, "%s", TF("$MTFUI_Effects_OverCap", model::kEffectCap).c_str());
            }
        }

        bool MinutesEditor(const char* id, const char* label, const char* help, std::int64_t& minutes) {
            int h = static_cast<int>(minutes / 60);
            int m = static_cast<int>(minutes % 60);
            bool changed = false;
            PushID(id);
            TextUnformatted(label);
            HelpMarker(help);
            SetNextItemWidth(160);
            changed |= InputInt(T("$MTFUI_Timing_Hours"), &h, 1, 6);
            SameLine();
            SetNextItemWidth(160);
            changed |= InputInt(T("$MTFUI_Timing_Minutes"), &m, 5, 15);
            PopID();
            if (changed) {
                // Minutes past 59 or below 0 carry into the hours.
                minutes = std::max<std::int64_t>(0, static_cast<std::int64_t>(h) * 60 + m);
            }
            return changed;
        }

        void DrawTimingTab(model::Slot& s, int slot) {
            if (slot == 0) {
                TextWrapped("%s", T("$MTFUI_Timing_DefaultNone"));
                return;
            }
            MinutesEditor("##stay", T("$MTFUI_Timing_Stay"), T("$MTFUI_Timing_StayHelp"), s.persistMin);
            bool allow = s.allowOverride != 0;
            BeginDisabled(s.persistMin <= 0);
            if (Checkbox(T("$MTFUI_Timing_Interrupt"), &allow)) {
                s.allowOverride = allow ? 1 : 0;
            }
            EndDisabled();
            HelpMarker(T("$MTFUI_Timing_InterruptHelp"));
            Spacing();
            MinutesEditor("##cooldown", T("$MTFUI_Timing_Cooldown"), T("$MTFUI_Timing_CooldownHelp"), s.coolMin);
        }
    }

    void DrawInspectorPanel(App& app) {
        if (!app.HasPreset()) {
            TextDisabled("%s", T("$MTFUI_Inspector_NoPreset"));
            return;
        }
        auto& p = app.Preset();
        const int slot = std::clamp(app.ed.slot, 0, static_cast<int>(p.slots.size()) - 1);
        auto& s = p.slots[slot];
        TextUnformatted(slot == 0 ? T("$MTFUI_Tier_Default") : TF("$MTFUI_Inspector_TierTitle", slot, model::SlotLabel(p, slot)).c_str());
        if (slot > 0) {
            SameLine();
            TextDisabled("%s", TF("$MTFUI_Inspector_Priority", slot, model::kSlotCount - 1).c_str());
        }
        bool anyIssue = false;
        for (const auto severity : { model::Severity::Error, model::Severity::Warning }) {
            for (const auto& issue : app.issues) {
                if (issue.slot == slot && issue.severity == severity) {
                    SeverityText(issue.severity, issue.message);
                    anyIssue = true;
                }
            }
        }
        if (anyIssue) {
            Spacing();
        }
        if (BeginTabBar("##inspector_tabs")) {
            const auto requested = app.inspectorTab;
            app.inspectorTab.reset();
            auto flags = [&](model::LintTab t) { return requested == t ? ImGuiTabItemFlags_SetSelected : 0; };
            // Default applies whenever no tier matches, so it has no conditions to edit.
            const auto label = [](const std::string& text, const char* id) { return text + "###" + id; };
            if (slot > 0 && BeginTabItem(label(T("$MTFUI_Inspector_TabCondition"), "cond").c_str(), nullptr, flags(model::LintTab::Condition))) {
                DrawConditionTab(app, s, slot);
                EndTabItem();
            }
            if (BeginTabItem(label(T("$MTFUI_Inspector_TabLook"), "look").c_str(), nullptr, flags(model::LintTab::Look))) {
                DrawLookTab(app, s, slot);
                EndTabItem();
            }
            if (BeginTabItem(label(T("$MTFUI_Inspector_TabPulse"), "pulse").c_str(), nullptr, flags(model::LintTab::Pulse))) {
                DrawPulseTab(app, s);
                EndTabItem();
            }
            const std::string fxText = s.effects.empty() ? T("$MTFUI_Inspector_TabEffects") : TF("$MTFUI_Inspector_TabEffectsCount", s.effects.size());
            if (BeginTabItem(label(fxText, "fx").c_str(), nullptr, flags(model::LintTab::Effects))) {
                DrawEffectsTab(app, s, slot);
                EndTabItem();
            }
            if (BeginTabItem(label(T("$MTFUI_Inspector_TabTiming"), "timing").c_str(), nullptr, flags(model::LintTab::Timing))) {
                DrawTimingTab(s, slot);
                EndTabItem();
            }
            EndTabBar();
        }
    }
}
