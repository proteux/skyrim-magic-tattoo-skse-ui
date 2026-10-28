#include "model/Describe.h"
#include "model/Gradient.h"
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

        struct GradientState {
            std::string conditionId;
            bool below = true;
            int lo = 0;
            int hi = 100;
            int steps = 10;
            std::vector<std::uint32_t> colors;  // low end of the range first
            bool tint = true;
            bool emissive = true;
            bool glow = true;
            float glowLow = 3.0f;
            float glowHigh = 1.0f;
            bool fade = false;
            int alphaLow = 100;
            int alphaHigh = 100;
        };
        GradientState grad;
        const std::vector<std::uint32_t> kHealthBar{ 0xE02020, 0xF0D020, 0x30C040 };

        void DrawGroupMenu(const model::PresetFileInfo& info, const std::vector<std::string>& groups) {
            if (!BeginMenu(T("$MTFUI_Library_MoveToGroup"))) {
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
            if (MenuItem(T("$MTFUI_Library_NewGroupItem"))) {
                g.groupTarget = info.name;
                g.groupDraft.clear();
            }
            if (!info.group.empty() && MenuItem(T("$MTFUI_Library_RemoveFromGroup"))) {
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
                if (BeginMenu(T("$MTFUI_Library_BuiltinMenu"))) {
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
                TextUnformatted(TF("$MTFUI_Library_DragToGroup", info.displayName).c_str());
                EndDragDropSource();
            }
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("%s", (applied ? TF("$MTFUI_Library_FileTipApplied", info.name) : TF("$MTFUI_Library_FileTip", info.name)).c_str());
            }
            if (applied) {
                SameLine(onMarkerX);
                TextColored(kGood, "%s", T("$MTFUI_Library_OnMarker"));
            }
            if (BeginPopupContextItem("##ctx")) {
                if (MenuItem(T("$MTFUI_Target_Apply"))) {
                    app.ApplyToTarget(info.name);
                }
                if (applied && MenuItem(T("$MTFUI_Library_RemoveFromTarget"))) {
                    app.RemoveFromTarget(info.name);
                }
                if (MenuItem(T("$MTFUI_Library_DuplicateItem"))) {
                    g.nameAction = NameAction::Duplicate;
                    g.duplicateSource = info.name;
                    g.nameDraft = TF("$MTFUI_Preset_CopyName", info.displayName);
                    g.openNamePrompt = true;
                }
                if (!info.builtin) {
                    DrawGroupMenu(info, groups);
                }
                if (!info.builtin && BeginMenu(T("$MTFUI_Library_CopyTiersFrom"))) {
                    TextDisabled("%s", T("$MTFUI_Library_CopyTiersNote"));
                    Separator();
                    DrawCopyTiersMenu(app, info.name, groups);
                    ImGuiMCP::EndMenu();
                }
                if (!info.builtin && MenuItem(T("$MTFUI_Library_DeleteItem"))) {
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
            const char* namePromptTitle = T("$MTFUI_Library_NameTitle");
            SearchBox("##presetsearch", g.search, -1.0f, T("$MTFUI_Library_Search"));
            if (Button(T("$MTFUI_Library_New"))) {
                g.nameAction = NameAction::New;
                g.nameDraft = T("$MTFUI_Library_DefaultName");
                OpenPopup(namePromptTitle);
            }
            SameLine();
            BeginDisabled(!app.HasPreset());
            if (Button(T("$MTFUI_Library_Duplicate"))) {
                g.nameAction = NameAction::Duplicate;
                g.duplicateSource.clear();
                g.nameDraft = TF("$MTFUI_Preset_CopyName", app.Preset().displayName);
                OpenPopup(namePromptTitle);
            }
            EndDisabled();
            Separator();

            if (BeginChild("##list")) {
                const float onMarkerX = GetContentRegionAvail().x - CalcTextSize(T("$MTFUI_Library_OnMarker")).x;
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
                        SeparatorText(T("$MTFUI_Library_Ungrouped"));
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
                        SeparatorText(T("$MTFUI_Library_BuiltinHeader"));
                        builtinHeader = true;
                    }
                    DrawLibraryItem(app, info, groups, onMarkerX);
                }
                if (app.library.empty()) {
                    TextWrapped("%s", T("$MTFUI_Library_Empty"));
                }
            }
            EndChild();

            if (g.move) {
                const auto [name, group] = *g.move;
                g.move.reset();
                app.SetGroup(name, group);
            }

            const char* groupTitle = T("$MTFUI_Library_NewGroupTitle");
            if (!g.groupTarget.empty() && !IsPopupOpen(groupTitle)) {
                OpenPopup(groupTitle);
            }
            if (BeginPopupModal(groupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                if (IsWindowAppearing()) {
                    SetKeyboardFocusHere();
                }
                SetNextItemWidth(260);
                const bool enter = InputString("##group", g.groupDraft, T("$MTFUI_Library_GroupHint"), ImGuiInputTextFlags_EnterReturnsTrue, 64);
                const bool valid = g.groupDraft.find_first_not_of(' ') != std::string::npos;
                BeginDisabled(!valid);
                if (Button(T("$MTFUI_Library_Move"), ImVec2{ 100, 0 }) || (enter && valid)) {
                    app.SetGroup(g.groupTarget, g.groupDraft);
                    g.groupTarget.clear();
                    CloseCurrentPopup();
                }
                EndDisabled();
                SameLine();
                if (Button(T("$MTFUI_Common_Cancel"), ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                    g.groupTarget.clear();
                    CloseCurrentPopup();
                }
                EndPopup();
            }

            const char* deleteTitle = T("$MTFUI_Library_DeleteTitle");
            if (!g.deleteName.empty()) {
                OpenPopup(deleteTitle);
            }
            if (BeginPopupModal(deleteTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                TextUnformatted(TF("$MTFUI_Library_DeleteQuestion", g.deleteName).c_str());
                TextDisabled("%s", T("$MTFUI_Library_DeleteNote"));
                if (Button(T("$MTFUI_Library_Delete"), ImVec2{ 100, 0 })) {
                    app.Delete(g.deleteName);
                    g.deleteName.clear();
                    CloseCurrentPopup();
                }
                SameLine();
                if (Button(T("$MTFUI_Common_Cancel"), ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                    g.deleteName.clear();
                    CloseCurrentPopup();
                }
                EndPopup();
            }

            if (g.openNamePrompt) {
                g.openNamePrompt = false;
                OpenPopup(namePromptTitle);
            }
            if (BeginPopupModal(namePromptTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
            if (i > 1 && MenuItem(T("$MTFUI_TierMenu_MoveUp"), "Alt+Up")) {
                app.MoveTier(i, i - 1);
            }
            if (i > 0 && i + 1 < static_cast<int>(p.slots.size()) && MenuItem(T("$MTFUI_TierMenu_MoveDown"), "Alt+Down")) {
                app.MoveTier(i, i + 1);
            }
            if (i > 0 && MenuItem(T("$MTFUI_TierMenu_Clear"))) {
                p.slots[i] = model::DefaultSlot();
                model::TrimTrailingEmptySlots(p);
                app.ed.slot = std::min(app.ed.slot, static_cast<int>(p.slots.size()) - 1);
            }
            Separator();
            if (MenuItem(T("$MTFUI_TierMenu_Copy"))) {
                app.clip.tier = p.slots[i];
                app.clip.tierVisual = model::ResolveVisual(p, i);
            }
            if (BeginMenu(T("$MTFUI_TierMenu_Paste"), app.clip.tier.has_value())) {
                const auto& src = *app.clip.tier;
                auto& dst = p.slots[i];
                if (i > 0 && MenuItem(T("$MTFUI_TierMenu_PasteWhole"))) {
                    const auto vis = app.clip.tierVisual;
                    dst = src;
                    model::SetLook(dst, false, src, vis);
                }
                if (MenuItem(T("$MTFUI_TierMenu_PasteLook"))) {
                    model::SetLook(dst, i == 0, src, app.clip.tierVisual);
                }
                if (i > 0 && MenuItem(T("$MTFUI_TierMenu_PasteConditions"), nullptr, false, !src.conditions.empty())) {
                    dst.conditions = src.conditions;
                    dst.op = src.op;
                }
                if (MenuItem(T("$MTFUI_TierMenu_PasteEffects"), nullptr, false, !src.effects.empty())) {
                    dst.effects = src.effects;
                }
                if (i > 0 && MenuItem(T("$MTFUI_TierMenu_PasteTiming"))) {
                    dst.persistMin = src.persistMin;
                    dst.allowOverride = src.allowOverride;
                    dst.coolMin = src.coolMin;
                }
                ImGuiMCP::EndMenu();
            }
            if (BeginMenu(T("$MTFUI_TierMenu_CopyLookFrom"))) {
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
                TextUnformatted(TF("$MTFUI_Tiers_DragMove", model::SlotLabel(p, i)).c_str());
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
            std::string title = i == 0 ? T("$MTFUI_Tier_Default") : TF("$MTFUI_Tiers_RowTitle", i, model::SlotLabel(p, i));
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
                const int n = errors + warnings;
                const std::string badge = errors ? (n == 1 ? TF("$MTFUI_Tiers_ErrorOne", n) : TF("$MTFUI_Tiers_ErrorMany", n))
                                                 : (n == 1 ? TF("$MTFUI_Tiers_WarningOne", n) : TF("$MTFUI_Tiers_WarningMany", n));
                const float tw = CalcTextSize(title.c_str()).x;
                ImDrawListManager::AddText(dl, ImVec2{ start.x + pad + tw + 12.0f, start.y + 4.0f },
                    GetColorU32(errors ? kError : kWarn), badge.c_str());
            }
            const std::string summary = unused ? T("$MTFUI_Tiers_UnusedSummary") : model::DescribeSlot(app.cat, p, i);
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

        void SetGradientCondition(const model::GradientCondition& c) {
            grad.conditionId = c.key;
            grad.below = model::IsBelowCondition(*c.entry);
            grad.lo = static_cast<int>(c.entry->params[0].min);
            grad.hi = static_cast<int>(c.entry->params[0].max);
        }

        // Starts from the open preset's first tier when it already uses a threshold condition.
        void InitGradient(App& app, const std::vector<model::GradientCondition>& conditions) {
            const auto& p = app.Preset();
            const model::GradientCondition* pick = nullptr;
            for (const auto& c : conditions) {
                if (p.slots.size() > 1 && p.slots[1].Configured() && model::IEquals(p.slots[1].conditions.front().pluginId, c.key)) {
                    pick = &c;
                    break;
                }
            }
            for (const auto& c : conditions) {
                if (!pick && model::IEquals(c.key, "mtf.attributes:health.below")) {
                    pick = &c;
                }
            }
            if (!pick && !conditions.empty()) {
                pick = &conditions.front();
            }
            if (pick) {
                SetGradientCondition(*pick);
            }
            if (grad.colors.empty()) {
                grad.colors = kHealthBar;
            }
        }

        model::GradientSpec GradientSpecFromState() {
            model::GradientSpec spec;
            spec.conditionId = grad.conditionId;
            spec.below = grad.below;
            spec.lo = std::min(grad.lo, grad.hi);
            spec.hi = std::max(grad.lo, grad.hi);
            spec.steps = grad.steps;
            const auto n = grad.colors.size();
            for (std::size_t i = 0; i < n; ++i) {
                spec.stops.push_back({ n == 1 ? 0.0 : static_cast<double>(i) / (n - 1), grad.colors[i] });
            }
            spec.tint = grad.tint;
            spec.emissive = grad.emissive;
            if (grad.glow) {
                spec.emissiveMult = model::Ramp{ grad.glowLow, grad.glowHigh };
            }
            if (grad.fade) {
                spec.alpha = model::Ramp{ static_cast<double>(grad.alphaLow), static_cast<double>(grad.alphaHigh) };
            }
            return spec;
        }

        void DrawGradientStrip(const model::GradientSpec& spec, float width) {
            const auto plan = model::PlanGradient(spec);
            const int bands = static_cast<int>(plan.tiers.size()) + 1;
            const float h = GetFrameHeight();
            const ImVec2 at = GetCursorScreenPos();
            auto* dl = GetWindowDrawList();
            const float cell = width / static_cast<float>(bands);
            for (int j = 0; j < bands; ++j) {
                const double t = bands == 1 ? 0.0 : static_cast<double>(j) / (bands - 1);
                ImDrawListManager::AddRectFilled(dl, ImVec2{ at.x + cell * j, at.y }, ImVec2{ at.x + cell * (j + 1), at.y + h },
                    render::ToImColor(model::SampleGradient(spec.stops, t), 1.0f), 0.0f, 0);
            }
            ImDrawListManager::AddRect(dl, at, ImVec2{ at.x + width, at.y + h }, GetColorU32(ImGuiCol_Border, 1.0f), 0.0f, 0, 1.0f);
            Dummy(ImVec2{ width, h });
            TextDisabled("%s", std::to_string(spec.lo).c_str());
            const std::string high = std::to_string(spec.hi);
            SameLine(width - CalcTextSize(high.c_str()).x);
            TextDisabled("%s", high.c_str());
        }

        void DrawGradientModal(App& app, const char* title, const std::vector<model::GradientCondition>& conditions) {
            if (!BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }
            auto& p = app.Preset();
            const float width = 380.0f;
            TextWrapped("%s", T("$MTFUI_Gradient_Intro"));
            Spacing();

            const model::GradientCondition* current = nullptr;
            for (const auto& c : conditions) {
                if (model::IEquals(c.key, grad.conditionId)) {
                    current = &c;
                }
            }
            auto conditionName = [](const model::GradientCondition& c) { return c.plugin->label + ": " + c.entry->label; };
            SetNextItemWidth(width);
            if (BeginCombo(T("$MTFUI_Gradient_Condition"), current ? conditionName(*current).c_str() : grad.conditionId.c_str())) {
                for (std::size_t i = 0; i < conditions.size(); ++i) {
                    PushID(static_cast<int>(i));
                    if (Selectable(conditionName(conditions[i]).c_str(), &conditions[i] == current)) {
                        SetGradientCondition(conditions[i]);
                        current = &conditions[i];
                    }
                    PopID();
                }
                EndCombo();
            }
            if (!current) {
                TextColored(kWarn, "%s", T("$MTFUI_Gradient_NoCondition"));
            } else {
                const auto& spec = current->entry->params[0];
                int dir = grad.below ? 0 : 1;
                RadioButton(T("$MTFUI_Gradient_Below"), &dir, 0);
                SameLine();
                RadioButton(T("$MTFUI_Gradient_Above"), &dir, 1);
                grad.below = dir == 0;
                HelpMarker(T("$MTFUI_Gradient_DirectionHelp"));

                SetNextItemWidth(width * 0.5f - GetStyle()->ItemInnerSpacing.x);
                SliderInt("##lo", &grad.lo, static_cast<int>(spec.min), static_cast<int>(spec.max), T("$MTFUI_Gradient_From_Fmt"), ImGuiSliderFlags_AlwaysClamp);
                SameLine(0, GetStyle()->ItemInnerSpacing.x);
                SetNextItemWidth(width * 0.5f);
                SliderInt(T("$MTFUI_Gradient_Range"), &grad.hi, static_cast<int>(spec.min), static_cast<int>(spec.max), T("$MTFUI_Gradient_To_Fmt"),
                    ImGuiSliderFlags_AlwaysClamp);

                const int maxSteps = std::max(1, std::min(app.maxSlots - 1, std::abs(grad.hi - grad.lo)));
                grad.steps = std::clamp(grad.steps, 1, maxSteps);
                SetNextItemWidth(width);
                SliderInt(T("$MTFUI_Gradient_Steps"), &grad.steps, 1, maxSteps, "%d", ImGuiSliderFlags_AlwaysClamp);
                const auto planned = model::PlanGradient(GradientSpecFromState()).tiers.size();
                TextDisabled("%s", TF("$MTFUI_Gradient_StepsInfo", planned, std::format("{:.1f}", std::abs(grad.hi - grad.lo) / (planned + 1.0))).c_str());
                if (static_cast<int>(planned) > model::kSlotCount - 1) {
                    TextColored(kWarn, "%s", TF("$MTFUI_Gradient_PastMenu", model::kSlotCount - 1).c_str());
                }
            }

            SeparatorText(T("$MTFUI_Gradient_Colors"));
            HelpMarker(T("$MTFUI_Gradient_ColorsHelp"));
            int remove = -1;
            for (std::size_t i = 0; i < grad.colors.size(); ++i) {
                PushID(static_cast<int>(i));
                SetNextItemWidth(width * 0.6f);
                ColorEditRgb("##color", grad.colors[i]);
                if (grad.colors.size() > 2) {
                    SameLine();
                    if (SmallButton(T("$MTFUI_Gradient_RemoveColor"))) {
                        remove = static_cast<int>(i);
                    }
                }
                PopID();
            }
            if (remove >= 0) {
                grad.colors.erase(grad.colors.begin() + remove);
            }
            BeginDisabled(grad.colors.size() >= 4);
            if (SmallButton(T("$MTFUI_Gradient_AddColor"))) {
                grad.colors.push_back(grad.colors.back());
            }
            EndDisabled();
            SameLine();
            if (SmallButton(T("$MTFUI_Gradient_Reverse"))) {
                std::reverse(grad.colors.begin(), grad.colors.end());
            }
            SameLine();
            if (SmallButton(T("$MTFUI_Gradient_HealthBar"))) {
                grad.colors = kHealthBar;
            }
            const auto spec = GradientSpecFromState();
            DrawGradientStrip(spec, width);

            SeparatorText(T("$MTFUI_Gradient_Apply"));
            Checkbox(T("$MTFUI_Gradient_Tint"), &grad.tint);
            SameLine();
            Checkbox(T("$MTFUI_Gradient_Emissive"), &grad.emissive);
            Checkbox(T("$MTFUI_Gradient_Glow"), &grad.glow);
            HelpMarker(T("$MTFUI_Gradient_GlowHelp"));
            if (grad.glow) {
                SetNextItemWidth(width * 0.5f - GetStyle()->ItemInnerSpacing.x);
                SliderFloat("##glowlo", &grad.glowLow, 0.0f, 10.0f, T("$MTFUI_Gradient_LowEnd_Fmt"));
                SameLine(0, GetStyle()->ItemInnerSpacing.x);
                SetNextItemWidth(width * 0.5f);
                SliderFloat("##glowhi", &grad.glowHigh, 0.0f, 10.0f, T("$MTFUI_Gradient_HighEnd_Fmt"));
            }
            Checkbox(T("$MTFUI_Gradient_Opacity"), &grad.fade);
            if (grad.fade) {
                SetNextItemWidth(width * 0.5f - GetStyle()->ItemInnerSpacing.x);
                SliderInt("##alphalo", &grad.alphaLow, 0, 100, T("$MTFUI_Gradient_LowEndPct_Fmt"));
                SameLine(0, GetStyle()->ItemInnerSpacing.x);
                SetNextItemWidth(width * 0.5f);
                SliderInt("##alphahi", &grad.alphaHigh, 0, 100, T("$MTFUI_Gradient_HighEndPct_Fmt"));
            }

            if (p.transitionDuration < 0.5) {
                Spacing();
                TextWrapped("%s", T("$MTFUI_Gradient_TransitionHint"));
                if (SmallButton(T("$MTFUI_Gradient_SetTransition"))) {
                    p.transitionDuration = 1.0;
                }
            }

            Spacing();
            TextColored(kWarn, "%s", T("$MTFUI_Gradient_Replaces"));
            BeginDisabled(!current);
            if (Button(T("$MTFUI_Gradient_Create"), ImVec2{ 120, 0 })) {
                model::ApplyGradient(app.cat, p, spec, app.maxSlots);
                app.ed.slot = p.slots.size() > 1 ? 1 : 0;
                app.Toast(TF("$MTFUI_Toast_GradientMade", static_cast<int>(p.slots.size()) - 1));
                CloseCurrentPopup();
            }
            EndDisabled();
            SameLine();
            if (Button(T("$MTFUI_Common_Cancel"), ImVec2{ 100, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                CloseCurrentPopup();
            }
            EndPopup();
        }

        void DrawPresetHeader(App& app) {
            auto& p = app.Preset();
            const bool dirty = app.Dirty();
            BeginDisabled(!dirty || app.ed.builtin);
            if (Button(app.ed.isNew ? T("$MTFUI_Common_Create") : T("$MTFUI_Common_Save"))) {
                app.Save();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(!dirty);
            if (Button(T("$MTFUI_Header_Revert"))) {
                app.Revert();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(app.ed.undo.empty());
            if (Button(T("$MTFUI_Header_Undo"))) {
                app.Undo();
            }
            EndDisabled();
            SameLine();
            BeginDisabled(app.ed.redo.empty());
            if (Button(T("$MTFUI_Header_Redo"))) {
                app.Redo();
            }
            EndDisabled();
            if (app.ed.builtin) {
                SameLine();
                TextColored(kWarn, "%s", T("$MTFUI_Header_Builtin"));
            } else if (dirty) {
                SameLine();
                TextColored(kWarn, "%s", T("$MTFUI_Unsaved_Title"));
            }

            SetNextItemWidth(260);
            if (InputString(T("$MTFUI_Header_Name"), p.displayName, nullptr, 0, 64) && app.ed.isNew) {
                p.name = app.UniqueName(p.displayName);
            }
            SameLine();
            TextDisabled("%s.json", p.name.c_str());
            if (!app.ed.isNew && model::SanitizePresetName(p.displayName) != p.name) {
                HelpMarker(T("$MTFUI_Header_RenameHelp"));
            }

            SetNextItemWidth(260 - GetFrameHeight() - GetStyle()->ItemInnerSpacing.x);
            InputString("##groupname", p.group, T("$MTFUI_Header_NoGroup"), 0, 64);
            SameLine(0, GetStyle()->ItemInnerSpacing.x);
            if (BeginCombo(T("$MTFUI_Header_Group"), nullptr, ImGuiComboFlags_NoPreview)) {
                if (Selectable(T("$MTFUI_Header_NoGroup"), p.group.empty())) {
                    p.group.clear();
                }
                for (const auto& group : app.Groups()) {
                    if (Selectable(group.c_str(), model::IEquals(group, p.group))) {
                        p.group = group;
                    }
                }
                EndCombo();
            }
            HelpMarker(T("$MTFUI_Header_GroupHelp"));

            const char* gradientTitle = T("$MTFUI_Gradient_Title");
            const auto conditions = model::GradientConditions(app.cat);
            if (Button(T("$MTFUI_Gradient_Open"))) {
                InitGradient(app, conditions);
                OpenPopup(gradientTitle);
            }
            HelpMarker(T("$MTFUI_Gradient_OpenHelp"));
            DrawGradientModal(app, gradientTitle, conditions);

            if (CollapsingHeader((std::string(T("$MTFUI_Options_Title")) + "###presetoptions").c_str())) {
                float duration = static_cast<float>(p.transitionDuration);
                SetNextItemWidth(220);
                if (SliderFloat(T("$MTFUI_Options_Transition"), &duration, 0.0f, 10.0f, T("$MTFUI_Format_Seconds1_Fmt"))) {
                    p.transitionDuration = duration;
                }
                HelpMarker(T("$MTFUI_Options_TransitionHelp"));
                const char* modes[] = { T("$MTFUI_Options_DeathOff"), T("$MTFUI_Options_DeathOverlay"), T("$MTFUI_Options_DeathEmissive"),
                    T("$MTFUI_Options_DeathInverted") };
                int mode = p.fade.enabled ? p.fade.mode + 1 : 0;
                SetNextItemWidth(220);
                if (Combo(T("$MTFUI_Options_OnDeath"), &mode, modes, 4)) {
                    p.fade.enabled = mode > 0;
                    p.fade.mode = std::max(0, mode - 1);
                }
                HelpMarker(T("$MTFUI_Options_OnDeathHelp"));
                if (p.fade.enabled) {
                    int ms = static_cast<int>(p.fade.durationMs);
                    SetNextItemWidth(220);
                    if (SliderInt(T("$MTFUI_Options_DeathFade"), &ms, 100, 10000, T("$MTFUI_Format_Milliseconds_Fmt"))) {
                        p.fade.durationMs = ms;
                    }
                }
                if (Button(T("$MTFUI_Options_CopyJson"))) {
                    app.ExportToClipboard();
                }
                HelpMarker(T("$MTFUI_Options_CopyJsonHelp"));
            }
        }

        void DrawIssues(App& app) {
            if (app.issues.empty()) {
                TextColored(kGood, "%s", T("$MTFUI_Checks_None"));
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
                        SetTooltip("%s", T("$MTFUI_Checks_JumpTip"));
                    }
                    SameLine();
                }
                if (issue.fix.kind != model::LintFix::Kind::None) {
                    if (SmallButton(issue.fix.kind == model::LintFix::Kind::MoveAbove ? T("$MTFUI_Checks_MoveUp") : T("$MTFUI_Checks_Enable"))) {
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
            TextWrapped("%s", T("$MTFUI_Tiers_PickPreset"));
            Spacing();
            TextWrapped("%s", T("$MTFUI_Tiers_Explainer"));
            return;
        }
        DrawPresetHeader(app);
        const float rowWidth = GetContentRegionAvail().x;
        SeparatorText(T("$MTFUI_Tiers_Base"));
        HelpMarker(T("$MTFUI_Tiers_BaseHelp"));
        DrawTierRow(app, 0, rowWidth);
        SeparatorText(T("$MTFUI_Tiers_Header"));
        HelpMarker(T("$MTFUI_Tiers_HeaderHelp"));
        for (int i = 1; i < static_cast<int>(app.Preset().slots.size()); ++i) {
            if (i == model::kSlotCount) {
                SeparatorText(T("$MTFUI_Tiers_Extra"));
                HelpMarker(TF("$MTFUI_Tiers_ExtraHelp", model::kSlotCount - 1).c_str());
            }
            DrawTierRow(app, i, rowWidth);
        }
        auto& p = app.Preset();
        const bool full = static_cast<int>(p.slots.size()) >= app.maxSlots;
        BeginDisabled(full);
        if (Button(T("$MTFUI_Tiers_Add"))) {
            p.slots.push_back(model::DefaultSlot());
            app.ed.slot = static_cast<int>(p.slots.size()) - 1;
        }
        EndDisabled();
        HelpMarker(full ? TF("$MTFUI_Tiers_AddFull", app.maxSlots - 1).c_str() : TF("$MTFUI_Tiers_AddHelp", model::kSlotCount - 1, app.maxSlots - 1).c_str());
        SeparatorText(T("$MTFUI_Checks_Title"));
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
