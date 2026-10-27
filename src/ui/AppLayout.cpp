// Where the editor is drawn: the menu section, the standalone window or the split panels, plus the shared
// status line and modals.

#include "ui/App.h"

#include "Version.h"
#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "render/DdsCache.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        // First-use spots for the split layout: editing panels hug the screen edges so the middle, where the target
        // usually stands, stays clear.
        io::PanelState DefaultPanel(std::string_view key, ImVec2 view) {
            const float m = 16.0f;
            const float top = 150.0f;
            const float leftW = std::clamp(view.x * 0.24f, 360.0f, 520.0f);
            const float rightW = std::clamp(view.x * 0.26f, 420.0f, 560.0f);
            const float tall = view.y - top - m;
            if (key == "main") {
                return { std::max(m, view.x / 2 - 470.0f), m, 0.0f, 0.0f, true };
            }
            if (key == "library") {
                return { m, top, leftW, tall * 0.36f, true };
            }
            if (key == "tiers") {
                return { m, top + tall * 0.36f + 8.0f, leftW, tall * 0.64f - 8.0f, true };
            }
            return { view.x - rightW - m, top, rightW, tall, true };
        }
    }

    void App::DrawModals() {
        // Both callbacks call this; a second copy of a modal would open in the other window.
        if (GetFrameCount() == modalsFrame_) {
            return;
        }
        modalsFrame_ = GetFrameCount();
        if (draftOffer_ && !IsPopupOpen("Restore unsaved edits?")) {
            OpenPopup("Restore unsaved edits?");
        }
        if (BeginPopupModal("Restore unsaved edits?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            TextWrapped("The game closed while '%s' had unsaved edits.", draftName_.c_str());
            Spacing();
            if (Button("Restore", ImVec2{ 110, 0 })) {
                draftOffer_ = false;
                if (ed.preset && Dirty()) {
                    Toast("Save or discard the open preset first", true);
                } else {
                    RestoreDraft();
                }
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Throw away", ImVec2{ 110, 0 })) {
                draftOffer_ = false;
                DiscardDraft();
                CloseCurrentPopup();
            }
            EndPopup();
        }

        if (confirmOpen_) {
            OpenPopup("Unsaved changes");
            confirmOpen_ = false;
        }
        SetNextWindowSize(ImVec2{ 420, 0 }, ImGuiCond_Appearing);
        if (BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ed.isNew) {
                TextWrapped("'%s' has never been saved.", ed.preset ? ed.preset->displayName.c_str() : "");
            } else if (ed.builtin) {
                TextWrapped("'%s' is a built-in preset and can't be changed, but you edited it. Save your edits as your own copy?",
                    ed.preset ? ed.preset->displayName.c_str() : "");
            } else {
                TextWrapped("'%s' has unsaved changes.", ed.preset ? ed.preset->displayName.c_str() : "");
            }
            Spacing();
            auto run = [this] {
                auto action = std::move(pendingAction_);
                pendingAction_ = nullptr;
                CloseCurrentPopup();
                if (action) {
                    action();
                }
            };
            if (ed.builtin) {
                if (Button("Save as a copy", ImVec2{ 130, 0 }) && ed.preset) {
                    Duplicate(ed.preset->displayName + " copy");
                    if (Save()) {
                        run();
                    }
                }
            } else if (Button("Save", ImVec2{ 110, 0 })) {
                // On failure the toast says why and the choice stays open.
                if (Save()) {
                    run();
                }
            }
            SameLine();
            if (Button("Discard", ImVec2{ 110, 0 })) {
                ClearEditor();
                run();
            }
            SameLine();
            if (Button("Cancel", ImVec2{ 110, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                pendingAction_ = nullptr;
                CloseCurrentPopup();
            }
            DrawStatus();
            EndPopup();
        }
    }

    void App::DrawStatus() {
        const double now = GetTime();
        std::erase_if(toasts_, [now](const ToastMsg& t) { return t.until < now; });
        if (!toasts_.empty()) {
            const auto& t = toasts_.back();
            TextColored(t.error ? kError : kGood, "%s", t.text.c_str());
            if (t.action) {
                SameLine();
                if (SmallButton(t.actionLabel.c_str())) {
                    auto action = t.action;
                    toasts_.pop_back();
                    action();
                }
            }
            return;
        }
        const auto& bridge = game::MtfBridge::Get();
        if (!bridge.Installed()) {
            TextColored(kError, "Magic Tattoos Framework is not loaded. You can still edit preset files.");
        } else if (bridge.Snapshot().fresh && !bridge.Snapshot().modActive) {
            TextColored(kWarn, "MTF is disabled in its settings. Tattoos will not update.");
        } else {
            const auto& lp = game::LivePreview::Get();
            const int pending = render::DdsCache::Get().Pending();
            if (!lp.Status().empty()) {
                TextColored(kAccent, "%s on %s", lp.Status().c_str(), targetName.c_str());
            } else if (pending > 0) {
                TextDisabled("Loading %d texture(s)...", pending);
            } else {
                TextDisabled("Magic Tattoos UI %s", kVersion);
                if (IsItemHovered()) {
                    SetTooltip("Shortcuts\n"
                               "Ctrl+S  save\n"
                               "Ctrl+Z / Ctrl+Y  undo / redo\n"
                               "Ctrl+N  new preset\n"
                               "Ctrl+D  duplicate\n"
                               "Ctrl+F  search presets\n"
                               "F5  apply to target\n"
                               "Alt+Up / Alt+Down  move the selected tier");
                }
                SameLine();
                TextDisabled("|  Ctrl+S save, Ctrl+Z undo, F5 apply (hover for more)");
            }
        }
    }

    void App::Draw(bool presetsTab, bool targetBar) {
        if (targetBar) {
            DrawTargetBar(*this);
        }

        // The Tattoos tab draws its own footer where the status line would go.
        bool ownFooter = false;
        if (BeginTabBar("##mtfui_tabs")) {
            auto tab = [&](const char* label, Tab t, void (*fn)(App&)) {
                const int flags = requestedTab_ == t ? ImGuiTabItemFlags_SetSelected : 0;
                if (BeginTabItem(label, nullptr, flags)) {
                    if (requestedTab_ == t) {
                        requestedTab_.reset();
                    }
                    ownFooter = t == Tab::Tattoos;
                    const float footer = targetBar && !ownFooter ? GetFrameHeight() + 8.0f : 0.0f;
                    if (BeginChild("##tab", ImVec2{ 0, -footer })) {
                        fn(*this);
                    }
                    EndChild();
                    EndTabItem();
                }
            };
            if (presetsTab) {
                tab("Presets", Tab::Presets, DrawPresetsTab);
            }
            tab(picking ? "Tattoos (choosing)###tattoos" : "Tattoos###tattoos", Tab::Tattoos, DrawTattoosTab);
            tab("General", Tab::General, DrawGeneralTab);
            tab("Plugins", Tab::Plugins, DrawPluginsTab);
            EndTabBar();
        }
        // While the editor windows are up, the strip at the top shows the status.
        if (targetBar && !ownFooter) {
            Separator();
            DrawStatus();
        }
        DrawModals();
    }

    io::PanelState& App::Panel(const std::string& key) {
        auto [it, added] = settings.panels.try_emplace(key);
        if (added || (it->second.w <= 0.0f && key != "main")) {
            const bool open = it->second.open;
            it->second = DefaultPanel(key, GetIO()->DisplaySize);
            if (!added) {
                it->second.open = open;
            }
        }
        return it->second;
    }

    void App::ShowEditorWindows(bool show) {
        if (!window_ || window_->IsOpen == show) {
            return;
        }
        window_->IsOpen = show;
        if (show) {
            for (const char* key : { "library", "tiers", "inspector" }) {
                Panel(key).open = true;
            }
        } else {
            OnEditorClose();
        }
    }

    void App::HandlePanelRequests() {
        // The Presets tab lives in the editor windows; the other tabs stay in the menu section, which consumes them.
        if (requestedTab_ == Tab::Presets) {
            Panel("tiers").open = true;
            Panel("inspector").open = true;
            requestedTab_.reset();
        }
        if (requestedTab_ == Tab::Tattoos && !pickHintShown_ && GetFrameCount() - lastSectionFrame_ > 2) {
            Toast("Choose the tattoo on the Tattoos tab of Magic Tattoos > Editor in the menu");
            pickHintShown_ = true;
        }
        if (!requestedTab_) {
            pickHintShown_ = false;
        }
    }

    void App::DrawPanel(const char* key, const char* title, const std::function<void()>& body, ImGuiWindowFlags flags) {
        auto& st = Panel(key);
        if (!st.open) {
            return;
        }
        const ImGuiCond cond = placeAlways_ ? ImGuiCond_Always : ImGuiCond_Appearing;
        SetNextWindowPos(ImVec2{ st.x, st.y }, cond);
        if (!(flags & ImGuiWindowFlags_AlwaysAutoResize)) {
            SetNextWindowSize(ImVec2{ st.w, st.h }, cond);
        }
        SetNextWindowSizeConstraints(ImVec2{ 260, 160 }, ImVec2{ 8192, 8192 });
        SetNextWindowBgAlpha(settings.panelOpacity / 100.0f);
        bool open = true;
        if (Begin(title, &open, flags | ImGuiWindowFlags_NoCollapse)) {
            body();
        }
        const ImVec2 pos = GetWindowPos();
        const ImVec2 size = GetWindowSize();
        End();
        st.x = pos.x;
        st.y = pos.y;
        if (!(flags & ImGuiWindowFlags_AlwaysAutoResize)) {
            st.w = size.x;
            st.h = size.y;
        }
        if (!open) {
            st.open = false;
        }
    }

    bool App::DrawPanels() {
        if (resetLayout_) {
            settings.panels.clear();
            resetLayout_ = false;
            placeAlways_ = true;
        }
        HandlePanelRequests();

        // The main strip always shows, peeking or not, so the peek button stays under the cursor.
        bool open = true;
        peeking_ = false;
        {
            auto& st = Panel("main");
            SetNextWindowPos(ImVec2{ st.x, st.y }, placeAlways_ ? ImGuiCond_Always : ImGuiCond_Appearing);
            SetNextWindowBgAlpha(settings.panelOpacity / 100.0f);
            if (Begin("Magic Tattoos###mtfui_main", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                DrawTargetBar(*this);
                Button("Hold to peek");
                peeking_ = IsItemActive();
                if (IsItemHovered()) {
                    SetTooltip("Hides the other panels while you hold the mouse button down. Drag panels by their title bar to move them; "
                               "the layout is remembered.");
                }
                SameLine(0, 16);
                DrawStatus();
                DrawModals();
            }
            const ImVec2 pos = GetWindowPos();
            End();
            st.x = pos.x;
            st.y = pos.y;
        }

        if (!peeking_) {
            DrawPanel("library", "Presets###mtfui_library", [this] { DrawLibraryPanel(*this); });
            DrawPanel("tiers", "Tiers###mtfui_tiers", [this] { DrawTiersPanel(*this); });
            DrawPanel("inspector", "Tier editor###mtfui_inspector", [this] { DrawInspectorPanel(*this); });
        }
        placeAlways_ = false;
        return open;
    }

    void App::DrawSection() {
        if (!initialized_) {
            TextUnformatted("Magic Tattoos UI is starting...");
            return;
        }
        lastSectionFrame_ = GetFrameCount();
        BeginFrame();
        if (!window_) {
            Draw(true, true);
        } else if (settings.splitPanels) {
            const bool shown = window_->IsOpen;
            if (requestedTab_ == Tab::Presets) {
                requestedTab_.reset();
                ShowEditorWindows(true);
            }
            if (Button(shown ? "Close editor windows" : "Open editor windows")) {
                ShowEditorWindows(!shown);
            }
            HelpMarker("Presets, Tiers and Tier editor open as separate panels over the game, with the target and live preview "
                       "on a strip at the top. Place them around the target; the layout is remembered. Esc or the X on the strip closes them.");
            // The strip has the target bar while the editor windows are up.
            Draw(false, !window_->IsOpen);
        } else {
            if (window_->IsOpen) {
                // Drawing the editor here as well would run every edit twice.
                if (Button("Close its own window")) {
                    window_->IsOpen = false;
                    OnEditorClose();
                }
                SameLine();
                TextDisabled("The editor is open in its own window.");
            } else {
                if (Button("Open in its own window")) {
                    window_->IsOpen = true;
                }
                HelpMarker("A larger, movable window with the same editor. Esc or the X closes it.");
                Draw(true, true);
            }
        }
        CommitFrame();
    }

    void App::DrawWindow() {
        if (!initialized_ || !window_) {
            return;
        }
        static bool loggedIo = false;
        if (!loggedIo) {
            const auto* io = GetIO();
            logger::info("ImGui ini file: {}, docking: {}", io->IniFilename ? io->IniFilename : "(none)",
                (io->ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0);
            loggedIo = true;
        }
        BeginFrame();
        bool open = true;
        if (settings.splitPanels) {
            open = DrawPanels();
        } else {
            SetNextWindowSize(ImVec2{ 1180, 760 }, ImGuiCond_FirstUseEver);
            SetNextWindowSizeConstraints(ImVec2{ 820, 520 }, ImVec2{ 4096, 4096 });
            SetNextWindowBgAlpha(settings.panelOpacity / 100.0f);
            if (Begin("Magic Tattoos##mtfui", &open, ImGuiWindowFlags_NoCollapse)) {
                Draw(true, true);
            }
            End();
        }
        CommitFrame();
        if (!open || (IsKeyPressed(ImGuiKey_Escape, false) && !IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !GetIO()->WantTextInput)) {
            window_->IsOpen = false;
            OnEditorClose();
        }
    }
}
