#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "game/Targets.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        struct Candidates {
            std::optional<game::ActorInfo> crosshair;
            std::optional<game::ActorInfo> console;
            std::vector<game::ActorInfo> nearby;
            bool loading = false;
        };
        Candidates g_candidates;

        void RefreshCandidates() {
            g_candidates.loading = true;
            game::MtfBridge::RunOnGame([] {
                Candidates c;
                c.crosshair = game::CrosshairActor();
                c.console = game::ConsoleActor();
                c.nearby = game::NearbyActors();
                game::MtfBridge::PostToUi([c] {
                    g_candidates = c;
                    g_candidates.loading = false;
                });
            });
        }

        std::string Label(const game::ActorInfo& a) {
            if (a.player) {
                return a.name;
            }
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s  (%.0f m)", a.name.c_str(), a.distance / 70.0f);
            return buf;
        }
    }

    void DrawTargetBar(App& app) {
        const bool mtf = game::MtfBridge::Get().Installed();
        AlignTextToFramePadding();
        TextUnformatted("Target");
        SameLine();
        SetNextItemWidth(260);
        if (BeginCombo("##target", app.targetName.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (IsWindowAppearing()) {
                RefreshCandidates();
            }
            if (Selectable("You", app.targetId == 0x14)) {
                app.SetTarget(0x14, "You");
            }
            if (g_candidates.crosshair) {
                const auto& a = *g_candidates.crosshair;
                if (Selectable(("Crosshair: " + a.name + "##xh").c_str(), app.targetId == a.id)) {
                    app.SetTarget(a.id, a.name);
                }
            }
            if (g_candidates.console && (!g_candidates.crosshair || g_candidates.console->id != g_candidates.crosshair->id)) {
                const auto& a = *g_candidates.console;
                if (Selectable(("Console selection: " + a.name + "##con").c_str(), app.targetId == a.id)) {
                    app.SetTarget(a.id, a.name);
                }
            }
            SeparatorText("Nearby");
            if (g_candidates.loading) {
                TextDisabled("Looking around...");
            } else if (g_candidates.nearby.empty()) {
                TextDisabled("Nobody nearby");
            }
            for (const auto& a : g_candidates.nearby) {
                PushID(static_cast<int>(a.id));
                if (Selectable(Label(a).c_str(), app.targetId == a.id)) {
                    app.SetTarget(a.id, a.name);
                }
                PopID();
            }
            EndCombo();
        }
        HelpMarker("Who live previews and Apply act on. Crosshair is whoever you were looking at when you opened the menu; "
                   "console selection is the actor you clicked in the console.");

        SameLine(0, 20);
        BeginDisabled(!mtf);
        if (Checkbox("Live preview", &app.livePreview)) {
            app.SaveSettings();
            if (!app.livePreview) {
                game::LivePreview::Get().Stop();
            }
        }
        HelpMarker("Shows the selected tier's tattoo, colors and pulse on the target while you edit, through MTF itself. "
                   "Uses a temporary preset that is removed when you close this menu or load a save.");
        if (app.livePreview) {
            SameLine();
            Checkbox("with effects", &app.previewEffects);
            HelpMarker("Also run the tier's effects (lights, sounds, spells) in the preview.");
        }
        EndDisabled();

        SameLine(0, 20);
        const bool canApply = mtf && app.HasPreset() && !app.ed.isNew;
        BeginDisabled(!canApply);
        if (Button("Apply to target")) {
            if (app.Dirty() && !app.ed.builtin) {
                app.Save();
            }
            app.ApplyToTarget(app.Preset().name);
        }
        EndDisabled();
        if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            SetTooltip(canApply ? "Saves if needed, then applies this preset to the target. MTF keeps it on them until removed." : "Open a saved preset first");
        }

        SameLine();
        BeginDisabled(!mtf);
        const std::string appliedLabel = app.appliedKnown ? "Applied (" + std::to_string(app.applied.size()) + ")" : "Applied";
        if (Button(appliedLabel.c_str())) {
            app.RefreshApplied();
            OpenPopup("##applied");
        }
        EndDisabled();
        if (BeginPopup("##applied")) {
            SeparatorText(("Presets on " + app.targetName).c_str());
            if (!app.appliedKnown) {
                TextDisabled("Asking MTF...");
            } else if (app.applied.empty()) {
                TextDisabled("None");
            }
            for (const auto& name : app.applied) {
                PushID(name.c_str());
                const auto it = std::find_if(app.library.begin(), app.library.end(), [&](const model::PresetFileInfo& i) { return model::IEquals(i.name, name); });
                TextUnformatted(it != app.library.end() ? it->displayName.c_str() : name.c_str());
                SameLine(260);
                if (SmallButton("Edit")) {
                    app.Open(name);
                    app.RequestTab(Tab::Presets);
                    CloseCurrentPopup();
                }
                SameLine();
                if (SmallButton("Remove")) {
                    app.RemoveFromTarget(name);
                }
                PopID();
            }
            if (!app.applied.empty()) {
                Separator();
                if (Button("Remove all")) {
                    app.RemoveAllFromTarget();
                }
            }
            Separator();
            if (Button("Clear stuck effects")) {
                game::MtfBridge::Get().StopShadersOn(app.targetId);
                app.Toast("Cleared MTF effect shaders on " + app.targetName);
            }
            HelpMarker("Stops every effect shader MTF can play (invisibility, ghost, flames...) on the target and resets its alpha.");
            EndPopup();
        }

        if (game::LivePreview::Get().Active()) {
            SameLine();
            if (Button("Stop preview")) {
                app.livePreview = false;
                game::LivePreview::Get().Stop();
            }
        }
        Separator();
    }
}
