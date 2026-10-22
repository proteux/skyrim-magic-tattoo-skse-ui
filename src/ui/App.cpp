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

    void App::CreateNew(const std::string& displayName) {
        if (Dirty()) {
            Toast("Save or revert the open preset first", true);
            return;
        }
        ed = {};
        ed.preset = model::NewPreset(UniqueName(displayName), displayName);
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
        game::LivePreview::Get().Stop();
        SaveSettings();
    }

    void App::OnGameLoaded() {
        targetId = 0x14;
        targetName = "You";
        applied.clear();
        appliedKnown = false;
        game::LivePreview::Get().Cleanup(settings.previewActors);
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

    void App::DrawToasts() {
        const double now = GetTime();
        std::erase_if(toasts_, [now](const ToastMsg& t) { return t.until < now; });
        Separator();
        if (!toasts_.empty()) {
            const auto& t = toasts_.back();
            TextColored(t.error ? kError : kGood, "%s", t.text.c_str());
            return;
        }
        const auto& bridge = game::MtfBridge::Get();
        if (!bridge.Installed()) {
            TextColored(kError, "Magic Tattoos Framework is not loaded. You can still edit preset files.");
        } else if (bridge.Snapshot().fresh && !bridge.Snapshot().modActive) {
            TextColored(kWarn, "MTF is disabled (General tab). Tattoos will not update.");
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

    void App::Draw() {
        game::MtfBridge::DrainUi();
        HandleShortcuts();
        DrawTargetBar(*this);

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
            tab("Presets", Tab::Presets, DrawPresetsTab);
            tab(picking ? "Tattoos (choosing)###tattoos" : "Tattoos###tattoos", Tab::Tattoos, DrawTattoosTab);
            tab("General", Tab::General, DrawGeneralTab);
            tab("Plugins", Tab::Plugins, DrawPluginsTab);
            EndTabBar();
        }
        DrawToasts();
        DrawModals();
        CommitFrame();
    }

    void App::DrawSection() {
        if (!initialized_) {
            TextUnformatted("Magic Tattoos UI is starting...");
            return;
        }
        if (window_) {
            if (Button("Open in its own window")) {
                window_->IsOpen = true;
            }
            HelpMarker("A larger, movable window with the same editor. Esc or the X closes it.");
        }
        Draw();
    }

    void App::DrawWindow() {
        if (!initialized_ || !window_) {
            return;
        }
        SetNextWindowSize(ImVec2{ 1180, 760 }, ImGuiCond_FirstUseEver);
        SetNextWindowSizeConstraints(ImVec2{ 820, 520 }, ImVec2{ 4096, 4096 });
        bool open = true;
        if (Begin("Magic Tattoos##mtfui", &open, ImGuiWindowFlags_NoCollapse)) {
            Draw();
        }
        End();
        if (!open || (IsKeyPressed(ImGuiKey_Escape, false) && !IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !GetIO()->WantTextInput)) {
            window_->IsOpen = false;
            OnMenuClose();
        }
    }
}
