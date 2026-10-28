#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "io/JsonStore.h"
#include "model/Slots.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"


namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        struct GeneralTabState {
            float interval = -1.0f;
            int overlayArea = -1;  // area whose slider is being dragged
            int overlayDraft = 0;
            std::string backupSearch;
            std::vector<io::BackupInfo> backups;
            double backupsRead = -100.0;
        };
        GeneralTabState g;

        constexpr ImVec4 kMcmCell{ 0.45f, 0.85f, 0.45f, 0.85f };
        constexpr ImVec4 kPresetCell = kAccent;
        constexpr ImVec4 kPresetCellAlt{ 0.38f, 0.58f, 0.92f, 1.0f };
        constexpr ImVec4 kOtherCell = kWarn;
        constexpr ImVec4 kClashCell = kError;

        std::string PresetLabel(const std::string& name) {
            return model::IEquals(name, game::kPreviewPreset) ? T("$MTFUI_Slots_LivePreview") : name;
        }

        void LegendItem(const ImVec4& color, const char* label) {
            auto* dl = GetWindowDrawList();
            const ImVec2 p = GetCursorScreenPos();
            const float s = GetTextLineHeight();
            ImDrawListManager::AddRectFilled(dl, p, ImVec2{ p.x + s, p.y + s }, GetColorU32(color), 2.0f, 0);
            Dummy(ImVec2{ s, s });
            SameLine();
            TextDisabled("%s", label);
            SameLine(0, 16);
        }

        // One row per area: the start slider, then one cell per RaceMenu slot. While the slider is dragged the
        // cells show where MTF would put everything from the new start.
        void DrawSlotMap(App& app, int area, const game::SlotUsage& usage) {
            const auto& snap = game::MtfBridge::Get().Snapshot();
            const auto& slots = usage.areas[area];
            const std::string name = model::AreaLabel(game::kAreas[area]);
            int start = g.overlayArea == area ? g.overlayDraft : app.OverlayMoveTarget(area).value_or(snap.overlayStart[area]);
            const bool planning = start != slots.start;

            PushID(area);
            AlignTextToFramePadding();
            TextUnformatted(name.c_str());
            SameLine(80);
            SetNextItemWidth(140);
            BeginDisabled(app.OverlayMoveBusy());
            SliderInt("##start", &start, 0, std::max(0, slots.total - 1), T("$MTFUI_Slots_StartsAt_Fmt"));
            if (IsItemActive()) {
                g.overlayArea = area;
                g.overlayDraft = start;
            }
            if (IsItemDeactivatedAfterEdit() && start != snap.overlayStart[area]) {
                app.MoveOverlayStart(area, start);
            }
            EndDisabled();
            if (!IsItemActive() && g.overlayArea == area) {
                g.overlayArea = -1;
            }
            SameLine();

            const auto plan = model::PlanStart(slots, start, game::kPreviewPreset);
            const auto cells = planning ? plan.cells : model::CurrentSlots(slots);
            auto* dl = GetWindowDrawList();
            const ImVec2 p = GetCursorScreenPos();
            const float cell = GetFrameHeight();
            int mine = 0;
            for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
                const auto& c = cells[i];
                const ImVec2 a{ p.x + i * (cell + 3), p.y };
                const ImVec2 b{ a.x + cell, a.y + cell };
                ImVec4 color{};
                bool filled = true;
                if (c.use != model::SlotUse::Free) {
                    ++mine;
                    color = c.other ? kClashCell : c.use == model::SlotUse::Mcm ? kMcmCell : (c.presetIndex % 2 ? kPresetCellAlt : kPresetCell);
                } else if (c.other) {
                    color = kOtherCell;
                } else {
                    filled = false;
                }
                ImDrawListManager::AddRectFilled(dl, a, b, filled ? GetColorU32(color) : GetColorU32(ImGuiCol_FrameBg, 1.0f), 3.0f, 0);
                const std::string n = std::to_string(i);
                ImDrawListManager::AddText(dl, ImVec2{ a.x + 6, a.y + 3 }, filled ? 0xFF000000 : GetColorU32(ImGuiCol_TextDisabled, 1.0f), n.c_str());
                if (IsMouseHoveringRect(a, b)) {
                    std::string use;
                    if (c.use == model::SlotUse::Mcm) {
                        use = T("$MTFUI_Slots_TipMcm");
                    } else if (c.use == model::SlotUse::Preset) {
                        use = PresetLabel(c.preset);
                    } else if (!c.other) {
                        use = T("$MTFUI_Slots_TipFree");
                    }
                    std::string tip;
                    if (!c.other) {
                        tip = TF("$MTFUI_Slots_Tip", n, use);
                    } else if (c.use == model::SlotUse::Free) {
                        tip = TF("$MTFUI_Slots_TipOther", n, c.texture);
                    } else {
                        tip = TF("$MTFUI_Slots_TipClash", n, use, c.texture);
                    }
                    SetTooltip("%s", tip.c_str());
                }
            }
            Dummy(ImVec2{ cells.size() * (cell + 3), cell });
            SameLine();
            if (!plan.fits) {
                TextColored(kError, "%s", TF("$MTFUI_Slots_Needs", plan.needed, start, std::max(0, slots.total - start)).c_str());
            } else {
                TextDisabled("%s", TF("$MTFUI_Slots_Used", mine, slots.total).c_str());
            }

            Indent(80);
            if (!plan.fits) {
                TextColored(kError, "%s", TF(planning ? "$MTFUI_Slots_WontMove" : "$MTFUI_Slots_CutOff", plan.needed, name, start).c_str());
            }
            if (!plan.covered.empty()) {
                const auto list = model::ListSlots(plan.covered);
                TextColored(kWarn, "%s", (plan.covered.size() == 1 ? TF("$MTFUI_Slots_CoveredOne", list, start) : TF("$MTFUI_Slots_CoveredMany", list, start)).c_str());
            }
            if (!plan.exposed.empty()) {
                const auto list = model::ListSlots(plan.exposed);
                const int next = start + plan.needed;
                TextColored(kWarn, "%s", (plan.exposed.size() == 1 ? TF("$MTFUI_Slots_ExposedOne", list, next) : TF("$MTFUI_Slots_ExposedMany", list, next)).c_str());
            }
            Unindent(80);
            PopID();
        }

        // "20261022-153012-123" -> "2026-10-22 15:30:12"
        std::string StampText(const std::string& s) {
            if (s.size() < 15) {
                return s;
            }
            return s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2) + " " + s.substr(9, 2) + ":" + s.substr(11, 2) + ":" + s.substr(13, 2);
        }

        void DrawRecovery(App& app) {
            SeparatorText(T("$MTFUI_Recovery_Deleted"));
            if (app.hiddenPresets.empty()) {
                TextDisabled("%s", T("$MTFUI_Common_None"));
            }
            std::string undelete;
            for (const auto& info : app.hiddenPresets) {
                PushID(info.name.c_str());
                if (SmallButton(T("$MTFUI_Common_Restore"))) {
                    undelete = info.name;
                }
                SameLine();
                Text("%s", info.displayName.c_str());
                SameLine();
                TextDisabled("%s.json", info.name.c_str());
                PopID();
            }
            if (!undelete.empty()) {
                app.Undelete(undelete);
            }

            SeparatorText(T("$MTFUI_Recovery_Backups"));
            TextWrapped("%s", T("$MTFUI_Recovery_BackupsIntro"));
            if (Button(T("$MTFUI_Recovery_Paste"))) {
                app.ImportFromClipboard();
            }
            HelpMarker(T("$MTFUI_Recovery_PasteHelp"));
            SameLine();
            if (Button(T("$MTFUI_Recovery_OpenFolder"))) {
                std::error_code ec;
                const auto dir = app.paths.uiDir / "backups";
                fs::create_directories(dir, ec);
                ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (!CollapsingHeader((std::string(T("$MTFUI_Recovery_RestoreBackup")) + "###restorebackup").c_str())) {
                return;
            }
            if (GetTime() - g.backupsRead > 2.0) {
                g.backups = io::ListBackups(app.paths.uiDir / "backups");
                g.backupsRead = GetTime();
            }
            if (g.backups.empty()) {
                TextDisabled("%s", T("$MTFUI_Recovery_NoBackups"));
                return;
            }
            SearchBox("##backupsearch", g.backupSearch, 260.0f, T("$MTFUI_Recovery_Filter"));
            std::optional<io::BackupInfo> restore;
            if (BeginTable("##backups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, ImVec2{ 0, 260 })) {
                TableSetupColumn(T("$MTFUI_Recovery_ColPreset"), ImGuiTableColumnFlags_WidthStretch);
                TableSetupColumn(T("$MTFUI_Recovery_ColSaved"), ImGuiTableColumnFlags_WidthFixed, CalcTextSize("0000-00-00 00:00:00").x);
                TableSetupColumn("##restore", ImGuiTableColumnFlags_WidthFixed, CalcTextSize(T("$MTFUI_Common_Restore")).x + 12.0f);
                TableHeadersRow();
                int row = 0;
                for (const auto& b : g.backups) {
                    if (!MatchesSearch(b.stem, g.backupSearch)) {
                        continue;
                    }
                    PushID(row++);
                    TableNextRow();
                    TableNextColumn();
                    const auto* live = app.FindLibrary(b.stem);
                    TextUnformatted(b.stem.c_str());
                    if (!live) {
                        SameLine();
                        TextDisabled("%s", T("$MTFUI_Recovery_DeletedTag"));
                    }
                    TableNextColumn();
                    TextUnformatted(StampText(b.stamp).c_str());
                    TableNextColumn();
                    if (SmallButton(T("$MTFUI_Common_Restore"))) {
                        restore = b;
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        SetTooltip("%s", TF("$MTFUI_Recovery_RestoreTip", b.stem).c_str());
                    }
                    PopID();
                }
                EndTable();
            }
            if (restore && app.RestoreBackup(restore->path, restore->stem)) {
                g.backupsRead = -100.0;
            }
        }
    }

    void DrawGeneralTab(App& app) {
        auto& bridge = game::MtfBridge::Get();
        const auto& snap = bridge.Snapshot();

        if (!bridge.Installed()) {
            TextColored(kError, "%s", T("$MTFUI_General_NoMtf"));
            TextWrapped("%s", T("$MTFUI_General_NoMtfPresets"));
            DrawRecovery(app);
            return;
        }
        if (!snap.fresh) {
            TextDisabled("%s", T("$MTFUI_General_Reading"));
            DrawRecovery(app);
            return;
        }

        SeparatorText(T("$MTFUI_General_Framework"));
        bool active = snap.modActive;
        if (Checkbox(T("$MTFUI_General_Enabled"), &active)) {
            bridge.SetModActive(active);
        }
        HelpMarker(T("$MTFUI_General_EnabledHelp"));
        SameLine(260);
        bool debug = snap.debugMode;
        if (Checkbox(T("$MTFUI_General_Debug"), &debug)) {
            bridge.SetDebugMode(debug);
        }
        HelpMarker(T("$MTFUI_General_DebugHelp"));

        if (g.interval < 0.0f || !IsAnyItemActive()) {
            g.interval = snap.updateInterval;
        }
        SetNextItemWidth(260);
        SliderFloat(T("$MTFUI_General_Interval"), &g.interval, 0.1f, 45.0f, T("$MTFUI_Format_Seconds1_Fmt"), ImGuiSliderFlags_Logarithmic);
        if (IsItemDeactivatedAfterEdit()) {
            bridge.SetUpdateInterval(g.interval);
        }
        HelpMarker(T("$MTFUI_General_IntervalHelp"));

        if (snap.skyrimNet) {
            bool persist = snap.snPersist;
            if (Checkbox(T("$MTFUI_General_SkyrimNet"), &persist)) {
                bridge.SetSkyrimNetPersist(persist);
            }
            HelpMarker(T("$MTFUI_General_SkyrimNetHelp"));
        }

        SeparatorText(T("$MTFUI_General_OverlaySlots"));
        TextWrapped("%s", T("$MTFUI_General_OverlaySlotsIntro"));
        HelpMarker(T("$MTFUI_General_OverlaySlotsHelp"));
        app.WantSlotUsage();
        if (const auto* usage = app.SlotUsage()) {
            LegendItem(kMcmCell, T("$MTFUI_Slots_LegendMcm"));
            LegendItem(kPresetCell, T("$MTFUI_Slots_LegendPresets"));
            LegendItem(kOtherCell, T("$MTFUI_Slots_LegendOther"));
            LegendItem(kClashCell, T("$MTFUI_Slots_LegendClash"));
            NewLine();
            for (int a = 0; a < 4; ++a) {
                DrawSlotMap(app, a, *usage);
            }
        } else {
            TextDisabled("%s", T("$MTFUI_General_ReadingSlots"));
        }

        SeparatorText(T("$MTFUI_General_VisualPacks"));
        TextUnformatted(TF("$MTFUI_General_PackCount", snap.visualPackCount, app.cat.packs.size()).c_str());
        if (Button(T("$MTFUI_General_ReloadPacks"))) {
            bridge.ReloadVisualPacks();
            app.ReloadAll();
            render::DdsCache::Get().Clear();
            app.Toast(T("$MTFUI_Toast_ReloadingPacks"));
        }
        HelpMarker(T("$MTFUI_General_ReloadPacksHelp"));
        SameLine();
        bool rescan = snap.rescanOnLoad;
        if (Checkbox(T("$MTFUI_General_Rescan"), &rescan)) {
            bridge.SetRescanOnLoad(rescan);
        }

        SeparatorText(T("$MTFUI_General_Target"));
        TextUnformatted(TF("$MTFUI_General_CurrentTarget", app.targetName).c_str());
        if (Button(T("$MTFUI_General_RemoveAll"))) {
            app.RemoveAllFromTarget();
        }
        SameLine();
        if (Button(T("$MTFUI_Common_ClearStuckEffects"))) {
            bridge.StopShadersOn(app.targetId);
            app.Toast(TF("$MTFUI_Toast_ShadersStopped", app.targetName));
        }
        HelpMarker(T("$MTFUI_General_ClearEffectsHelp"));

        SeparatorText(T("$MTFUI_General_ThisMenu"));
        SetNextItemWidth(260);
        ThumbSizeSlider(T("$MTFUI_General_ThumbSize"), app.settings.thumbSize);
        if (IsItemDeactivatedAfterEdit()) {
            app.SaveSettings();
        }
        if (Checkbox(T("$MTFUI_General_Pause"), &app.settings.pauseGame)) {
            app.SaveSettings();
            app.Toast(T("$MTFUI_Toast_AfterRestart"));
        }
        if (Checkbox(T("$MTFUI_General_SplitPanels"), &app.settings.splitPanels)) {
            app.SaveSettings();
        }
        HelpMarker(T("$MTFUI_General_SplitPanelsHelp"));
        if (app.settings.splitPanels) {
            SameLine();
            if (Button(T("$MTFUI_General_ResetLayout"))) {
                app.ResetPanelLayout();
            }
        }
        SetNextItemWidth(260);
        SliderInt(T("$MTFUI_General_Opacity"), &app.settings.panelOpacity, 20, 100, "%d%%");
        if (IsItemDeactivatedAfterEdit()) {
            app.SaveSettings();
        }
        HelpMarker(T("$MTFUI_General_OpacityHelp"));
        if (Button(T("$MTFUI_General_ClearCache"))) {
            render::DdsCache::Get().Clear();
        }
        HelpMarker(T("$MTFUI_General_ClearCacheHelp"));

        DrawRecovery(app);
    }
}
