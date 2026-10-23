#include "ui/App.h"

#include "game/Camera.h"
#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "game/Targets.h"
#include "io/JsonStore.h"
#include "model/Text.h"
#include "render/DdsCache.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        constexpr std::size_t kUndoDepth = 100;

        int IniInt(const fs::path& ini, const wchar_t* section, const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.wstring().c_str()));
        }
    }

    fs::path GameDataDir() {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        return fs::path(exe).parent_path() / L"Data";
    }

    void App::HoldVanityCamera(bool hold) {
        if (hold == vanityHeld_) {
            return;
        }
        vanityHeld_ = hold;
        game::MtfBridge::RunOnGame([hold] { game::SetVanityCameraHeld(hold); });
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
        logger::info("Overlay slots: Body {}, Face {}, Hands {}, Feet {}", overlayCapacity_["Body"], overlayCapacity_["Face"], overlayCapacity_["Hands"],
            overlayCapacity_["Feet"]);

        game::LivePreview::Get().Init(paths.mtfRoot / "presets_builtin", settings.previewActors, [this](const std::vector<RE::FormID>& ids) {
                settings.previewActors = ids;
                SaveSettings();
            });
        ReloadAll();
        if (auto draft = io::ReadJson(DraftPath()); draft && draft->is_object()) {
            draftOffer_ = true;
            draftName_ = draft->value("name", std::string{});
            if (auto p = draft->find("preset"); p != draft->end() && p->is_object()) {
                draftName_ = p->value("displayname", draftName_);
            }
            logger::info("Found unsaved edits to {} from an earlier session", draftName_);
        }
        initialized_ = true;
        logger::info("UI ready: {} presets ({} deleted), {} packs, {} plugin catalogs", library.size(), hiddenPresets.size(), cat.packs.size(),
            cat.plugins.size());
    }

    void App::ReloadAll() {
        cat.Load(paths.mtfRoot);
        // Every menu open reloads; only log a broken file again once something changed.
        static std::vector<std::string> logged;
        if (cat.loadErrors != logged) {
            for (const auto& e : cat.loadErrors) {
                logger::warn("Skipped catalog file: {}", e);
            }
            logged = cat.loadErrors;
        }
        ReloadLibrary();
        Relint();
    }

    void App::ReloadLibrary() {
        library = cat.ListPresets(true, &hiddenPresets);
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

    const model::PresetFileInfo* App::FindLibrary(const std::string& name) const {
        const auto it = std::find_if(library.begin(), library.end(), [&](const model::PresetFileInfo& i) { return model::IEquals(i.name, name); });
        return it == library.end() ? nullptr : &*it;
    }

    bool App::IsApplied(const std::string& name) const {
        return std::any_of(applied.begin(), applied.end(), [&](const std::string& a) { return model::IEquals(a, name); });
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
        return model::NextFreeName(base, [&](const std::string& n) { return BuiltinNameTaken(n) || FindLibrary(n) != nullptr; });
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
        // Building the preview document every frame is wasteful; only a change to what it is built from matters.
        const int slot = std::clamp(ed.slot, 0, static_cast<int>(ed.preset->slots.size()) - 1);
        PreviewInputs in{ targetId, slot, previewEffects, ed.preset->slots[slot], ed.preset->slots[0] };
        if (lp.Active() && previewInputs_ == in) {
            return;
        }
        previewInputs_ = std::move(in);
        lp.Request(targetId, *ed.preset, slot, previewEffects);
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
        if (!ed.preset && !draftOffer_ && !settings.lastPreset.empty() && FindLibrary(settings.lastPreset)) {
            LoadPresetNow(settings.lastPreset);
        }
    }

    void App::OnMenuClose() {
        HoldVanityCamera(false);
        HoldActor(0);
        game::LivePreview::Get().Stop();
        SaveSettings();
    }

    // Runs from the SKSE message handler. That is the main thread, where the menu framework also renders, so touching
    // UI state here is safe; what must not run is a callback from before the load, which NewSession drops.
    void App::OnGameLoaded() {
        game::MtfBridge::NewSession();
        HoldVanityCamera(false);
        targetId = 0x14;
        targetName = "You";
        applied.clear();
        appliedKnown = false;
        game::LivePreview::Get().Cleanup();
        holdWanted_ = 0;
        game::MtfBridge::RunOnGame([id = settings.heldActor] { game::ReleaseLeftover(id); });
        if (settings.heldActor != 0) {
            settings.heldActor = 0;
            SaveSettings();
        }
        game::MtfBridge::Get().Refresh();
    }

    void App::OnSaving() {
        // The game thread is the one saving, so this releases the NPC before the save is written. The next
        // frame of the editor holds them again.
        if (game::ReleaseForSave() != 0) {
            holdWanted_ = 0;
        }
    }

    void App::HandleShortcuts() {
        auto* io = GetIO();
        if (io->WantTextInput || IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            return;
        }
        if (!io->KeyCtrl) {
            HandlePlainShortcuts();
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
        } else if (IsKeyPressed(ImGuiKey_N, false)) {
            CreateNew("New preset");
            RequestTab(Tab::Presets);
        } else if (IsKeyPressed(ImGuiKey_D, false) && ed.preset) {
            Duplicate(ed.preset->displayName + " copy");
        } else if (IsKeyPressed(ImGuiKey_F, false)) {
            focusSearch = true;
        }
    }

    void App::HandlePlainShortcuts() {
        if (!ed.preset) {
            return;
        }
        auto* io = GetIO();
        if (IsKeyPressed(ImGuiKey_F5, false) && !io->KeyAlt && !io->KeyShift) {
            if (ed.isNew) {
                Toast("Save the preset before applying it", true);
            } else if (!(Dirty() && !ed.builtin) || Save()) {
                ApplyToTarget(ed.preset->name);
            }
        } else if (io->KeyAlt && IsKeyPressed(ImGuiKey_UpArrow, false) && ed.slot > 1) {
            MoveTier(ed.slot, ed.slot - 1);
        } else if (io->KeyAlt && IsKeyPressed(ImGuiKey_DownArrow, false) && ed.slot >= 1) {
            MoveTier(ed.slot, ed.slot + 1);
        }
    }

    void App::CommitFrame() {
        if (GetFrameCount() == committedFrame_) {
            return;
        }
        committedFrame_ = GetFrameCount();
        if (ed.preset && !(*ed.preset == ed.committed) && !IsAnyItemActive()) {
            ed.undo.push_back(ed.committed);
            if (ed.undo.size() > kUndoDepth) {
                ed.undo.erase(ed.undo.begin());
            }
            ed.redo.clear();
            ed.committed = *ed.preset;
            draftPending_ = true;
            Relint();
        } else if (GetTime() - lastLint_ > 0.5) {
            // Lint also depends on MTF's settings and on which textures failed to load; recheck when those move.
            const auto& snap = game::MtfBridge::Get().Snapshot();
            LintInputs in{ render::DdsCache::Get().Generation(), snap.fresh && snap.loaded, snap.pluginEnabled, snap.overlayStart };
            lastLint_ = GetTime();
            if (!(in == lintInputs_)) {
                lintInputs_ = std::move(in);
                Relint();
            }
        }
        if (draftPending_ && GetTime() - lastDraft_ > 3.0) {
            lastDraft_ = GetTime();
            WriteDraft();
        }
        UpdatePreview();
        game::LivePreview::Get().Tick();
        render::DdsCache::Get().EndFrame();
    }

    void App::BeginFrame() {
        // The menu section and the editor window are separate callbacks in the same ImGui frame.
        if (GetFrameCount() == beganFrame_) {
            return;
        }
        beganFrame_ = GetFrameCount();
        HoldVanityCamera(true);
        HoldActor(settings.holdTarget ? targetId : 0);
        game::MtfBridge::DrainUi();
        HandleShortcuts();
    }
}
