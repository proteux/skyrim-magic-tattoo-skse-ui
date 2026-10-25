#include "model/Describe.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        enum class NameAction { None, New, Duplicate };

        struct PresetsTabState {
            std::string search;
            std::string nameDraft;
            NameAction nameAction = NameAction::None;
            std::string duplicateSource;  // "" = the open preset
            bool openNamePrompt = false;  // set from a context menu, whose ID stack can't reach the prompt
            std::string deleteName;
            std::string groupTarget;  // preset waiting for a new group name
            std::string groupDraft;
            // Regrouping reloads app.library, so it waits until the list is drawn.
            std::optional<std::pair<std::string, std::string>> move;  // preset name, group
        };
        PresetsTabState g;

        void DrawGroupMenu(const model::PresetFileInfo& info, const std::vector<std::string>& groups) {
            if (!BeginMenu("Move to group")) {
                return;
            }
            for (const auto& group : groups) {
                if (MenuItem(group.c_str(), nullptr, model::IEquals(group, info.group))) {
                    g.move.emplace(info.name, group);
                }
            }
            if (!groups.empty()) {
                Separator();
            }
            if (MenuItem("New group...")) {
                g.groupTarget = info.name;
                g.groupDraft.clear();
            }
            if (!info.group.empty() && MenuItem("Remove from group")) {
                g.move.emplace(info.name, std::string{});
            }
            ImGuiMCP::EndMenu();
        }

        // Same layout as the library: a submenu per group, then ungrouped presets, then built-ins.
        void DrawCopyTiersMenu(App& app, const std::string& target, const std::vector<std::string>& groups) {
            auto item = [&](const model::PresetFileInfo& other) {
                PushID(other.name.c_str());
                if (MenuItem(other.displayName.c_str())) {
                    app.CopyTiersFrom(target, other.name);
                }
                PopID();
            };
            auto isSource = [&](const model::PresetFileInfo& other) { return !model::IEquals(other.name, target); };

            for (const auto& group : groups) {
                const bool any = std::any_of(app.library.begin(), app.library.end(),
                    [&](const model::PresetFileInfo& o) { return !o.builtin && model::IEquals(o.group, group) && isSource(o); });
                if (!any) {
                    continue;
                }
                PushID(("grp_" + io::ToLower(group)).c_str());
                if (BeginMenu(group.c_str())) {
                    for (const auto& other : app.library) {
                        if (!other.builtin && model::IEquals(other.group, group) && isSource(other)) {
                            item(other);
                        }
                    }
                    ImGuiMCP::EndMenu();
                }
                PopID();
            }

            bool separated = groups.empty();
            for (const auto& other : app.library) {
                if (other.builtin || !other.group.empty() || !isSource(other)) {
                    continue;
                }
                if (!separated) {
                    Separator();
                    separated = true;
                }
                item(other);
            }

            if (std::any_of(app.library.begin(), app.library.end(), [](const model::PresetFileInfo& o) { return o.builtin; })) {
                Separator();
                if (BeginMenu("Built-in")) {
                    for (const auto& other : app.library) {
                        if (other.builtin) {
                            item(other);
                        }
                    }
                    ImGuiMCP::EndMenu();
                }
            }
        }

        void DrawLibraryItem(App& app, const model::PresetFileInfo& info, const std::vector<std::string>& groups, float onMarkerX) {
            PushID(info.name.c_str());
            const bool open = app.HasPreset() && model::IEquals(app.Preset().name, info.name) && !app.ed.isNew;
            const bool applied = app.IsApplied(info.name);
            std::string label = info.displayName;
            if (open && app.Dirty()) {
                label += " *";
            }
            if (Selectable(label.c_str(), open)) {
                app.Open(info.name);
            }
            if (!info.builtin && BeginDragDropSource()) {
                SetDragDropPayload("MTFUI_PRESET", info.name.c_str(), info.name.size() + 1);
                Text("Move %s to a group", info.displayName.c_str());
                EndDragDropSource();
            }
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s.json%s", info.name.c_str(), applied ? "\nApplied to the current target" : "");
            }
            if (applied) {
                SameLine(onMarkerX);
                TextColored(kGood, "on");
            }
            if (BeginPopupContextItem("##ctx")) {
                if (MenuItem("Apply to target")) {
                    app.ApplyToTarget(info.name);
                }
                if (applied && MenuItem("Remove from target")) {
                    app.RemoveFromTarget(info.name);
                }
                if (MenuItem("Duplicate...")) {
                    g.nameAction = NameAction::Duplicate;
                    g.duplicateSource = info.name;
                    g.nameDraft = info.displayName + " copy";
                    g.openNamePrompt = true;
                }
                if (!info.builtin) {
                    DrawGroupMenu(info, groups);
                }
                if (!info.builtin && BeginMenu("Copy tiers from")) {
                    TextDisabled("Keeps this preset's Default tattoo");
                    Separator();
                    DrawCopyTiersMenu(app, info.name, groups);
                    ImGuiMCP::EndMenu();
                }
                if (!info.builtin && MenuItem("Delete...")) {
                    g.deleteName = info.name;
                }
                EndPopup();
            }
            PopID();
        }

        // Drops a dragged preset into group ("" = ungrouped) when released over the last item.
        void GroupDropTarget(const std::string& group) {
            if (BeginDragDropTarget()) {
                if (const auto* payload = AcceptDragDropPayload("MTFUI_PRESET")) {
                    g.move.emplace(static_cast<const char*>(payload->Data), group);
                }
                EndDragDropTarget();
            }
        }

        void DrawLibrary(App& app) {
            if (app.focusSearch) {
                SetKeyboardFocusHere();
                app.focusSearch = false;
            }
            SearchBox("##presetsearch", g.search, -1.0f, "Search presets");
            if (Button("New")) {
                g.nameAction = NameAction::New;
                g.nameDraft = "My Preset";
                OpenPopup("Preset name");
            }
            SameLine();
            BeginDisabled(!app.HasPreset());
            if (Button("Duplicate")) {
                g.nameAction = NameAction::Duplicate;
                g.duplicateSource.clear();
                g.nameDraft = app.Preset().displayName + " copy";
                OpenPopup("Preset name");
            }
            EndDisabled();
            Separator();

            if (BeginChild("##list")) {
                const float onMarkerX = GetContentRegionAvail().x - CalcTextSize("on").x;
                const auto groups = app.Groups();
                const bool searching = !g.search.empty();
                const std::string openName = app.HasPreset() && !app.ed.isNew ? app.Preset().name : std::string{};
                auto matches = [&](const model::PresetFileInfo& info) { return MatchesSearch(info.displayName + " " + info.name + " " + info.group, g.search); };

                for (const auto& group : groups) {
                    std::vector<const model::PresetFileInfo*> members;
                    bool holdsOpen = false;
                    for (const auto& info : app.library) {
                        if (!info.builtin && model::IEquals(info.group, group) && matches(info)) {
                            members.push_back(&info);
                            holdsOpen |= model::IEquals(info.name, openName);
                        }
                    }
                    if (members.empty()) {
                        continue;
                    }
                    if (searching) {
                        SetNextItemOpen(true);
                    } else if (holdsOpen) {
                        SetNextItemOpen(true, ImGuiCond_Appearing);
                    }
                    const std::string header = group + "  (" + std::to_string(members.size()) + ")###grp_" + io::ToLower(group);
                    const bool expanded = TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                    GroupDropTarget(group);
                    if (expanded) {
                        for (const auto* info : members) {
                            DrawLibraryItem(app, *info, groups, onMarkerX);
                        }
                        TreePop();
                    }
                }

                bool ungroupedHeader = false;
                for (const auto& info : app.library) {
                    if (info.builtin || !info.group.empty() || !matches(info)) {
                        continue;
                    }
                    if (!groups.empty() && !ungroupedHeader) {
                        SeparatorText("Ungrouped");
                        GroupDropTarget({});
                        ungroupedHeader = true;
                    }
                    DrawLibraryItem(app, info, groups, onMarkerX);
                }

                bool builtinHeader = false;
                for (const auto& info : app.library) {
                    if (!info.builtin || !matches(info)) {
                        continue;
                    }
                    if (!builtinHeader) {
                        SeparatorText("Built-in (read-only)");
                        builtinHeader = true;
                    }
                    DrawLibraryItem(app, info, groups, onMarkerX);
                }
                if (app.library.empty()) {
                    TextWrapped("No presets yet. Click New to start one.");
                }
            }
            EndChild();

            if (g.move) {
                const auto [name, group] = *g.move;
                g.move.reset();
                app.SetGroup(name, group);
            }

            if (!g.groupTarget.empty() && !IsPopupOpen("New group")) {
                OpenPopup("New group");
            }
            if (BeginPopupModal("New group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (IsWindowAppearing()) {
                    SetKeyboardFocusHere();
                }
                SetNextItemWidth(260);
                const bool enter = InputString("##group", g.groupDraft, "Group name", ImGuiInputTextFlags_EnterReturnsTrue, 64);
                const bool valid = g.groupDraft.find_first_not_of(' ') != std::string::npos;
                BeginDisabled(!valid);
                if (Button("Move", ImVec2{ 100, 0 }) || (enter && valid)) {
                    app.SetGroup(g.groupTarget, g.groupDraft);
                    g.groupTarget.clear();
                    CloseCurrentPopup();
                }
                EndDisabled();
                SameLine();
                if (Button("Cancel", ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                    g.groupTarget.clear();
                    CloseCurrentPopup();
                }
                EndPopup();
            }

            if (!g.deleteName.empty()) {
                OpenPopup("Delete preset?");
            }
            if (BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                Text("Delete '%s'?", g.deleteName.c_str());
                TextDisabled("You can bring it back from General > Deleted presets.");
                if (Button("Delete", ImVec2{ 100, 0 })) {
                    app.Delete(g.deleteName);
                    g.deleteName.clear();
                    CloseCurrentPopup();
                }
                SameLine();
                if (Button("Cancel", ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                    g.deleteName.clear();
                    CloseCurrentPopup();
                }
                EndPopup();
            }

            if (g.openNamePrompt) {
                g.openNamePrompt = false;
                OpenPopup("Preset name");
            }
            if (BeginPopupModal("Preset name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                switch (NamePrompt(g.nameDraft, app.UniqueName(g.nameDraft))) {
                    case NamePromptResult::Confirmed:
                        if (g.nameAction == NameAction::New) {
                            app.CreateNew(g.nameDraft);
                        } else {
                            app.Duplicate(g.nameDraft, g.duplicateSource);
                        }
                        CloseCurrentPopup();
                        break;
                    case NamePromptResult::Cancelled:
                        CloseCurrentPopup();
                        break;
                    default:
                        break;
                }
                EndPopup();
            }
        }

        void DrawTierMenu(App& app, int i) {
            auto& p = app.Preset();
            if (i > 1 && MenuItem("Move up", "Alt+Up")) {
                app.MoveTier(i, i - 1);
            }
            if (i > 0 && i + 1 < static_cast<int>(p.slots.size()) && MenuItem("Move down", "Alt+Down")) {
                app.MoveTier(i, i + 1);
            }
            if (i > 0 && MenuItem("Clear tier")) {
                p.slots[i] = model::DefaultSlot();
            }
            Separator();
            if (MenuItem("Copy tier")) {
                app.clip.tier = p.slots[i];
                app.clip.tierVisual = model::ResolveVisual(p, i);
            }
            if (BeginMenu("Paste", app.clip.tier.has_value())) {
                const auto& src = *app.clip.tier;
                auto& dst = p.slots[i];
                if (i > 0 && MenuItem("Whole tier")) {
                    const auto vis = app.clip.tierVisual;
                    dst = src;
                    model::SetLook(dst, false, src, vis);
                }
                if (MenuItem("Look only (tattoo, layers, pulse)")) {
                    model::SetLook(dst, i == 0, src, app.clip.tierVisual);
                }
                if (i > 0 && MenuItem("Conditions only", nullptr, false, !src.conditions.empty())) {
                    dst.conditions = src.conditions;
                    dst.op = src.op;
                }
                if (MenuItem("Effects only", nullptr, false, !src.effects.empty())) {
                    dst.effects = src.effects;
                }
                if (i > 0 && MenuItem("Timing only")) {
                    dst.persistMin = src.persistMin;
                    dst.allowOverride = src.allowOverride;
                    dst.coolMin = src.coolMin;
                }
                ImGuiMCP::EndMenu();
            }
            if (BeginMenu("Copy look from")) {
                for (int j = 0; j < static_cast<int>(p.slots.size()); ++j) {
                    if (j != i && MenuItem(model::SlotLabel(p, j).c_str())) {
                        model::CopyLook(p, j, i);
                    }
                }
                ImGuiMCP::EndMenu();
            }
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
                    app.MoveTier(*static_cast<const int*>(payload->Data), i);
                }
                EndDragDropTarget();
            }
            if (BeginPopupContextItem("##tierctx")) {
                DrawTierMenu(app, i);
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
                        render::DrawThumbAt(dl, ImVec2{ start.x + width - thumb - 4.0f, start.y + 4.0f }, thumb, e->textures.front(), s.layers[0].tint,
                            s.layers[0].alpha / 100.0f, 3.0f);
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

            SetNextItemWidth(260 - GetFrameHeight() - GetStyle()->ItemInnerSpacing.x);
            InputString("##groupname", p.group, "No group", 0, 64);
            SameLine(0, GetStyle()->ItemInnerSpacing.x);
            if (BeginCombo("Group", nullptr, ImGuiComboFlags_NoPreview)) {
                if (Selectable("No group", p.group.empty())) {
                    p.group.clear();
                }
                for (const auto& group : app.Groups()) {
                    if (Selectable(group.c_str(), model::IEquals(group, p.group))) {
                        p.group = group;
                    }
                }
                EndCombo();
            }
            HelpMarker("Folder for this preset in the list. Only this UI uses it; MTF ignores it.");

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
                if (Button("Copy as JSON")) {
                    app.ExportToClipboard();
                }
                HelpMarker("Puts the whole preset on the clipboard, to share it or keep it somewhere. "
                           "General > Backups can paste one back in as a new preset.");
            }
        }

        void DrawIssues(App& app) {
            if (app.issues.empty()) {
                TextColored(kGood, "No problems found");
                return;
            }
            // A fix changes the list, so apply it after the loop.
            std::optional<model::LintIssue> fix;
            int shown = 0;
            for (const auto& issue : app.issues) {
                PushID(shown++);
                if (issue.slot >= 0) {
                    if (SmallButton(model::SlotLabel(app.Preset(), issue.slot).c_str())) {
                        app.ed.slot = issue.slot;
                        if (issue.tab != model::LintTab::None) {
                            app.inspectorTab = issue.tab;
                        }
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        SetTooltip("Select this tier and show where to fix it");
                    }
                    SameLine();
                }
                if (issue.fix.kind != model::LintFix::Kind::None) {
                    if (SmallButton(issue.fix.kind == model::LintFix::Kind::MoveAbove ? "Move it up" : "Enable it")) {
                        fix = issue;
                    }
                    SameLine();
                }
                SeverityText(issue.severity, issue.message);
                PopID();
            }
            if (fix) {
                app.ApplyLintFix(*fix);
            }
        }
    }

    void DrawLibraryPanel(App& app) { DrawLibrary(app); }

    void DrawTiersPanel(App& app) {
        if (!app.HasPreset()) {
            Spacing();
            TextWrapped("Pick a preset in the list, or click New.");
            Spacing();
            TextWrapped("A preset is a Default look plus a ladder of tiers that modify it. MTF checks tiers from the top; the first one "
                        "whose conditions match decides the tattoo's look and effects. When none match, Default is used.");
            return;
        }
        DrawPresetHeader(app);
        const float rowWidth = GetContentRegionAvail().x;
        SeparatorText("Base");
        HelpMarker("Default sets the base look. Tiers that leave their tattoo as \"Same as Default\" build on it, "
                   "and it is shown whenever no tier matches.");
        DrawTierRow(app, 0, rowWidth);
        SeparatorText("Tiers (top wins)");
        HelpMarker("Drag tiers to change priority, or select one and press Alt+Up / Alt+Down. Right-click a tier to copy, paste or clear it. "
                   "The first tier whose conditions are true is shown; Default applies when none are.");
        for (int i = 1; i < model::kSlotCount && i < static_cast<int>(app.Preset().slots.size()); ++i) {
            DrawTierRow(app, i, rowWidth);
        }
        SeparatorText("Checks");
        DrawIssues(app);
    }

    void DrawPresetsTab(App& app) {
        if (BeginChild("##library", ImVec2{ 230.0f, 0 }, ImGuiChildFlags_Border)) {
            DrawLibrary(app);
        }
        EndChild();
        SameLine();
        if (!app.HasPreset()) {
            BeginGroup();
            DrawTiersPanel(app);
            EndGroup();
            return;
        }

        const float inspectorWidth = std::max(400.0f, GetContentRegionAvail().x * 0.48f);
        const float middleWidth = GetContentRegionAvail().x - inspectorWidth - GetStyle()->ItemSpacing.x;
        if (BeginChild("##middle", ImVec2{ middleWidth, 0 })) {
            DrawTiersPanel(app);
        }
        EndChild();
        SameLine();
        if (BeginChild("##inspector", ImVec2{ inspectorWidth, 0 }, ImGuiChildFlags_Border)) {
            DrawInspectorPanel(app);
        }
        EndChild();
    }
}
