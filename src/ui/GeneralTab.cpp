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

        std::string Holds(const std::vector<int>& slots) {
            return slots.size() == 1 ? "Slot " + model::ListSlots(slots) + " holds an overlay MTF didn't draw"
                                     : "Slots " + model::ListSlots(slots) + " hold overlays MTF didn't draw";
        }

        std::string PresetLabel(const std::string& name) {
            return model::IEquals(name, game::kPreviewPreset) ? "Live preview" : name;
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
            const std::string name = game::kAreas[area];
            int start = g.overlayArea == area ? g.overlayDraft : app.OverlayMoveTarget(area).value_or(snap.overlayStart[area]);
            const bool planning = start != slots.start;

            PushID(area);
            AlignTextToFramePadding();
            TextUnformatted(name.c_str());
            SameLine(80);
            SetNextItemWidth(140);
            BeginDisabled(app.OverlayMoveBusy());
            SliderInt("##start", &start, 0, std::max(0, slots.total - 1), "starts at %d");
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
                    std::string tip = "Slot " + n + ": ";
                    if (c.use == model::SlotUse::Mcm) {
                        tip += "character default (the preset loaded in MTF's MCM)";
                    } else if (c.use == model::SlotUse::Preset) {
                        tip += PresetLabel(c.preset);
                    } else if (!c.other) {
                        tip += "free";
                    }
                    if (c.other) {
                        tip += std::string(c.use == model::SlotUse::Free ? "" : "\nMTF would draw over ") + "an overlay MTF didn't draw\n" + c.texture;
                    }
                    SetTooltip("%s", tip.c_str());
                }
            }
            Dummy(ImVec2{ cells.size() * (cell + 3), cell });
            SameLine();
            if (!plan.fits) {
                TextColored(kError, "needs %d from slot %d, only %d left", plan.needed, start, std::max(0, slots.total - start));
            } else {
                TextDisabled("%d of %d used by MTF", mine, slots.total);
            }

            Indent(80);
            if (!plan.fits) {
                TextColored(kError, "%s Your character default and applied presets need %d %s slots from slot %d.",
                    planning ? "Won't move:" : "Some tattoos are cut off:", plan.needed, name.c_str(), start);
            }
            if (!plan.covered.empty()) {
                TextColored(kWarn, "%s. From slot %d, MTF would draw over %s.", Holds(plan.covered).c_str(), start, plan.covered.size() == 1 ? "it" : "them");
            }
            if (!plan.exposed.empty()) {
                TextColored(kWarn, "%s. The next preset applied to you goes in from slot %d and can draw over %s.", Holds(plan.exposed).c_str(),
                    start + plan.needed, plan.exposed.size() == 1 ? "it" : "them");
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
            SeparatorText("Deleted presets");
            if (app.hiddenPresets.empty()) {
                TextDisabled("None");
            }
            std::string undelete;
            for (const auto& info : app.hiddenPresets) {
                PushID(info.name.c_str());
                if (SmallButton("Restore")) {
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

            SeparatorText("Backups");
            TextWrapped("Every save, delete and restore keeps the file it replaces, the last 10 per preset.");
            if (Button("Paste a preset from the clipboard")) {
                app.ImportFromClipboard();
            }
            HelpMarker("Opens preset JSON from the clipboard (for example from Copy as JSON under Preset options) as a new, unsaved preset.");
            SameLine();
            if (Button("Open backups folder")) {
                std::error_code ec;
                const auto dir = app.paths.uiDir / "backups";
                fs::create_directories(dir, ec);
                ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            if (!CollapsingHeader("Restore a backup")) {
                return;
            }
            if (GetTime() - g.backupsRead > 2.0) {
                g.backups = io::ListBackups(app.paths.uiDir / "backups");
                g.backupsRead = GetTime();
            }
            if (g.backups.empty()) {
                TextDisabled("No backups yet.");
                return;
            }
            SearchBox("##backupsearch", g.backupSearch, 260.0f, "Filter by preset");
            std::optional<io::BackupInfo> restore;
            if (BeginTable("##backups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV, ImVec2{ 0, 260 })) {
                TableSetupColumn("Preset", ImGuiTableColumnFlags_WidthStretch);
                TableSetupColumn("Saved", ImGuiTableColumnFlags_WidthFixed, CalcTextSize("0000-00-00 00:00:00").x);
                TableSetupColumn("##restore", ImGuiTableColumnFlags_WidthFixed, CalcTextSize("Restore").x + 12.0f);
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
                        TextDisabled("(deleted)");
                    }
                    TableNextColumn();
                    TextUnformatted(StampText(b.stamp).c_str());
                    TableNextColumn();
                    if (SmallButton("Restore")) {
                        restore = b;
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        SetTooltip("Replace %s.json with this version. The current file is backed up first.", b.stem.c_str());
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
            TextColored(kError, "MagicTattoosFramework.esp is not loaded, so these settings are unavailable.");
            TextWrapped("Presets can still be edited; they are plain files in Data/SKSE/Plugins/StorageUtilData/MagicTattoosFramework/presets.");
            DrawRecovery(app);
            return;
        }
        if (!snap.fresh) {
            TextDisabled("Reading MTF settings...");
            DrawRecovery(app);
            return;
        }

        SeparatorText("Framework");
        bool active = snap.modActive;
        if (Checkbox("Enabled", &active)) {
            bridge.SetModActive(active);
        }
        HelpMarker("Master switch. While off, MTF stops evaluating conditions and drawing tattoos.");
        SameLine(260);
        bool debug = snap.debugMode;
        if (Checkbox("Debug messages", &debug)) {
            bridge.SetDebugMode(debug);
        }
        HelpMarker("Writes extra lines to the Papyrus log and shows notifications. Leave off for normal play.");

        if (g.interval < 0.0f || !IsAnyItemActive()) {
            g.interval = snap.updateInterval;
        }
        SetNextItemWidth(260);
        SliderFloat("Update interval", &g.interval, 0.1f, 45.0f, "%.1f s", ImGuiSliderFlags_Logarithmic);
        if (IsItemDeactivatedAfterEdit()) {
            bridge.SetUpdateInterval(g.interval);
        }
        HelpMarker("How often MTF re-checks conditions. Lower reacts faster but costs more script time.");

        if (snap.skyrimNet) {
            bool persist = snap.snPersist;
            if (Checkbox("SkyrimNet: remember visual changes", &persist)) {
                bridge.SetSkyrimNetPersist(persist);
            }
            HelpMarker("Lets SkyrimNet-driven visual changes persist across tier changes.");
        }

        SeparatorText("Overlay slots");
        TextWrapped("Your tattoos are drawn into RaceMenu overlay slots. Pick where MTF's block starts so it does not overlap "
                    "overlays from RaceMenu or other mods (body paint, dirt, other tattoos).");
        HelpMarker("MTF first reserves room for your character default (the preset loaded in its MCM; remove it under Applied), "
                   "then stacks each preset applied to you above it. "
                   "Changing the start moves all of them, keeping their tiers. NPCs aren't affected: MTF stacks their tattoos "
                   "above the highest slot already in use.\n\n"
                   "More slots: the counts are RaceMenu's. Open Data/SKSE/Plugins/skee64_custom.ini in a text editor (MTF ships "
                   "one; create it if it's missing) and add or change these lines, keeping bPlayerOnly=0 under [Overlays]:\n\n"
                   "[Overlays/Body]\niNumOverlays=12\n[Overlays/Face]\niNumOverlays=6\n[Overlays/Hands]\niNumOverlays=6\n"
                   "[Overlays/Feet]\niNumOverlays=6\n\n"
                   "Restart the game afterwards; RaceMenu only reads it at startup. The file comes with MTF, so a mod manager may "
                   "flag the edit or an MTF update may replace it. Putting your copy in its own small mod that wins the conflict "
                   "keeps it safe.");
        app.WantSlotUsage();
        if (const auto* usage = app.SlotUsage()) {
            LegendItem(kMcmCell, "character default");
            LegendItem(kPresetCell, "applied presets");
            LegendItem(kOtherCell, "not drawn by MTF");
            LegendItem(kClashCell, "MTF draws over another overlay");
            NewLine();
            for (int a = 0; a < 4; ++a) {
                DrawSlotMap(app, a, *usage);
            }
        } else {
            TextDisabled("Reading your overlay slots...");
        }

        SeparatorText("Visual packs");
        Text("%d pack(s) loaded by MTF, %d found on disk", snap.visualPackCount, static_cast<int>(app.cat.packs.size()));
        if (Button("Reload packs")) {
            bridge.ReloadVisualPacks();
            app.ReloadAll();
            render::DdsCache::Get().Clear();
            app.Toast("Reloading visual packs");
        }
        HelpMarker("Use after installing or editing a visual pack JSON without restarting.");
        SameLine();
        bool rescan = snap.rescanOnLoad;
        if (Checkbox("Rescan on every load", &rescan)) {
            bridge.SetRescanOnLoad(rescan);
        }

        SeparatorText("Target");
        Text("Current target: %s", app.targetName.c_str());
        if (Button("Remove all presets")) {
            app.RemoveAllFromTarget();
        }
        SameLine();
        if (Button("Clear stuck effects")) {
            bridge.StopShadersOn(app.targetId);
            app.Toast("Stopped MTF shader effects on " + app.targetName);
        }
        HelpMarker("Stops any MTF glow or shader effect that kept running after its tier ended.");

        SeparatorText("This menu");
        SetNextItemWidth(260);
        ThumbSizeSlider("Thumbnail size", app.settings.thumbSize);
        if (IsItemDeactivatedAfterEdit()) {
            app.SaveSettings();
        }
        if (Checkbox("Pause the game while the window is open", &app.settings.pauseGame)) {
            app.SaveSettings();
            app.Toast("Takes effect after restarting the game");
        }
        if (Checkbox("Separate panels in the standalone window", &app.settings.splitPanels)) {
            app.SaveSettings();
        }
        HelpMarker("Shows Presets, Tiers and Tier editor as their own windows that you can place around the target, "
                   "instead of one big tabbed window.");
        if (app.settings.splitPanels) {
            SameLine();
            if (Button("Reset panel layout")) {
                app.ResetPanelLayout();
            }
        }
        SetNextItemWidth(260);
        SliderInt("Window opacity", &app.settings.panelOpacity, 20, 100, "%d%%");
        if (IsItemDeactivatedAfterEdit()) {
            app.SaveSettings();
        }
        HelpMarker("Background opacity of the standalone window and its panels, so the target shows through.");
        if (Button("Clear texture cache")) {
            render::DdsCache::Get().Clear();
        }
        HelpMarker("Reloads tattoo thumbnails from disk, after you replace a texture file.");

        DrawRecovery(app);
    }
}
