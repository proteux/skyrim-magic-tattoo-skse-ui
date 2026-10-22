#include "model/Describe.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        std::string g_search;
        std::string g_nameDraft;
        enum class NameAction { None, New, Duplicate };
        NameAction g_nameAction = NameAction::None;
        std::string g_deleteName;

        bool IsApplied(const App& app, const std::string& name) {
            return std::any_of(app.applied.begin(), app.applied.end(), [&](const std::string& a) { return model::IEquals(a, name); });
        }

        void DrawLibrary(App& app, float width) {
            if (!BeginChild("##library", ImVec2{ width, 0 }, ImGuiChildFlags_Border)) {
                EndChild();
                return;
            }
            SearchBox("##presetsearch", g_search, -1.0f, "Search presets");
            if (Button("New")) {
                g_nameAction = NameAction::New;
                g_nameDraft = "My Preset";
                OpenPopup("Preset name");
            }
            SameLine();
            BeginDisabled(!app.HasPreset());
            if (Button("Duplicate")) {
                g_nameAction = NameAction::Duplicate;
                g_nameDraft = app.Preset().displayName + " copy";
                OpenPopup("Preset name");
            }
            EndDisabled();
            Separator();

            if (BeginChild("##list")) {
                bool builtinHeader = false;
                for (const auto& info : app.library) {
                    if (!MatchesSearch(info.displayName + " " + info.name, g_search)) {
                        continue;
                    }
                    if (info.builtin && !builtinHeader) {
                        SeparatorText("Built-in (read-only)");
                        builtinHeader = true;
                    }
                    PushID(info.name.c_str());
                    const bool open = app.HasPreset() && model::IEquals(app.Preset().name, info.name) && !app.ed.isNew;
                    std::string label = info.displayName;
                    if (open && app.Dirty()) {
                        label += " *";
                    }
                    if (Selectable(label.c_str(), open)) {
                        app.Open(info.name);
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        SetTooltip("%s.json%s", info.name.c_str(), IsApplied(app, info.name) ? "\nApplied to the current target" : "");
                    }
                    if (IsApplied(app, info.name)) {
                        SameLine(width - 34);
                        TextColored(kGood, "on");
                    }
                    if (BeginPopupContextItem("##ctx")) {
                        if (MenuItem("Apply to target")) {
                            app.ApplyToTarget(info.name);
                        }
                        if (IsApplied(app, info.name) && MenuItem("Remove from target")) {
                            app.RemoveFromTarget(info.name);
                        }
                        if (!info.builtin && MenuItem("Delete...")) {
                            g_deleteName = info.name;
                        }
                        EndPopup();
                    }
                    PopID();
                }
                if (app.library.empty()) {
                    TextWrapped("No presets yet. Click New to start one.");
                }
            }
            EndChild();

            if (!g_deleteName.empty()) {
                OpenPopup("Delete preset?");
            }
            if (BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                Text("Delete '%s'?", g_deleteName.c_str());
                TextDisabled("MTF keeps a hidden placeholder file; a backup goes to MagicTattoosUI/backups.");
                if (Button("Delete", ImVec2{ 100, 0 })) {
                    app.Delete(g_deleteName);
                    g_deleteName.clear();
                    CloseCurrentPopup();
                }
                SameLine();
                if (Button("Cancel", ImVec2{ 100, 0 })) {
                    g_deleteName.clear();
                    CloseCurrentPopup();
                }
                EndPopup();
            }

            if (BeginPopupModal("Preset name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (IsWindowAppearing()) {
                    SetKeyboardFocusHere();
                }
                SetNextItemWidth(280);
                const bool enter = InputString("##name", g_nameDraft, "Display name", ImGuiInputTextFlags_EnterReturnsTrue, 64);
                TextDisabled("File: %s.json", app.UniqueName(g_nameDraft).c_str());
                const bool ok = !g_nameDraft.empty();
                BeginDisabled(!ok);
                if (Button("Create", ImVec2{ 100, 0 }) || (enter && ok)) {
                    if (g_nameAction == NameAction::New) {
                        app.CreateNew(g_nameDraft);
                    } else {
                        app.Duplicate(g_nameDraft);
                    }
                    CloseCurrentPopup();
                }
                EndDisabled();
                SameLine();
                if (Button("Cancel", ImVec2{ 100, 0 })) {
                    CloseCurrentPopup();
                }
                EndPopup();
            }
            EndChild();
        }

        void MoveTier(model::Preset& p, int from, int to) {
            if (from == to || from < 1 || to < 1 || from >= model::kSlotCount || to >= model::kSlotCount) {
                return;
            }
            auto slot = p.slots[from];
            p.slots.erase(p.slots.begin() + from);
            p.slots.insert(p.slots.begin() + to, slot);
        }

        void DrawTierRow(App& app, int i, float width) {
            auto& p = app.Preset();
            const auto& s = p.slots[i];
            const bool unused = i > 0 && !s.Configured();
            const float rowH = GetTextLineHeight() * 2.0f + 10.0f;
            PushID(i);
            const ImVec2 start = GetCursorScreenPos();
            if (Selectable("##row", app.ed.slot == i, 0, ImVec2{ width, rowH })) {
                app.ed.slot = i;
            }
            if (i > 0 && BeginDragDropSource()) {
                SetDragDropPayload("MTFUI_TIER", &i, sizeof(int));
                Text("Move %s", model::SlotLabel(p, i).c_str());
                EndDragDropSource();
            }
            if (i > 0 && BeginDragDropTarget()) {
                if (const auto* payload = AcceptDragDropPayload("MTFUI_TIER")) {
                    const int from = *static_cast<const int*>(payload->Data);
                    MoveTier(p, from, i);
                    app.ed.slot = i;
                }
                EndDragDropTarget();
            }
            if (BeginPopupContextItem("##tierctx")) {
                if (i > 1 && MenuItem("Move up")) {
                    MoveTier(p, i, i - 1);
                    app.ed.slot = i - 1;
                }
                if (i > 0 && i + 1 < model::kSlotCount && MenuItem("Move down")) {
                    MoveTier(p, i, i + 1);
                    app.ed.slot = i + 1;
                }
                if (i > 0 && MenuItem("Clear tier")) {
                    p.slots[i] = model::DefaultSlot();
                }
                if (BeginMenu("Copy look from")) {
                    for (int j = 0; j < model::kSlotCount; ++j) {
                        if (j != i && MenuItem(model::SlotLabel(p, j).c_str())) {
                            p.slots[i].layers = p.slots[j].layers;
                            p.slots[i].pulse = p.slots[j].pulse;
                        }
                    }
                    ImGuiMCP::EndMenu();
                }
                EndPopup();
            }
            const ImVec2 after = GetCursorScreenPos();

            auto* dl = GetWindowDrawList();
            const float pad = 6.0f;
            const float thumb = rowH - 8.0f;
            ImDrawListManager::PushClipRect(dl, start, ImVec2{ start.x + width - thumb - 12.0f, start.y + rowH }, true);
            std::string title = i == 0 ? "Default" : "#" + std::to_string(i) + "  " + model::SlotLabel(p, i);
            const auto titleColor = unused ? GetColorU32(ImGuiCol_TextDisabled, 1.0f) : GetColorU32(ImGuiCol_Text, 1.0f);
            ImDrawListManager::AddText(dl, ImVec2{ start.x + pad, start.y + 4.0f }, titleColor, title.c_str());

            int errors = 0, warnings = 0;
            for (const auto& issue : app.issues) {
                if (issue.slot != i) {
                    continue;
                }
                if (issue.severity == model::Severity::Error) {
                    ++errors;
                } else if (issue.severity == model::Severity::Warning) {
                    ++warnings;
                }
            }
            if (errors + warnings > 0) {
                const std::string badge = std::to_string(errors + warnings) + (errors ? " error" : " warning") + (errors + warnings > 1 ? "s" : "");
                const float tw = CalcTextSize(title.c_str()).x;
                ImDrawListManager::AddText(dl, ImVec2{ start.x + pad + tw + 12.0f, start.y + 4.0f },
                    GetColorU32(errors ? kError : kWarn), badge.c_str());
            }
            const std::string summary = unused ? "Unused. Select it and add a condition to make a new tier." : model::DescribeSlot(app.cat, p, i);
            ImDrawListManager::AddText(dl, ImVec2{ start.x + pad, start.y + 6.0f + GetTextLineHeight() }, GetColorU32(ImGuiCol_TextDisabled, 1.0f), summary.c_str());
            ImDrawListManager::PopClipRect(dl);

            if (!unused) {
                const auto vis = model::ResolveVisual(p, i);
                if (!vis.none) {
                    if (const auto* e = app.cat.FindEntry(vis.packId, vis.entryId); e && !e->textures.empty()) {
                        if (const auto* tex = render::DdsCache::Get().Request(e->textures.front(), 256)) {
                            const ImVec2 a{ start.x + width - thumb - 4.0f, start.y + 4.0f };
                            ImDrawListManager::AddRectFilled(dl, a, ImVec2{ a.x + thumb, a.y + thumb }, render::ToImColor(0xC89F86, 1.0f), 3.0f, 0);
                            ImDrawListManager::AddImage(dl, reinterpret_cast<ImTextureID>(tex->srv), a, ImVec2{ a.x + thumb, a.y + thumb }, ImVec2{ 0, 0 },
                                ImVec2{ 1, 1 }, render::ToImColor(s.layers[0].tint, s.layers[0].alpha / 100.0f));
                        }
                    }
                }
            }
            SetCursorScreenPos(after);
            PopID();
        }

        void DrawPresetHeader(App& app) {
            auto& p = app.Preset();
            const bool dirty = app.Dirty();
            BeginDisabled(!dirty || app.ed.builtin);
            if (Button(app.ed.isNew ? "Create" : "Save")) {
                app.Save();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(!dirty);
            if (Button("Revert")) {
                app.Revert();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(app.ed.undo.empty());
            if (Button("Undo")) {
                app.Undo();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(app.ed.redo.empty());
            if (Button("Redo")) {
                app.Redo();
            }
            EndDisabled();
            if (app.ed.builtin) {
                SameLine();
                TextColored(kWarn, "Built-in preset: Duplicate it to make changes you can save.");
            } else if (dirty) {
                SameLine();
                TextColored(kWarn, "Unsaved changes");
            }

            SetNextItemWidth(260);
            if (InputString("Name", p.displayName, nullptr, 0, 64) && app.ed.isNew) {
                p.name = app.UniqueName(p.displayName);
            }
            SameLine();
            TextDisabled("%s.json", p.name.c_str());
            if (!app.ed.isNew && model::SanitizePresetName(p.displayName) != p.name) {
                HelpMarker("The file name stays the same when you rename, so anything already using this preset keeps working.");
            }

            if (CollapsingHeader("Preset options")) {
                float duration = static_cast<float>(p.transitionDuration);
                SetNextItemWidth(220);
                if (SliderFloat("Transition", &duration, 0.0f, 10.0f, "%.1f s")) {
                    p.transitionDuration = duration;
                }
                HelpMarker("Seconds to cross-fade the tattoo's opacity, colors and glow when the active tier changes. 0 = instant.");
                static const char* modes[] = { "Off", "Overlay (fade out)", "Emissive (stop glowing)", "Inverted (dip and recover)" };
                int mode = p.fade.enabled ? p.fade.mode + 1 : 0;
                SetNextItemWidth(220);
                if (Combo("On death", &mode, modes, 4)) {
                    p.fade.enabled = mode > 0;
                    p.fade.mode = std::max(0, mode - 1);
                }
                HelpMarker("What happens to the tattoo when the wearer dies.");
                if (p.fade.enabled) {
                    int ms = static_cast<int>(p.fade.durationMs);
                    SetNextItemWidth(220);
                    if (SliderInt("Death fade", &ms, 100, 10000, "%d ms")) {
                        p.fade.durationMs = ms;
                    }
                }
            }
        }

        void DrawIssues(App& app) {
            if (app.issues.empty()) {
                TextColored(kGood, "No problems found");
                return;
            }
            int shown = 0;
            for (const auto& issue : app.issues) {
                PushID(shown++);
                if (issue.slot >= 0) {
                    if (SmallButton(model::SlotLabel(app.Preset(), issue.slot).c_str())) {
                        app.ed.slot = issue.slot;
                    }
                    SameLine();
                }
                SeverityText(issue.severity, issue.message);
                PopID();
            }
        }
    }

    void DrawPresetsTab(App& app) {
        const float libWidth = 230.0f;
        DrawLibrary(app, libWidth);
        SameLine();
        if (!app.HasPreset()) {
            BeginGroup();
            Spacing();
            TextWrapped("Pick a preset on the left, or click New.");
            Spacing();
            TextWrapped("A preset is a ladder of tiers. MTF checks tiers from the top; the first one whose conditions match decides "
                        "the tattoo's look and effects. When none match, Default is used.");
            EndGroup();
            return;
        }

        const float inspectorWidth = std::max(400.0f, GetContentRegionAvail().x * 0.48f);
        const float middleWidth = GetContentRegionAvail().x - inspectorWidth - GetStyle()->ItemSpacing.x;
        if (BeginChild("##middle", ImVec2{ middleWidth, 0 })) {
            DrawPresetHeader(app);
            SeparatorText("Tiers (top wins)");
            HelpMarker("Drag tiers to change priority. Right-click a tier for more. The first tier whose conditions are true is shown; "
                       "Default applies when none are.");
            const float rowWidth = GetContentRegionAvail().x;
            for (int i = 1; i < model::kSlotCount && i < static_cast<int>(app.Preset().slots.size()); ++i) {
                DrawTierRow(app, i, rowWidth);
            }
            Separator();
            DrawTierRow(app, 0, rowWidth);
            SeparatorText("Checks");
            DrawIssues(app);
        }
        EndChild();
        SameLine();
        DrawInspector(app, inspectorWidth);
    }
}
