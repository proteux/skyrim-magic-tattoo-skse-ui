#include "ui/App.h"

#include "game/Camera.h"
#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "game/Targets.h"
#include "io/JsonStore.h"
#include "model/Text.h"
#include "render/DdsCache.h"
#include "ui/Widgets.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        constexpr std::size_t kUndoDepth = 100;

        int IniInt(const fs::path& ini, const wchar_t* section, const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.wstring().c_str()));
        }

        std::string_view EmbeddedEnglish() {
            const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
            const HRSRC res = FindResourceW(module, L"MTFUI_ENGLISH", MAKEINTRESOURCEW(10));  // RT_RCDATA
            const HGLOBAL data = res ? LoadResource(module, res) : nullptr;
            const void* bytes = data ? LockResource(data) : nullptr;
            return bytes ? std::string_view(static_cast<const char*>(bytes), SizeofResource(module, res)) : std::string_view{};
        }

        // English is built into the DLL, so a missing or partial file never shows raw keys. A file in
        // Interface/Translations for the game's language (English included) goes over it.
        void LoadTranslations(const fs::path& data) {
            auto& tr = io::Translations::Get();
            tr.SetEnglish(EmbeddedEnglish());
            const auto* setting = RE::GetINISetting("sLanguage:General");
            std::string language = setting && setting->GetType() == RE::Setting::Type::kString && setting->GetString() ? setting->GetString() : "";
            std::transform(language.begin(), language.end(), language.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (language.empty()) {
                language = "ENGLISH";
            }
            const auto file = data / "Interface" / "Translations" / ("MagicTattoosUI_" + language + ".txt");
            const auto bytes = io::ReadText(file);
            if (!bytes) {
                logger::info("UI language {}: no {}, using English ({} strings)", language, io::PathText(file.filename()), tr.EnglishTable().size());
                return;
            }
            const auto result = tr.Overlay(*bytes);
            logger::info("UI language {}: {} of {} strings from {}", language, result.loaded, tr.EnglishTable().size(), io::PathText(file.filename()));
            for (const auto& key : result.rejected) {
                logger::warn("Kept {} in English: its printf placeholders don't match the English text", key);
            }
            for (const auto& key : result.unknown) {
                logger::warn("Ignored {}: not a Magic Tattoos UI string (renamed or removed?)", key);
            }
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
                auto& ids = settings.heldActors;
                if (held == 0 || (!ids.empty() && ids.front() == held)) {
                    return;
                }
                std::erase(ids, held);
                ids.insert(ids.begin(), held);
                if (ids.size() > 32) {
                    ids.resize(32);
                }
                SaveSettings();
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
        LoadTranslations(paths.data);
        targetName = T("$MTFUI_Target_You");
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
        // Only until RaceMenu itself answers, after a save loads.
        logger::info("Overlay slots in the ini files: Body {}, Face {}, Hands {}, Feet {}", overlayCapacity_["Body"], overlayCapacity_["Face"], overlayCapacity_["Hands"],
            overlayCapacity_["Feet"]);

        game::LivePreview::Get().Init(paths.mtfRoot / "presets_builtin", settings.previewActors, [this](const std::vector<RE::FormID>& ids) {
            settings.previewActors = ids;
            SaveSettings();
        });
        game::LivePreview::Get().SetListeners([this](const std::string& error) { Toast(error, true); }, [this] { RefreshApplied(); });
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
        appliedLooks_.clear();
    }

    void App::Toast(std::string text, bool error) {
        Toast(std::move(text), error, {}, {});
    }

    void App::Toast(std::string text, bool error, std::string actionLabel, std::function<void()> action) {
        if (error) {
            logger::warn("{}", text);
        }
        const double shown = action ? 12.0 : (error ? 8.0 : 4.0);
        toasts_.push_back({ std::move(text), error, GetTime() + shown, std::move(actionLabel), std::move(action) });
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
        if (slotUsage_) {
            for (int a = 0; a < 4; ++a) {
                ctx.overlayCapacity[game::kAreas[a]] = slotUsage_->areas[a].total;
            }
        }
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
            std::erase_if(names, [](const std::string& n) { return model::IEquals(n, game::kPreviewPreset); });
            // Taken off only for the preview: still applied as far as the user is concerned.
            for (const auto& s : game::LivePreview::Get().SuspendedOn(id)) {
                if (std::none_of(names.begin(), names.end(), [&](const std::string& n) { return model::IEquals(n, s); })) {
                    names.push_back(s);
                }
            }
            applied = std::move(names);
            appliedKnown = true;
        });
    }

    void App::ApplyToTarget(const std::string& name) {
        if (game::LivePreview::Get().IsSuspended(targetId, name)) {
            Toast(TF("$MTFUI_Toast_AlreadyOnHidden", name, targetName));
            return;
        }
        game::MtfBridge::Get().Apply(targetId, name, [this, name, onPlayer = targetId == 0x14](int rc) {
            const std::string text = rc == 1 ? TF("$MTFUI_Toast_AppliedTo", name, targetName) : TF("$MTFUI_Toast_ApplyResult", name, game::ApplyResultText(rc, onPlayer));
            if ((rc == -6 || rc == -7) && onPlayer) {
                Toast(text, true, T("$MTFUI_Toast_ShowOverlaySlots"), [this] { RequestTab(Tab::General); });
            } else {
                Toast(text, rc < 0);
            }
            SlotUsageChanged();
            RefreshApplied();
        });
    }

    void App::RemoveFromTarget(const std::string& name) {
        auto& lp = game::LivePreview::Get();
        if (lp.IsSuspended(targetId, name)) {
            lp.Forget(targetId, name);
            Toast(TF("$MTFUI_Toast_RemovedFrom", name, targetName));
            RefreshApplied();
            return;
        }
        game::MtfBridge::Get().Remove(targetId, name, [this, name] {
            Toast(TF("$MTFUI_Toast_RemovedFrom", name, targetName));
            SlotUsageChanged();
            RefreshApplied();
        });
    }

    void App::RemoveAllFromTarget() {
        auto& lp = game::LivePreview::Get();
        for (const auto& n : applied) {
            if (lp.IsSuspended(targetId, n)) {
                lp.Forget(targetId, n);
            } else {
                game::MtfBridge::Get().Remove(targetId, n, [this] {
                    SlotUsageChanged();
                    RefreshApplied();
                });
            }
        }
        Toast(TF("$MTFUI_Toast_RemovingAll", applied.size(), targetName));
        RefreshApplied();
    }

    bool App::HasMtfPreset() const {
        if (!game::MtfBridge::Get().Snapshot().mcmEditing.empty()) {
            return true;
        }
        return slotUsage_ && std::any_of(slotUsage_->areas.begin(), slotUsage_->areas.end(), [](const model::AreaSlots& a) { return a.mcmWant > 0; });
    }

    std::string App::MtfPresetName() const {
        const auto& name = game::MtfBridge::Get().Snapshot().mcmEditing;
        const auto* info = name.empty() ? nullptr : FindLibrary(name);
        return info ? info->displayName : name;
    }

    void App::RemoveMtfPreset() {
        const std::string name = MtfPresetName();
        game::MtfBridge::Get().ClearMcmPreset([this, name] {
            Toast(name.empty() ? std::string(T("$MTFUI_Toast_ClearedMtfPreset")) : TF("$MTFUI_Toast_ClearedNamedMtfPreset", name));
            if (slotUsage_) {
                for (auto& a : slotUsage_->areas) {
                    a.mcmWant = 0;
                }
            }
            SlotUsageChanged();
            RefreshApplied();
        });
    }

    void App::MoveOverlayStart(int area, int start) {
        if (overlayMoving_) {
            return;
        }
        area = std::clamp(area, 0, 3);
        if (!slotUsage_) {
            Toast(T("$MTFUI_Toast_StillReadingSlots"), true);
            return;
        }
        const auto& slots = slotUsage_->areas[area];
        const auto plan = model::PlanStart(slots, start, game::kPreviewPreset);
        if (!plan.fits) {
            Toast(TF("$MTFUI_Toast_StartNotMoved", start, plan.needed, model::AreaLabel(game::kAreas[area]), std::max(0, slots.total - start)), true);
            return;
        }
        overlayMove_ = OverlayMove{ area, start };
    }

    std::optional<int> App::OverlayMoveTarget(int area) const {
        if (overlayMove_ && overlayMove_->area == area) {
            return overlayMove_->start;
        }
        return std::nullopt;
    }

    void App::RunOverlayMove() {
        if (!overlayMove_ || overlayMoving_ || game::LivePreview::Get().Active()) {
            return;
        }
        overlayMoving_ = true;
        const auto move = *overlayMove_;
        logger::info("Moving MTF's {} overlay start to {}", game::kAreas[move.area], move.start);
        game::MtfBridge::Get().MoveOverlayStart(move.area, move.start, [this, move] {
            overlayMove_.reset();
            overlayMoving_ = false;
            Toast(TF("$MTFUI_Toast_StartMoved", model::AreaLabel(game::kAreas[move.area]), move.start));
            SlotUsageChanged();
            RefreshApplied();
        });
    }

    void App::WantSlotUsage() {
        if (!slotUsageBusy_ && GetTime() - slotUsageRead_ > 3.0) {
            ReadSlotUsage();
        }
    }

    void App::ReadSlotUsage() {
        if (!game::MtfBridge::Get().Installed()) {
            return;
        }
        slotUsageBusy_ = true;
        game::MtfBridge::Get().ReadSlotUsage([this](game::SlotUsage u) {
            slotUsageBusy_ = false;
            slotUsageRead_ = GetTime();
            std::string fixed;
            for (int a = 0; a < 4; ++a) {
                if (u.staleCounts[a] >= 0) {
                    const auto one = TF("$MTFUI_Toast_SlotCountFixed", model::AreaLabel(game::kAreas[a]), u.areas[a].total, u.staleCounts[a]);
                    fixed = fixed.empty() ? one : TF("$MTFUI_List_Comma", fixed, one);
                }
            }
            if (!fixed.empty()) {
                Toast(TF("$MTFUI_Toast_SlotCountsFixed", fixed));
            }
            slotUsage_ = std::move(u);
            Relint();
        });
    }

    std::vector<std::string> App::PreviewSuspends(int slot) {
        const auto& lp = game::LivePreview::Get();
        if (!appliedKnown) {
            // Don't put anything back just because MTF hasn't answered yet.
            return lp.SuspendedOn(targetId);
        }
        const auto vis = model::ResolveVisual(*ed.preset, slot);
        const std::string look = vis.none ? std::string{} : io::ToLower(vis.packId + "/" + vis.entryId);
        std::vector<std::string> out;
        for (const auto& name : applied) {
            if (!ed.isNew && model::IEquals(name, ed.preset->name)) {
                out.push_back(name);
                continue;
            }
            if (look.empty()) {
                continue;
            }
            const auto key = io::ToLower(name);
            auto it = appliedLooks_.find(key);
            if (it == appliedLooks_.end()) {
                std::vector<std::string> looks;
                if (const auto* info = FindLibrary(name)) {
                    if (auto json = io::ReadJson(info->path); json && json->is_object()) {
                        const auto p = model::PresetFromJson(info->name, *json);
                        for (int i = 0; i < static_cast<int>(p.slots.size()); ++i) {
                            const auto v = model::ResolveVisual(p, i);
                            if (!v.none && !v.packId.empty()) {
                                looks.push_back(io::ToLower(v.packId + "/" + v.entryId));
                            }
                        }
                    }
                }
                it = appliedLooks_.emplace(key, std::move(looks)).first;
            }
            if (std::find(it->second.begin(), it->second.end(), look) != it->second.end()) {
                out.push_back(name);
            }
        }
        return out;
    }

    void App::UpdatePreview() {
        auto& lp = game::LivePreview::Get();
        if (!livePreview || !ed.preset || !game::MtfBridge::Get().Installed() || overlayMove_) {
            if (lp.Active()) {
                lp.Stop();
            }
            return;
        }
        // Building the preview document every frame is wasteful; only a change to what it is built from matters.
        const int slot = std::clamp(ed.slot, 0, static_cast<int>(ed.preset->slots.size()) - 1);
        PreviewInputs in{ targetId, slot, previewEffects, ed.preset->slots[slot], ed.preset->slots[0], PreviewSuspends(slot) };
        if (lp.Active() && previewInputs_ == in) {
            return;
        }
        previewInputs_ = std::move(in);
        lp.Request(targetId, *ed.preset, slot, previewEffects, previewInputs_->suspend);
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
                    SetTarget(0x14, T("$MTFUI_Target_You"));
                }
                RefreshApplied();
            });
        });
        if (!ed.preset && !draftOffer_ && !settings.lastPreset.empty() && FindLibrary(settings.lastPreset)) {
            LoadPresetNow(settings.lastPreset);
        }
    }

    void App::OnMenuClose() {
        // Closing the main menu (F1) with the editor windows still up lets the player walk around; the target stays
        // held and the preview keeps running until those windows close.
        if (window_ && window_->IsOpen) {
            SaveSettings();
            return;
        }
        OnEditorClose();
    }

    void App::OnEditorClose() {
        logger::info("Editor closed");
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
        targetName = T("$MTFUI_Target_You");
        applied.clear();
        appliedKnown = false;
        overlayMove_.reset();
        overlayMoving_ = false;
        slotUsage_.reset();
        slotUsageBusy_ = false;
        game::LivePreview::Get().Cleanup();
        holdWanted_ = 0;
        game::MtfBridge::RunOnGame([ids = settings.heldActors] { game::ReleaseLeftover({ ids.begin(), ids.end() }); });
        game::MtfBridge::Get().Refresh();
        // Also corrects MTF's cached slot counts before anything is applied this session.
        ReadSlotUsage();
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
            CreateNew(T("$MTFUI_Preset_NewName"));
            RequestTab(Tab::Presets);
        } else if (IsKeyPressed(ImGuiKey_D, false) && ed.preset) {
            Duplicate(TF("$MTFUI_Preset_CopyName", ed.preset->displayName));
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
                Toast(T("$MTFUI_Toast_SaveBeforeApply"), true);
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
        RunOverlayMove();
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
        if (holdWanted_ != 0 && GetTime() - lastHoldCheck_ > 0.25) {
            lastHoldCheck_ = GetTime();
            game::MtfBridge::RunOnGame([] { game::CheckHold(); });
        }
        game::MtfBridge::DrainUi();
        HandleShortcuts();
    }
}
