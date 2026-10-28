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
            return TF("$MTFUI_Target_Distance", a.name, a.distance / 70.0f);
        }
    }

    void DrawTargetBar(App& app) {
        const bool mtf = game::MtfBridge::Get().Installed();
        AlignTextToFramePadding();
        TextUnformatted(T("$MTFUI_Target_Label"));
        SameLine();
        SetNextItemWidth(260);
        if (BeginCombo("##target", app.targetName.c_str(), ImGuiComboFlags_HeightLarge)) {
            if (IsWindowAppearing()) {
                RefreshCandidates();
            }
            if (Selectable(T("$MTFUI_Target_You"), app.targetId == 0x14)) {
                app.SetTarget(0x14, T("$MTFUI_Target_You"));
            }
            if (g_candidates.crosshair) {
                const auto& a = *g_candidates.crosshair;
                if (Selectable((TF("$MTFUI_Target_Crosshair", a.name) + "##xh").c_str(), app.targetId == a.id)) {
                    app.SetTarget(a.id, a.name);
                }
            }
            if (g_candidates.console && (!g_candidates.crosshair || g_candidates.console->id != g_candidates.crosshair->id)) {
                const auto& a = *g_candidates.console;
                if (Selectable((TF("$MTFUI_Target_Console", a.name) + "##con").c_str(), app.targetId == a.id)) {
                    app.SetTarget(a.id, a.name);
                }
            }
            SeparatorText(T("$MTFUI_Target_Nearby"));
            if (g_candidates.loading) {
                TextDisabled("%s", T("$MTFUI_Target_Looking"));
            } else if (g_candidates.nearby.empty()) {
                TextDisabled("%s", T("$MTFUI_Target_NobodyNearby"));
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
        HelpMarker(T("$MTFUI_Target_Help"));
        SameLine();
        if (Checkbox(T("$MTFUI_Target_Hold"), &app.settings.holdTarget)) {
            app.SaveSettings();
        }
        HelpMarker(T("$MTFUI_Target_HoldHelp"));

        SameLine(0, 20);
        BeginDisabled(!mtf);
        if (Checkbox(T("$MTFUI_Target_LivePreview"), &app.livePreview)) {
            app.SaveSettings();
            if (!app.livePreview) {
                game::LivePreview::Get().Stop();
            }
        }
        HelpMarker(T("$MTFUI_Target_LivePreviewHelp"));
        if (app.livePreview) {
            SameLine();
            Checkbox(T("$MTFUI_Target_WithEffects"), &app.previewEffects);
            HelpMarker(T("$MTFUI_Target_WithEffectsHelp"));
        }
        EndDisabled();

        SameLine(0, 20);
        const bool canApply = mtf && app.HasPreset() && !app.ed.isNew;
        BeginDisabled(!canApply);
        if (Button(T("$MTFUI_Target_Apply"))) {
            // Applying after a failed save would put the old file on the target.
            if (!(app.Dirty() && !app.ed.builtin) || app.Save()) {
                app.ApplyToTarget(app.Preset().name);
            }
        }
        EndDisabled();
        if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            SetTooltip("%s", canApply ? T("$MTFUI_Target_ApplyTip") : T("$MTFUI_Target_ApplyDisabledTip"));
        }

        SameLine();
        BeginDisabled(!mtf);
        const bool mtfPreset = app.targetId == 0x14 && app.HasMtfPreset();
        const std::string star = mtfPreset ? "*" : "";
        const std::string appliedLabel =
            (app.appliedKnown ? TF("$MTFUI_Applied_Count", std::to_string(app.applied.size()) + star) : T("$MTFUI_Applied_Button") + star) + "###applied";
        if (Button(appliedLabel.c_str())) {
            app.RefreshApplied();
            OpenPopup("##applied");
        }
        EndDisabled();
        if (mtfPreset && IsItemHovered()) {
            SetTooltip("%s", T("$MTFUI_Applied_StarTip"));
        }
        if (BeginPopup("##applied")) {
            SeparatorText(TF("$MTFUI_Applied_Title", app.targetName).c_str());
            const bool onPlayer = app.targetId == 0x14;
            if (onPlayer) {
                app.WantSlotUsage();
            }
            if (onPlayer && app.HasMtfPreset()) {
                const std::string name = app.MtfPresetName();
                TextUnformatted(name.empty() ? T("$MTFUI_Applied_UnsavedMcm") : name.c_str());
                SameLine();
                TextDisabled("%s", T("$MTFUI_Applied_MtfPresetTag"));
                if (IsItemHovered()) {
                    SetTooltip("%s", T("$MTFUI_Applied_MtfPresetTip"));
                }
                SameLine(260);
                if (!name.empty() && SmallButton((std::string(T("$MTFUI_Common_Edit")) + "##default").c_str())) {
                    app.Open(game::MtfBridge::Get().Snapshot().mcmEditing);
                    app.RequestTab(Tab::Presets);
                    CloseCurrentPopup();
                }
                SameLine();
                if (SmallButton((std::string(T("$MTFUI_Common_Remove")) + "##default").c_str())) {
                    OpenPopup("##removedefault");
                }
                if (BeginPopup("##removedefault")) {
                    TextUnformatted(T("$MTFUI_Applied_ClearMtfPreset"));
                    TextDisabled("%s", name.empty() ? T("$MTFUI_Applied_ClearUnsavedMcm") : T("$MTFUI_Applied_ClearKeepsFile"));
                    if (Button(T("$MTFUI_Common_Remove"))) {
                        app.RemoveMtfPreset();
                        CloseCurrentPopup();
                    }
                    SameLine();
                    if (Button(T("$MTFUI_Common_Cancel"))) {
                        CloseCurrentPopup();
                    }
                    EndPopup();
                }
            }
            if (!app.appliedKnown) {
                TextDisabled("%s", T("$MTFUI_Applied_Asking"));
            } else if (app.applied.empty() && !(onPlayer && app.HasMtfPreset())) {
                TextDisabled("%s", T("$MTFUI_Common_None"));
            }
            for (const auto& name : app.applied) {
                PushID(name.c_str());
                const bool preview = model::IEquals(name, game::kPreviewPreset);
                const auto* info = app.FindLibrary(name);
                if (preview) {
                    TextColored(kAccent, "%s", T("$MTFUI_Applied_LivePreview"));
                } else {
                    TextUnformatted(info ? info->displayName.c_str() : name.c_str());
                    if (game::LivePreview::Get().IsSuspended(app.targetId, name)) {
                        SameLine();
                        TextDisabled("%s", T("$MTFUI_Applied_HiddenByPreview"));
                        if (IsItemHovered()) {
                            SetTooltip("%s", T("$MTFUI_Applied_HiddenByPreviewTip"));
                        }
                    }
                }
                SameLine(260);
                if (!preview && SmallButton(T("$MTFUI_Common_Edit"))) {
                    app.Open(name);
                    app.RequestTab(Tab::Presets);
                    CloseCurrentPopup();
                }
                SameLine();
                if (SmallButton(T("$MTFUI_Common_Remove"))) {
                    app.RemoveFromTarget(name);
                }
                PopID();
            }
            if (!app.applied.empty()) {
                Separator();
                if (Button(T("$MTFUI_Applied_RemoveAll"))) {
                    app.RemoveAllFromTarget();
                }
            }
            Separator();
            if (Button(T("$MTFUI_Common_ClearStuckEffects"))) {
                game::MtfBridge::Get().StopShadersOn(app.targetId);
                app.Toast(TF("$MTFUI_Toast_ShadersCleared", app.targetName));
            }
            HelpMarker(T("$MTFUI_Applied_ClearEffectsHelp"));
            EndPopup();
        }

        if (game::LivePreview::Get().Active()) {
            SameLine();
            if (Button(T("$MTFUI_Target_StopPreview"))) {
                app.livePreview = false;
                game::LivePreview::Get().Stop();
            }
        }
        Separator();
    }
}
