#include "game/MtfBridge.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"


namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        float g_interval = -1.0f;

        int LayersUsedIn(const App& app, const std::string& area) {
            std::size_t most = 0;
            for (const auto& p : app.cat.packs) {
                if (p.area != area) {
                    continue;
                }
                for (const auto& e : p.entries) {
                    most = std::max(most, e.textures.size());
                }
            }
            return static_cast<int>(std::min<std::size_t>(most, model::kLayerCount));
        }

        void DrawSlotMap(App& app, int area, int capacity) {
            auto& bridge = game::MtfBridge::Get();
            const auto& snap = bridge.Snapshot();
            const std::string name = game::kAreas[area];
            const int used = LayersUsedIn(app, name);
            int start = snap.overlayStart[area];

            PushID(area);
            AlignTextToFramePadding();
            TextUnformatted(name.c_str());
            SameLine(80);
            SetNextItemWidth(140);
            if (SliderInt("##start", &start, 0, std::max(0, capacity - 1), "starts at %d")) {
                bridge.SetOverlaySlot(area, start);
            }
            SameLine();

            auto* dl = GetWindowDrawList();
            const ImVec2 p = GetCursorScreenPos();
            const float cell = GetFrameHeight();
            for (int i = 0; i < capacity; ++i) {
                const bool mine = used > 0 && i >= start && i < start + used;
                const ImVec2 a{ p.x + i * (cell + 3), p.y };
                const ImVec2 b{ a.x + cell, a.y + cell };
                ImDrawListManager::AddRectFilled(dl, a, b, mine ? GetColorU32(kAccent) : GetColorU32(ImGuiCol_FrameBg, 1.0f), 3.0f, 0);
                const std::string n = std::to_string(i);
                ImDrawListManager::AddText(dl, ImVec2{ a.x + 6, a.y + 3 }, mine ? 0xFF000000 : GetColorU32(ImGuiCol_TextDisabled, 1.0f), n.c_str());
            }
            Dummy(ImVec2{ capacity * (cell + 3), cell });
            SameLine();
            if (used == 0) {
                TextDisabled("no packs for this area");
            } else if (start + used > capacity) {
                TextColored(kError, "needs %d slots, only %d left", used, capacity - start);
            } else {
                TextDisabled("%d used", used);
            }
            PopID();
        }
    }

    void DrawGeneralTab(App& app) {
        auto& bridge = game::MtfBridge::Get();
        const auto& snap = bridge.Snapshot();

        if (!bridge.Installed()) {
            TextColored(kError, "MagicTattoosFramework.esp is not loaded, so these settings are unavailable.");
            TextWrapped("Presets can still be edited; they are plain files in Data/SKSE/Plugins/StorageUtilData/MagicTattoosFramework/presets.");
            return;
        }
        if (!snap.fresh) {
            TextDisabled("Reading MTF settings...");
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

        if (g_interval < 0.0f || !IsAnyItemActive()) {
            g_interval = snap.updateInterval;
        }
        SetNextItemWidth(260);
        SliderFloat("Update interval", &g_interval, 0.1f, 45.0f, "%.1f s", ImGuiSliderFlags_Logarithmic);
        if (IsItemDeactivatedAfterEdit()) {
            bridge.SetUpdateInterval(g_interval);
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
        TextWrapped("Tattoos are drawn into RaceMenu overlay slots. Pick where MTF's block starts so it does not overlap "
                    "other mods that use overlays (body paint, dirt, tattoos from RaceMenu).");
        HelpMarker("Slot counts come from skee64.ini / skee64_custom.ini ([Overlays/Body] iNumOverlays, ...). "
                   "Highlighted cells are the slots MTF draws into, based on the most layers any installed tattoo uses.");
        const auto ctx = app.LintCtx();
        for (int a = 0; a < 4; ++a) {
            const auto it = ctx.overlayCapacity.find(game::kAreas[a]);
            DrawSlotMap(app, a, it == ctx.overlayCapacity.end() ? 3 : it->second);
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
        SliderInt("Thumbnail size", &app.settings.thumbSize, 64, 192, "%d px");
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
        if (Button("Open backups folder")) {
            std::error_code ec;
            const auto dir = app.paths.uiDir / "backups";
            fs::create_directories(dir, ec);
            ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        HelpMarker("Every save and delete keeps the previous file here (last 10 per preset).");
        SameLine();
        if (Button("Clear texture cache")) {
            render::DdsCache::Get().Clear();
        }
    }
}
