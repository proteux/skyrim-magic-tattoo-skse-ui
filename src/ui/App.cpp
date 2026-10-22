#include "ui/App.h"

#include "Version.h"
#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "game/Targets.h"
#include "io/JsonStore.h"
#include "render/DdsCache.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        constexpr std::size_t kUndoDepth = 100;

        fs::path GameDataDir() {
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            return fs::path(exe).parent_path() / L"Data";
        }

        int IniInt(const fs::path& ini, const wchar_t* section, const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.wstring().c_str()));
        }

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

        // Game thread only. The idle orbit camera otherwise starts circling the target behind the editor.
        void SetVanityCameraHeld(bool hold) {
            static std::optional<float> saved;
            constexpr auto kName = "fAutoVanityModeDelay:Camera";
            auto* setting = RE::INISettingCollection::GetSingleton()->GetSetting(kName);
            if (!setting) {
                setting = RE::INIPrefSettingCollection::GetSingleton()->GetSetting(kName);
            }
            if (!setting) {
                return;
            }
            if (hold && !saved) {
                saved = setting->GetFloat();
                setting->SetFloat(1.0e9f);
            } else if (!hold && saved) {
                setting->SetFloat(*saved);
                saved.reset();
            }
        }
    }

    void App::HoldVanityCamera(bool hold) {
        if (hold == vanityHeld_) {
            return;
        }
        vanityHeld_ = hold;
        game::MtfBridge::RunOnGame([hold] { SetVanityCameraHeld(hold); });
    }

    void App::HoldActor(RE::FormID id) {
        if (id == holdWanted_) {
            return;
        }
        holdWanted_ = id;
        game::MtfBridge::RunOnGame([this, id] {
            const RE::FormID held = game::HoldStill(id);
            game::MtfBridge::PostToUi([this, held] {
                if (settings.heldActor != held) {
                    settings.heldActor = held;
                    SaveSettings();
                }
            });
        });
    }

    App& App::Get() {
        static App instance;
        return instance;
    }

    void App::Init() {
        paths.data = GameDataDir();
        paths.mtfRoot = paths.data / "SKSE" / "Plugins" / "StorageUtilData" / "MagicTattoosFramework";
        paths.uiDir = paths.data / "SKSE" / "Plugins" / "MagicTattoosUI";
        settings.Load(paths.uiDir / "settings.json");
        std::erase_if(settings.panels, [](const auto& kv) {
            return kv.first != "main" && kv.first != "library" && kv.first != "tiers" && kv.first != "inspector";
        });
        livePreview = settings.livePreviewEnabled;

        const auto skee = paths.data / "SKSE" / "Plugins" / "skee64.ini";
        const auto custom = paths.data / "SKSE" / "Plugins" / "skee64_custom.ini";
        const std::pair<const char*, const wchar_t*> areas[] = { { "Body", L"Overlays/Body" }, { "Face", L"Overlays/Face" }, { "Hands", L"Overlays/Hands" },
            { "Feet", L"Overlays/Feet" } };
        for (const auto& [area, section] : areas) {
            int n = IniInt(skee, section, L"iNumOverlays", std::string_view(area) == "Body" ? 6 : 3);
            n = IniInt(custom, section, L"iNumOverlays", n);
            overlayCapacity_[area] = n;
        }

        game::LivePreview::Get().Init(paths.mtfRoot / "presets_builtin", [this](const std::vector<RE::FormID>& ids) {
                settings.previewActors = ids;
                SaveSettings();
            });
        ReloadAll();
        initialized_ = true;
        logger::info("UI ready: {} presets, {} packs, {} plugin catalogs", library.size(), cat.packs.size(), cat.plugins.size());
    }

    void App::ReloadAll() {
        cat.Load(paths.mtfRoot);
        ReloadLibrary();
        Relint();
    }

    void App::ReloadLibrary() {
        library = cat.ListPresets(true);
        std::vector<model::PresetFileInfo> mine;
        std::copy_if(library.begin(), library.end(), std::back_inserter(mine), [](const model::PresetFileInfo& i) { return !i.builtin; });
        usage = model::BuildUsage(cat, mine);
    }

    void App::Toast(std::string text, bool error) {
        if (error) {
            logger::warn("{}", text);
        }
        toasts_.push_back({ std::move(text), error, GetTime() + (error ? 8.0 : 4.0) });
        if (toasts_.size() > 3) {
            toasts_.erase(toasts_.begin());
        }
    }

    bool App::Dirty() const {
        return ed.preset && (ed.isNew || !(*ed.preset == ed.saved));
    }

    bool App::BuiltinNameTaken(const std::string& name) const {
        std::error_code ec;
        return fs::exists(cat.BuiltinPresetsDir() / (name + ".json"), ec);
    }

    std::string App::UniqueName(const std::string& displayName) const {
        std::string base = model::SanitizePresetName(displayName);
        if (base.empty()) {
            base = "Preset";
        }
        auto taken = [&](const std::string& n) {
            if (BuiltinNameTaken(n)) {
                return true;
            }
            return std::any_of(library.begin(), library.end(), [&](const model::PresetFileInfo& i) { return model::IEquals(i.name, n); });
        };
        std::string name = base;
        for (int i = 2; taken(name); ++i) {
            const std::string suffix = "_" + std::to_string(i);
            name = base.substr(0, 32 - suffix.size()) + suffix;
        }
        return name;
    }

    model::LintContext App::LintCtx() const {
        model::LintContext ctx;
        const auto texRoot = paths.data / "textures";
        ctx.textureExists = [texRoot](const std::string& rel) {
            std::error_code ec;
            if (fs::exists(texRoot / fs::path(rel), ec)) {
                return true;
            }
            // Could be packed in a BSA: only report it once the loader has tried and failed.
            return !render::DdsCache::Get().Failed(rel, 256);
        };
        const auto& snap = game::MtfBridge::Get().Snapshot();
        if (snap.fresh && snap.loaded) {
            ctx.pluginEnabled = [snap](const std::string& pid) {
                auto it = snap.pluginEnabled.find(io::ToLower(pid));
                return it == snap.pluginEnabled.end() || it->second;
            };
            for (int a = 0; a < 4; ++a) {
                ctx.overlayStart[game::kAreas[a]] = snap.overlayStart[a];
            }
        }
        ctx.overlayCapacity = overlayCapacity_;
        return ctx;
    }

    void App::Relint() {
        issues.clear();
        if (ed.preset) {
            issues = model::LintPreset(cat, *ed.preset, LintCtx());
        }
        lastLint_ = GetTime();
    }

    void App::Open(const std::string& name) {
        if (ed.preset && model::IEquals(ed.preset->name, name) && !ed.isNew) {
            return;
        }
        if (Dirty()) {
            pendingOpen_ = name;
            confirmOpen_ = true;
            return;
        }
        LoadPresetNow(name);
    }

    void App::LoadPresetNow(const std::string& name) {
        const auto it = std::find_if(library.begin(), library.end(), [&](const model::PresetFileInfo& i) { return model::IEquals(i.name, name); });
        if (it == library.end()) {
            Toast("Preset '" + name + "' no longer exists", true);
            return;
        }
        std::string error;
        auto json = io::ReadJson(it->path, &error);
        if (!json) {
            Toast("Could not read " + it->path.filename().string() + ": " + error, true);
            return;
        }
        ed = {};
        ed.preset = model::PresetFromJson(it->name, *json);
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.builtin = it->builtin;
        settings.lastPreset = it->name;
        SaveSettings();
        Relint();
    }

    void App::CreateNew(const std::string& displayName, const std::string& packId, const std::string& entryId) {
        if (Dirty()) {
            Toast("Save or revert the open preset first", true);
            return;
        }
        ed = {};
        ed.preset = model::NewPreset(UniqueName(displayName), displayName);
        if (!packId.empty()) {
            ed.preset->slots[0].packId = packId;
            ed.preset->slots[0].entryId = entryId;
            settings.PushRecent(packId + "/" + entryId);
            SaveSettings();
        }
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.isNew = true;
        Relint();
    }

    void App::Duplicate(const std::string& displayName) {
        if (!ed.preset) {
            return;
        }
        auto copy = *ed.preset;
        copy.displayName = displayName;
        copy.name = UniqueName(displayName);
        copy.hidden = 0;
        ed = {};
        ed.preset = std::move(copy);
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.isNew = true;
        Relint();
    }

    void App::Delete(const std::string& name) {
        const auto path = cat.PresetPath(name);
        auto json = io::ReadJson(path);
        if (!json) {
            Toast("Only your own presets can be deleted", true);
            return;
        }
        io::Backup(path, paths.uiDir / "backups");
        // Same soft delete as MTF: a hidden=1 tombstone that MTF skips and a later save can reclaim.
        (*json)["hidden"] = 1;
        std::string error;
        if (!io::WriteJsonStyled(path, *json, &error)) {
            Toast("Delete failed: " + error, true);
            return;
        }
        if (ed.preset && model::IEquals(ed.preset->name, name)) {
            ed = {};
        }
        game::MtfBridge::Get().PresetChanged(name, false);
        ReloadLibrary();
        Toast("Deleted " + name + " (a backup was kept)");
    }

    bool App::Save() {
        if (!ed.preset) {
            return false;
        }
        auto& p = *ed.preset;
        if (ed.builtin) {
            Toast("Built-in presets are read-only. Use Duplicate to make your own copy.", true);
            return false;
        }
        if (p.name.empty()) {
            p.name = UniqueName(p.displayName);
        }
        if (BuiltinNameTaken(p.name)) {
            Toast("'" + p.name + "' is reserved by an MTF built-in preset. Duplicate it under another name.", true);
            return false;
        }
        const auto path = cat.PresetPath(p.name);
        io::Backup(path, paths.uiDir / "backups");
        const auto json = model::PresetToJson(p);
        std::string error;
        if (!io::WriteJsonStyled(path, json, &error)) {
            Toast("Save failed: " + error, true);
            return false;
        }
        p.raw = json;
        p.schemaVersion = model::kSchemaVersion;
        p.hidden = 0;
        ed.saved = p;
        ed.committed = p;
        ed.isNew = false;
        settings.lastPreset = p.name;
        SaveSettings();
        ReloadLibrary();
        const std::string name = p.name;
        const std::string display = p.displayName;
        const bool wearing = std::any_of(applied.begin(), applied.end(), [&](const std::string& a) { return model::IEquals(a, name); });
        game::MtfBridge::Get().PresetChanged(name, false, [this, name, display, wearing] {
            if (wearing) {
                // MTF redraws on tier changes only; re-applying shows the edit right away.
                const auto target = targetId;
                game::MtfBridge::Get().Remove(target, name, [this, target, name, display] {
                    game::MtfBridge::Get().Apply(target, name, [this, display](int rc) {
                        Toast(rc == 1 || rc == 0 ? "Saved " + display + " and refreshed it on " + targetName : "Saved, but re-applying failed: " + game::ApplyResultText(rc), rc < 0);
                        RefreshApplied();
                    });
                });
            } else {
                Toast("Saved " + display);
            }
        });
        logger::info("Saved preset {}", name);
        return true;
    }

    void App::Revert() {
        if (!ed.preset) {
            return;
        }
        if (ed.isNew) {
            ed = {};
            return;
        }
        ed.undo.push_back(*ed.preset);
        *ed.preset = ed.saved;
        ed.committed = ed.saved;
        Relint();
    }

    void App::Undo() {
        if (!ed.preset || ed.undo.empty()) {
            return;
        }
        ed.redo.push_back(*ed.preset);
        *ed.preset = ed.undo.back();
        ed.undo.pop_back();
        ed.committed = *ed.preset;
        Relint();
    }

    void App::Redo() {
        if (!ed.preset || ed.redo.empty()) {
            return;
        }
        ed.undo.push_back(*ed.preset);
        *ed.preset = ed.redo.back();
        ed.redo.pop_back();
        ed.committed = *ed.preset;
        Relint();
    }

    void App::UseEntry(int slot, const std::string& packId, const std::string& entryId) {
        if (!ed.preset || slot < 0 || slot >= static_cast<int>(ed.preset->slots.size())) {
            return;
        }
        auto& s = ed.preset->slots[slot];
        s.packId = packId;
        s.entryId = entryId;
        settings.PushRecent(packId + "/" + entryId);
        SaveSettings();
    }

    void App::SetTarget(RE::FormID id, std::string name) {
        if (id == targetId) {
            return;
        }
        if (game::LivePreview::Get().Active()) {
            game::LivePreview::Get().Stop();
        }
        targetId = id;
        targetName = std::move(name);
        applied.clear();
        appliedKnown = false;
        RefreshApplied();
    }

    void App::RefreshApplied() {
        game::MtfBridge::Get().ListApplied(targetId, [this, id = targetId](std::vector<std::string> names) {
            if (id != targetId) {
                return;
            }
            std::erase_if(names, [](const std::string& n) { return model::IEquals(n, game::kPreviewPreset); });
            applied = std::move(names);
            appliedKnown = true;
        });
    }

    void App::ApplyToTarget(const std::string& name) {
        game::MtfBridge::Get().Apply(targetId, name, [this, name](int rc) {
            Toast(name + ": " + game::ApplyResultText(rc) + (rc == 1 ? " to " + targetName : ""), rc < 0);
            RefreshApplied();
        });
    }

    void App::RemoveFromTarget(const std::string& name) {
        game::MtfBridge::Get().Remove(targetId, name, [this, name] {
            Toast("Removed " + name + " from " + targetName);
            RefreshApplied();
        });
    }

    void App::RemoveAllFromTarget() {
        for (const auto& n : applied) {
            game::MtfBridge::Get().Remove(targetId, n, [this] { RefreshApplied(); });
        }
        Toast("Removing " + std::to_string(applied.size()) + " preset(s) from " + targetName);
    }

    void App::UpdatePreview() {
        auto& lp = game::LivePreview::Get();
        if (!livePreview || !ed.preset || !game::MtfBridge::Get().Installed()) {
            if (lp.Active()) {
                lp.Stop();
            }
            return;
        }
        lp.Request(targetId, *ed.preset, ed.slot, previewEffects);
    }

    void App::SaveSettings() {
        settings.livePreviewEnabled = livePreview;
        settings.Save(paths.uiDir / "settings.json");
    }

    void App::OnMenuOpen() {
        if (!initialized_) {
            return;
        }
        ReloadAll();
        game::MtfBridge::Get().Refresh();
        std::vector<std::string> pids;
        for (const auto& p : cat.plugins) {
            pids.push_back(p.pluginId);
        }
        game::MtfBridge::Get().RefreshPlugins(pids);
        game::MtfBridge::RunOnGame([this, id = targetId] {
            const bool ok = RE::TESForm::LookupByID<RE::Actor>(id) != nullptr;
            game::MtfBridge::PostToUi([this, ok] {
                if (!ok) {
                    SetTarget(0x14, "You");
                }
                RefreshApplied();
            });
        });
        if (!ed.preset && !settings.lastPreset.empty()) {
            const bool exists = std::any_of(library.begin(), library.end(), [&](const model::PresetFileInfo& i) { return model::IEquals(i.name, settings.lastPreset); });
            if (exists) {
                LoadPresetNow(settings.lastPreset);
            }
        }
    }

    void App::OnMenuClose() {
        HoldVanityCamera(false);
        HoldActor(0);
        game::LivePreview::Get().Stop();
        SaveSettings();
    }

    void App::OnGameLoaded() {
        targetId = 0x14;
        targetName = "You";
        applied.clear();
        appliedKnown = false;
        game::LivePreview::Get().Cleanup(settings.previewActors);
        holdWanted_ = 0;
        game::MtfBridge::RunOnGame([id = settings.heldActor] { game::ReleaseLeftover(id); });
        if (settings.heldActor != 0) {
            settings.heldActor = 0;
            SaveSettings();
        }
        game::MtfBridge::Get().Refresh();
    }

    void App::HandleShortcuts() {
        auto* io = GetIO();
        if (!io->KeyCtrl || io->WantTextInput) {
            return;
        }
        if (IsKeyPressed(ImGuiKey_S, false) && Dirty()) {
            Save();
        } else if (IsKeyPressed(ImGuiKey_Z, false)) {
            if (io->KeyShift) {
                Redo();
            } else {
                Undo();
            }
        } else if (IsKeyPressed(ImGuiKey_Y, false)) {
            Redo();
        }
    }

    void App::CommitFrame() {
        if (ed.preset && !(*ed.preset == ed.committed) && !IsAnyItemActive()) {
            ed.undo.push_back(ed.committed);
            if (ed.undo.size() > kUndoDepth) {
                ed.undo.erase(ed.undo.begin());
            }
            ed.redo.clear();
            ed.committed = *ed.preset;
            Relint();
        } else if (GetTime() - lastLint_ > 2.0) {
            Relint();
        }
        UpdatePreview();
        game::LivePreview::Get().Tick();
        render::DdsCache::Get().EndFrame();
    }

    void App::DrawModals() {
        if (confirmOpen_) {
            OpenPopup("Unsaved changes");
            confirmOpen_ = false;
        }
        SetNextWindowSize(ImVec2{ 420, 0 }, ImGuiCond_Appearing);
        if (BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            TextWrapped("'%s' has unsaved changes.", ed.preset ? ed.preset->displayName.c_str() : "");
            Spacing();
            if (Button("Save", ImVec2{ 110, 0 })) {
                if (Save()) {
                    LoadPresetNow(pendingOpen_);
                }
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Discard", ImVec2{ 110, 0 })) {
                ed = {};
                LoadPresetNow(pendingOpen_);
                CloseCurrentPopup();
            }
            SameLine();
            if (Button("Cancel", ImVec2{ 110, 0 }) || IsKeyPressed(ImGuiKey_Escape, false)) {
                CloseCurrentPopup();
            }
            EndPopup();
        }
    }

    void App::DrawStatus() {
        const double now = GetTime();
        std::erase_if(toasts_, [now](const ToastMsg& t) { return t.until < now; });
        if (!toasts_.empty()) {
            const auto& t = toasts_.back();
            TextColored(t.error ? kError : kGood, "%s", t.text.c_str());
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
                TextDisabled("Magic Tattoos UI %s  |  Ctrl+S save, Ctrl+Z undo, Ctrl+Y redo", kVersion);
            }
        }
    }

    void App::BeginFrame() {
        HoldVanityCamera(true);
        HoldActor(settings.holdTarget ? targetId : 0);
        game::MtfBridge::DrainUi();
        HandleShortcuts();
    }

    void App::Draw(bool presetsTab, bool targetBar) {
        if (targetBar) {
            DrawTargetBar(*this);
        }

        if (BeginTabBar("##mtfui_tabs")) {
            auto tab = [&](const char* label, Tab t, void (*fn)(App&)) {
                const int flags = requestedTab_ == t ? ImGuiTabItemFlags_SetSelected : 0;
                if (BeginTabItem(label, nullptr, flags)) {
                    if (requestedTab_ == t) {
                        requestedTab_.reset();
                    }
                    const float footer = GetFrameHeight() + 8.0f;
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
        Separator();
        DrawStatus();
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
            OnMenuClose();
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
            if (Button("Open in its own window")) {
                window_->IsOpen = true;
            }
            HelpMarker("A larger, movable window with the same editor. Esc or the X closes it.");
            Draw(true, true);
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
            OnMenuClose();
        }
    }
}
