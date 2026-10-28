#include "game/MtfBridge.h"

#include "game/OverlayVisibility.h"
#include "game/Papyrus.h"
#include "io/Translations.h"
#include "io/UiSettings.h"
#include "model/Catalogs.h"


namespace mtfui::game {
    namespace {
        std::mutex g_uiMutex;
        std::deque<std::function<void()>> g_uiQueue;
        std::atomic<int> g_session{ 0 };

        // Results of calls started before a save was loaded describe a world that is gone; drop them.
        template <class... A>
        std::function<void(A...)> Guarded(std::function<void(A...)> fn) {
            if (!fn) {
                return fn;
            }
            return [s = g_session.load(), fn = std::move(fn)](A... a) {
                if (s == g_session.load()) {
                    fn(std::forward<A>(a)...);
                }
            };
        }

        RE::Actor* ActorFor(RE::FormID id) {
            return id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        }

        RE::BSScript::Variable* Prop(const RE::BSTSmartPointer<RE::BSScript::Object>& obj, const char* name) {
            return obj ? obj->GetProperty(name) : nullptr;
        }

        constexpr const char* kSuspendedKey = "mtfui_suspended";

        const char* AreaProperty(int area) {
            static constexpr const char* names[] = { "OverlaySlot", "FaceOverlaySlot", "HandOverlaySlot", "FeetOverlaySlot" };
            return names[std::clamp(area, 0, 3)];
        }

        // Where MTF last painted the MCM block. Its tick wipes every overlay it owns, applied presets included,
        // whenever this differs from AreaProperty, and only redraws an applied preset when its tier changes.
        const char* CurrentAreaProperty(int area) {
            static constexpr const char* names[] = { "CurrentOverlaySlot", "CurrentFaceOverlaySlot", "CurrentHandOverlaySlot", "CurrentFeetOverlaySlot" };
            return names[std::clamp(area, 0, 3)];
        }

        constexpr const char* kCountFns[] = { "GetNumBodyOverlays", "GetNumFaceOverlays", "GetNumHandOverlays", "GetNumFeetOverlays" };
        constexpr const char* kCountCaches[] = { "_numOvBodyCache", "_numOvFaceCache", "_numOvHandCache", "_numOvFeetCache" };

        // Runs done once Arrive has been called n times, from whichever thread arrives last.
        class Join {
        public:
            Join(int n, std::function<void()> done) : left_(n), done_(std::move(done)) {}
            static std::shared_ptr<Join> Make(int n, std::function<void()> done) {
                auto j = std::make_shared<Join>(n, std::move(done));
                if (n <= 0) {
                    j->Fire();
                }
                return j;
            }
            void Arrive() {
                if (--left_ == 0) {
                    Fire();
                }
            }

        private:
            void Fire() {
                if (auto d = std::move(done_)) {
                    d();
                }
            }
            std::atomic<int> left_;
            std::function<void()> done_;
        };

        // MTF redraws on its next tick or two after one of these calls returns.
        void WatchRedraw() { WatchOverlaysFor(std::chrono::seconds(5)); }

        bool IsDefaultOverlay(std::string tex) {
            tex = io::ToLower(tex);
            return tex.empty() || tex.ends_with("default.dds");
        }

    }

    MtfBridge& MtfBridge::Get() {
        static MtfBridge instance;
        return instance;
    }

    void MtfBridge::RunOnGame(std::function<void()> fn) {
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(std::move(fn));
        }
    }

    void MtfBridge::PostToUi(std::function<void()> fn) {
        std::scoped_lock lock(g_uiMutex);
        g_uiQueue.push_back(std::move(fn));
    }

    void MtfBridge::DrainUi() {
        std::deque<std::function<void()>> work;
        {
            std::scoped_lock lock(g_uiMutex);
            work.swap(g_uiQueue);
        }
        for (auto& fn : work) {
            try {
                fn();
            } catch (const std::exception& e) {
                logger::error("UI callback threw: {}", e.what());
            } catch (...) {
                logger::error("UI callback threw an unknown exception");
            }
        }
    }

    void MtfBridge::NewSession() {
        ++g_session;
        std::scoped_lock lock(g_uiMutex);
        g_uiQueue.clear();
    }

    void MtfBridge::OnDataLoaded() {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName(kMtfPlugin)) {
            logger::warn("{} is not loaded; MTF actions are disabled", kMtfPlugin);
            return;
        }
        quest_ = data->LookupForm<RE::TESQuest>(kMainQuestLocalId, kMtfPlugin);
        if (!quest_) {
            logger::error("MTF main quest {:X} not found in {}", kMainQuestLocalId, kMtfPlugin);
            return;
        }
        logger::info("Found MTF main quest {:08X}", quest_->GetFormID());
    }

    void MtfBridge::Refresh() {
        if (!quest_) {
            return;
        }
        RunOnGame([this] {
            auto obj = ScriptObject(quest_, kMainQuestScript);
            if (!obj) {
                logger::warn("MTF's main quest has no {} script yet; its settings are unavailable", kMainQuestScript);
                PostToUi([this] {
                    snap_.loaded = false;
                    snap_.fresh = true;
                });
                return;
            }
            MtfSnapshot s;
            s.loaded = true;
            s.fresh = true;
            if (auto* v = Prop(obj, "ModActive")) {
                s.modActive = v->GetBool();
            }
            if (auto* v = Prop(obj, "DebugMode")) {
                s.debugMode = v->GetBool();
            }
            if (auto* v = Prop(obj, "updateInterval")) {
                s.updateInterval = v->GetFloat();
            }
            for (int a = 0; a < 4; ++a) {
                if (auto* v = Prop(obj, AreaProperty(a))) {
                    s.overlayStart[a] = v->GetSInt();
                }
            }
            if (auto* v = Prop(obj, "pluginCount")) {
                s.pluginCount = v->GetSInt();
            }

            // MTF saves _transitionEndRT, but real time restarts at 0 with each game launch. A value from an earlier
            // session makes _applyPulse fade the MCM preset's layer in over the old session's remaining time, which
            // can be hours, so it stays invisible. 0 is MTF's own "no transition running".
            CallStatic("Utility", "GetCurrentRealTime", [this](const RE::BSScript::Variable& r) {
                auto obj = ScriptObject(quest_, kMainQuestScript);
                auto* end = obj ? obj->GetVariable("_transitionEndRT") : nullptr;
                auto* dur = obj ? obj->GetVariable("_sTransitionDuration") : nullptr;
                if (!r.IsFloat() || !end || !end->IsFloat()) {
                    return;
                }
                const float now = r.GetFloat();
                const float longest = std::max(dur && dur->IsFloat() ? dur->GetFloat() : 0.0f, 0.0f) + 1.0f;
                if (end->GetFloat() > now + longest) {
                    logger::info("MTF's player tattoo was mid-fade from an earlier session ({:.0f} s left); finished it", end->GetFloat() - now);
                    end->SetFloat(0.0f);
                }
            });
            s.skyrimNet = RE::TESDataHandler::GetSingleton()->LookupModByName("SkyrimNet.esp") != nullptr;

            // Registered plugin ids: each entry of registeredPlugins is an MTF_Plugin script.
            std::vector<RE::BSTSmartPointer<RE::BSScript::Object>> plugins;
            if (auto* v = Prop(obj, "registeredPlugins"); v && v->IsArray()) {
                if (auto arr = v->GetArray()) {
                    for (std::uint32_t i = 0; i < arr->size() && static_cast<int>(i) < s.pluginCount; ++i) {
                        if (auto o = (*arr)[i].GetObject()) {
                            plugins.push_back(o);
                        }
                    }
                }
            }

            if (!mcmQuest_) {
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    for (auto* q : data->GetFormArray<RE::TESQuest>()) {
                        if (q && q->GetFile(0) && q->GetFile(0)->GetFilename() == kMtfPlugin && ScriptObject(q, kMcmScript)) {
                            mcmQuest_ = q;
                            break;
                        }
                    }
                }
            }
            if (auto mcm = ScriptObject(mcmQuest_, kMcmScript)) {
                if (auto* v = mcm->GetVariable("_editingPresetName"); v && v->IsString()) {
                    s.mcmEditing = std::string(v->GetString());
                }
            }

            // Every menu open refreshes; only log MTF's state when it changed.
            static std::string lastState;
            const auto state = std::format("MTF state: {}, {} plugin(s) registered, update every {:.1f} s, overlay starts Body {} Face {} Hands {} Feet {}",
                s.modActive ? "active" : "disabled", s.pluginCount, s.updateInterval, s.overlayStart[0], s.overlayStart[1], s.overlayStart[2],
                s.overlayStart[3]);
            if (state != lastState) {
                logger::info("{}", state);
                lastState = state;
            }

            PostToUi([this, s] {
                auto keep = std::move(snap_.pluginEnabled);
                auto keepIds = std::move(snap_.registeredPlugins);
                const int packs = snap_.visualPackCount;
                const bool rescan = snap_.rescanOnLoad;
                const bool sn = snap_.snPersist;
                snap_ = s;
                snap_.pluginEnabled = std::move(keep);
                snap_.registeredPlugins = std::move(keepIds);
                snap_.visualPackCount = packs;
                snap_.rescanOnLoad = rescan;
                snap_.snPersist = sn;
            });

            CallMethod(quest_, kMainQuestScript, "GetVisualPackCount", [this](const RE::BSScript::Variable& r) {
                const int n = r.IsInt() ? r.GetSInt() : 0;
                PostToUi([this, n] { snap_.visualPackCount = n; });
            });
            CallMethod(quest_, kMainQuestScript, "GetRescanOnLoad", [this](const RE::BSScript::Variable& r) {
                const bool b = r.IsBool() && r.GetBool();
                PostToUi([this, b] { snap_.rescanOnLoad = b; });
            });
            if (s.skyrimNet) {
                CallMethod(quest_, kMainQuestScript, "GetSkyrimNetPersistVisualChange", [this](const RE::BSScript::Variable& r) {
                    const bool b = r.IsBool() && r.GetBool();
                    PostToUi([this, b] { snap_.snPersist = b; });
                });
            }

            auto ids = std::make_shared<std::vector<std::string>>();
            auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(plugins.size()));
            if (plugins.empty()) {
                PostToUi([this] { snap_.registeredPlugins.clear(); });
            }
            auto idsMutex = std::make_shared<std::mutex>();
            for (auto& p : plugins) {
                CallObject(p, "GetPluginId", [this, ids, remaining, idsMutex](const RE::BSScript::Variable& r) {
                    {
                        std::scoped_lock lock(*idsMutex);
                        if (r.IsString()) {
                            ids->push_back(io::ToLower(std::string(r.GetString())));
                        }
                    }
                    if (--*remaining == 0) {
                        std::vector<std::string> out;
                        {
                            std::scoped_lock lock(*idsMutex);
                            out = *ids;
                        }
                        PostToUi([this, out] { snap_.registeredPlugins = out; });
                    }
                });
            }
        });
    }

    void MtfBridge::RefreshPlugins(const std::vector<std::string>& pluginIds) {
        if (!quest_) {
            return;
        }
        RunOnGame([this, pluginIds] {
            for (const auto& pid : pluginIds) {
                CallMethod(
                    quest_, kMainQuestScript, "IsPluginEnabled",
                    [this, pid](const RE::BSScript::Variable& r) {
                        const bool on = !r.IsBool() || r.GetBool();
                        PostToUi([this, pid, on] { snap_.pluginEnabled[io::ToLower(pid)] = on; });
                    },
                    RE::BSFixedString(pid));
            }
        });
    }

    void MtfBridge::SetModActive(bool on) {
        snap_.modActive = on;
        RunOnGame([this, on] {
            auto obj = ScriptObject(quest_, kMainQuestScript);
            if (auto* v = Prop(obj, "ModActive")) {
                v->SetBool(on);
            }
            if (on) {
                CallMethod(quest_, kMainQuestScript, "setRedraw", {});
                CallMethod(quest_, kMainQuestScript, "Rearm", {});
                WatchRedraw();
            }
        });
    }

    void MtfBridge::SetDebugMode(bool on) {
        snap_.debugMode = on;
        RunOnGame([this, on] {
            if (auto* v = Prop(ScriptObject(quest_, kMainQuestScript), "DebugMode")) {
                v->SetBool(on);
            }
        });
    }

    void MtfBridge::SetUpdateInterval(float seconds) {
        seconds = std::clamp(seconds, 0.1f, 45.0f);
        snap_.updateInterval = seconds;
        RunOnGame([this, seconds] {
            if (auto* v = Prop(ScriptObject(quest_, kMainQuestScript), "updateInterval")) {
                v->SetFloat(seconds);
            }
        });
    }

    void MtfBridge::MoveOverlayStart(int area, int start, std::function<void()> done) {
        area = std::clamp(area, 0, 3);
        snap_.overlayStart[area] = start;
        done = Guarded(std::move(done));
        // SetOverlaySlot clears what MTF painted and moves only the MCM preset's block. Marking the block as already
        // moved stops MTF's next tick from wiping everything again after _compactAppliedPresets has redrawn the
        // applied presets packed above the new block, keeping their tier and pulse.
        RunOnGame([this, area, start, done = std::move(done)]() mutable {
            CallMethod(
                quest_, kMainQuestScript, "SetOverlaySlot",
                [this, area, start, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                    if (auto* v = Prop(ScriptObject(quest_, kMainQuestScript), CurrentAreaProperty(area))) {
                        v->SetSInt(start);
                    }
                    RE::Actor* player = RE::PlayerCharacter::GetSingleton();
                    CallMethod(
                        quest_, kMainQuestScript, "_compactAppliedPresets",
                        [this, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                            CallMethod(quest_, kMainQuestScript, "setRedraw", {});
                            WatchRedraw();
                            if (done) {
                                PostToUi(std::move(done));
                            }
                        },
                        std::move(player));
                },
                RE::BSFixedString(kAreas[area]), static_cast<std::int32_t>(start));
        });
    }

    void MtfBridge::ClearMcmPreset(std::function<void()> done) {
        snap_.mcmEditing.clear();
        done = Guarded(std::move(done));
        // ResetEditor's removeOverlay also wipes the applied presets, and MTF only notices the emptied block and
        // repacks them for the body, so they are repacked here for every area.
        RunOnGame([this, done = std::move(done)]() mutable {
            logger::info("Clearing the preset loaded in MTF's MCM");
            CallMethod(quest_, kMainQuestScript, "ResetEditor", [this, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                if (auto mcm = ScriptObject(mcmQuest_, kMcmScript)) {
                    for (const char* name : { "_editingPresetName", "_editingPresetDisplay" }) {
                        if (auto* v = mcm->GetVariable(name); v && v->IsString()) {
                            v->SetString("");
                        }
                    }
                }
                RE::Actor* player = RE::PlayerCharacter::GetSingleton();
                CallMethod(
                    quest_, kMainQuestScript, "_compactAppliedPresets",
                    [this, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                        CallMethod(quest_, kMainQuestScript, "setRedraw", {});
                        WatchRedraw();
                        if (done) {
                            PostToUi(std::move(done));
                        }
                    },
                    std::move(player));
            });
        });
    }

    void MtfBridge::ReadSlotUsage(std::function<void(SlotUsage)> done) {
        if (!done) {
            return;
        }
        done = Guarded(std::move(done));
        if (!quest_) {
            return;
        }
        struct State {
            std::mutex m;
            SlotUsage u;
            std::vector<std::string> names;
            std::vector<std::array<int, 4>> bases;
            std::vector<std::array<int, 4>> layers;
        };
        auto st = std::make_shared<State>();

        auto finish = [st, done] {
            std::scoped_lock lock(st->m);
            for (std::size_t p = 0; p < st->names.size(); ++p) {
                for (int a = 0; a < 4; ++a) {
                    if (st->bases[p][a] >= 0 && st->layers[p][a] > 0) {
                        st->u.areas[a].presets.push_back({ st->names[p], st->bases[p][a], st->layers[p][a] });
                    }
                }
            }
            PostToUi([u = st->u, done] { done(u); });
        };

        // Second pass, once RaceMenu's counts are known: everything else, in parallel.
        auto details = [this, st, finish] {
            RunOnGame([this, st, finish] {
                auto obj = ScriptObject(quest_, kMainQuestScript);
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!obj || !player) {
                    finish();
                    return;
                }
                const auto* base = player->GetActorBase();
                const bool female = base && base->GetSex() == RE::SEX::kFemale;
                int slots = 0;
                for (int a = 0; a < 4; ++a) {
                    auto& area = st->u.areas[a];
                    if (area.total < 1) {
                        area.total = a == 0 ? 6 : 3;  // what MTF assumes when RaceMenu doesn't answer
                    }
                    if (auto* v = Prop(obj, AreaProperty(a))) {
                        area.start = v->GetSInt();
                    }
                    if (auto* v = obj->GetVariable(kCountCaches[a]); v && v->IsInt()) {
                        const int cached = v->GetSInt();
                        if (cached >= 0 && cached != area.total) {
                            logger::info("MTF had cached {} {} overlay slots; RaceMenu has {}. Corrected", cached, kAreas[a], area.total);
                            st->u.staleCounts[a] = cached;
                            v->SetSInt(area.total);
                        }
                    }
                    area.textures.assign(area.total, {});
                    slots += area.total;
                }

                auto all = Join::Make(4 + slots + 1, finish);
                for (int a = 0; a < 4; ++a) {
                    CallMethod(
                        quest_, kMainQuestScript, "_computePresetReservedLayers",
                        [st, all, a](const RE::BSScript::Variable& r) {
                            {
                                std::scoped_lock lock(st->m);
                                st->u.areas[a].mcmWant = r.IsInt() ? std::max(0, r.GetSInt()) : 0;
                            }
                            all->Arrive();
                        },
                        RE::BSFixedString(kAreas[a]), false);
                    for (int i = 0; i < st->u.areas[a].total; ++i) {
                        RE::TESObjectREFR* ref = player;
                        CallStatic(
                            "NiOverride", "GetNodeOverrideString",
                            [st, all, a, i](const RE::BSScript::Variable& r) {
                                if (r.IsString() && !IsDefaultOverlay(std::string(r.GetString()))) {
                                    std::scoped_lock lock(st->m);
                                    st->u.areas[a].textures[i] = std::string(r.GetString());
                                }
                                all->Arrive();
                            },
                            std::move(ref), female, RE::BSFixedString(std::format("{} [ovl{}]", kAreas[a], i)), static_cast<std::int32_t>(9),
                            static_cast<std::int32_t>(0));
                    }
                }

                RE::Actor* actor = player;
                CallMethod(
                    quest_, kMainQuestScript, "GetActorPresetCount",
                    [this, st, all](const RE::BSScript::Variable& r) {
                        const int n = r.IsInt() ? std::clamp(r.GetSInt(), 0, 256) : 0;
                        {
                            std::scoped_lock lock(st->m);
                            st->names.assign(n, {});
                            st->bases.assign(n, { -1, -1, -1, -1 });
                            st->layers.assign(n, { 0, 0, 0, 0 });
                        }
                        auto ranges = Join::Make(n * 8, [all] { all->Arrive(); });
                        auto named = Join::Make(n, [this, st, ranges, n] {
                            for (int p = 0; p < n; ++p) {
                                std::string name;
                                {
                                    std::scoped_lock lock(st->m);
                                    name = st->names[p];
                                }
                                for (int a = 0; a < 4; ++a) {
                                    for (const bool isBase : { true, false }) {
                                        RE::Actor* who = RE::PlayerCharacter::GetSingleton();
                                        CallMethod(
                                            quest_, kMainQuestScript, isBase ? "_getActorPresetBase" : "_getActorPresetLayers",
                                            [st, ranges, p, a, isBase](const RE::BSScript::Variable& v) {
                                                {
                                                    std::scoped_lock lock(st->m);
                                                    const int x = v.IsInt() ? v.GetSInt() : (isBase ? -1 : 0);
                                                    (isBase ? st->bases : st->layers)[p][a] = x;
                                                }
                                                ranges->Arrive();
                                            },
                                            std::move(who), RE::BSFixedString(name), RE::BSFixedString(kAreas[a]));
                                    }
                                }
                            }
                        });
                        for (int p = 0; p < n; ++p) {
                            RE::Actor* who = RE::PlayerCharacter::GetSingleton();
                            CallMethod(
                                quest_, kMainQuestScript, "GetActorPresetAt",
                                [st, named, p](const RE::BSScript::Variable& v) {
                                    if (v.IsString()) {
                                        std::scoped_lock lock(st->m);
                                        st->names[p] = std::string(v.GetString());
                                    }
                                    named->Arrive();
                                },
                                std::move(who), static_cast<std::int32_t>(p));
                        }
                    },
                    std::move(actor));
            });
        };

        RunOnGame([st, details] {
            auto counts = Join::Make(4, details);
            for (int a = 0; a < 4; ++a) {
                CallStatic("NiOverride", kCountFns[a], [st, counts, a](const RE::BSScript::Variable& r) {
                    {
                        std::scoped_lock lock(st->m);
                        st->u.areas[a].total = r.IsInt() ? r.GetSInt() : 0;
                    }
                    counts->Arrive();
                });
            }
        });
    }


    void MtfBridge::ReloadVisualPacks() {
        RunOnGame([this] {
            CallMethod(quest_, kMainQuestScript, "ForceReloadVisualCatalogs", [this](const RE::BSScript::Variable&) {
                CallMethod(quest_, kMainQuestScript, "setRedraw", {});
                WatchRedraw();
                CallMethod(quest_, kMainQuestScript, "GetVisualPackCount", [this](const RE::BSScript::Variable& r) {
                    const int n = r.IsInt() ? r.GetSInt() : 0;
                    PostToUi([this, n] { snap_.visualPackCount = n; });
                });
            });
        });
    }

    void MtfBridge::SetRescanOnLoad(bool on) {
        snap_.rescanOnLoad = on;
        RunOnGame([this, on] { CallMethod(quest_, kMainQuestScript, "SetRescanOnLoad", {}, on); });
    }

    void MtfBridge::SetPluginEnabled(const std::string& pluginId, bool on) {
        snap_.pluginEnabled[io::ToLower(pluginId)] = on;
        RunOnGame([this, pluginId, on] { CallMethod(quest_, kMainQuestScript, "SetPluginEnabled", {}, RE::BSFixedString(pluginId), on); });
    }

    void MtfBridge::SetSkyrimNetPersist(bool on) {
        snap_.snPersist = on;
        RunOnGame([this, on] { CallMethod(quest_, kMainQuestScript, "SetSkyrimNetPersistVisualChange", {}, on); });
    }

    void MtfBridge::StopShadersOn(RE::FormID actor) {
        RunOnGame([this, actor] {
            if (auto* a = ActorFor(actor)) {
                CallMethod(quest_, kMainQuestScript, "StopAllMtfShadersOn", {}, std::move(a));
            }
        });
    }

    void MtfBridge::PresetChanged(const std::string& name, bool builtin, std::function<void()> done) {
        if (!quest_) {
            if (done) {
                PostToUi(std::move(done));
            }
            return;
        }
        const std::string file = std::string("MagicTattoosFramework/") + (builtin ? "presets_builtin/" : "presets/") + name;
        const bool reloadEditor = !builtin && model::IEquals(snap_.mcmEditing, name);
        done = Guarded(std::move(done));
        RunOnGame([this, file, name, reloadEditor, done = std::move(done)]() mutable {
            CallStatic(
                "JsonUtil", "Unload",
                [this, name, reloadEditor, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                    CallMethod(
                        quest_, kMainQuestScript, "_invalidateScratchCache",
                        [this, name, reloadEditor, done = std::move(done)](const RE::BSScript::Variable&) mutable {
                            auto finish = [done = std::move(done)]() mutable {
                                if (done) {
                                    PostToUi(std::move(done));
                                }
                            };
                            if (reloadEditor) {
                                CallMethod(
                                    quest_, kMainQuestScript, "LoadPreset",
                                    [this, finish = std::move(finish)](const RE::BSScript::Variable&) mutable {
                                        CallMethod(quest_, kMainQuestScript, "setRedraw", {});
                                        WatchRedraw();
                                        finish();
                                    },
                                    RE::BSFixedString(name));
                            } else {
                                finish();
                            }
                        },
                        RE::BSFixedString(name));
                },
                RE::BSFixedString(file), false, false);
        });
    }

    void MtfBridge::Apply(RE::FormID actor, const std::string& name, std::function<void(int)> done) {
        done = Guarded(std::move(done));
        RunOnGame([this, actor, name, done = std::move(done)]() mutable {
            auto* a = ActorFor(actor);
            if (!a || !quest_) {
                logger::info("Apply {} skipped: actor {:08X} not found", name, actor);
                PostToUi([done = std::move(done)] {
                    if (done) {
                        done(-2);
                    }
                });
                return;
            }
            // A failed dispatch answers with None, which reads as -5 here.
            CallMethod(
                quest_, kMainQuestScript, "AddAppliedPreset",
                [done, name, actor](const RE::BSScript::Variable& r) {
                    const int rc = r.IsInt() ? r.GetSInt() : -5;
                    logger::info("AddAppliedPreset({:08X}, {}) returned {}", actor, name, rc);
                    WatchRedraw();
                    PostToUi([done, rc] {
                        if (done) {
                            done(rc);
                        }
                    });
                },
                std::move(a), RE::BSFixedString(name));
        });
    }

    void MtfBridge::Remove(RE::FormID actor, const std::string& name, std::function<void()> done) {
        done = Guarded(std::move(done));
        RunOnGame([this, actor, name, done = std::move(done)]() mutable {
            auto* a = ActorFor(actor);
            if (!a || !quest_) {
                if (done) {
                    PostToUi(std::move(done));
                }
                return;
            }
            CallMethod(
                quest_, kMainQuestScript, "RemoveAppliedPreset",
                [done = std::move(done)](const RE::BSScript::Variable&) mutable {
                    WatchRedraw();
                    if (done) {
                        PostToUi(std::move(done));
                    }
                },
                std::move(a), RE::BSFixedString(name));
        });
    }

    void MtfBridge::ListApplied(RE::FormID actor, std::function<void(std::vector<std::string>)> done) {
        if (!done) {
            return;
        }
        done = Guarded(std::move(done));
        RunOnGame([this, actor, done = std::move(done)]() mutable {
            auto* a = ActorFor(actor);
            if (!a || !quest_) {
                PostToUi([done = std::move(done)] { done({}); });
                return;
            }
            CallMethod(
                quest_, kMainQuestScript, "GetActorPresetCount",
                [this, actor, done](const RE::BSScript::Variable& r) {
                    const int n = r.IsInt() ? std::min(r.GetSInt(), 256) : 0;
                    if (n <= 0) {
                        PostToUi([done] { done({}); });
                        return;
                    }
                    auto names = std::make_shared<std::vector<std::string>>(n);
                    auto remaining = std::make_shared<std::atomic<int>>(n);
                    auto* a2 = ActorFor(actor);
                    for (int i = 0; i < n; ++i) {
                        CallMethod(
                            quest_, kMainQuestScript, "GetActorPresetAt",
                            [names, remaining, done, i](const RE::BSScript::Variable& v) {
                                if (v.IsString()) {
                                    (*names)[i] = std::string(v.GetString());
                                }
                                if (--*remaining == 0) {
                                    auto out = *names;
                                    std::erase(out, std::string{});
                                    PostToUi([done, out] { done(out); });
                                }
                            },
                            std::move(a2), static_cast<std::int32_t>(i));
                    }
                },
                std::move(a));
        });
    }

    void MtfBridge::WriteSuspended(RE::FormID actor, std::vector<std::string> names, std::function<void()> done) {
        done = Guarded(std::move(done));
        RunOnGame([actor, names = std::move(names), done = std::move(done)] {
            std::function<void()> finish = [done] {
                if (done) {
                    PostToUi(done);
                }
            };
            RE::TESForm* form = ActorFor(actor);
            if (!form) {
                finish();
                return;
            }
            CallStatic(
                "StorageUtil", "StringListClear",
                [actor, names, finish](const RE::BSScript::Variable&) {
                    RE::TESForm* form = ActorFor(actor);
                    if (!form || names.empty()) {
                        finish();
                        return;
                    }
                    auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(names.size()));
                    for (const auto& n : names) {
                        CallStatic(
                            "StorageUtil", "StringListAdd",
                            [remaining, finish](const RE::BSScript::Variable&) {
                                if (--*remaining == 0) {
                                    finish();
                                }
                            },
                            form, RE::BSFixedString(kSuspendedKey), RE::BSFixedString(n), false);
                    }
                },
                form, RE::BSFixedString(kSuspendedKey));
        });
    }

    void MtfBridge::ReadSuspended(RE::FormID actor, std::function<void(std::vector<std::string>)> done) {
        if (!done) {
            return;
        }
        done = Guarded(std::move(done));
        RunOnGame([actor, done = std::move(done)] {
            RE::TESForm* form = ActorFor(actor);
            if (!form) {
                PostToUi([done] { done({}); });
                return;
            }
            CallStatic(
                "StorageUtil", "StringListToArray",
                [done](const RE::BSScript::Variable& r) {
                    std::vector<std::string> out;
                    if (r.IsArray()) {
                        if (auto arr = r.GetArray()) {
                            for (std::uint32_t i = 0; i < arr->size(); ++i) {
                                if ((*arr)[i].IsString() && !(*arr)[i].GetString().empty()) {
                                    out.emplace_back((*arr)[i].GetString());
                                }
                            }
                        }
                    }
                    PostToUi([done, out] { done(out); });
                },
                form, RE::BSFixedString(kSuspendedKey));
        });
    }

    std::string ApplyResultText(int rc, bool onPlayer) {
        const char* fix = onPlayer ? T("$MTFUI_Apply_FixPlayer") : T("$MTFUI_Apply_FixNpc");
        switch (rc) {
            case 1: return T("$MTFUI_Apply_Applied");
            case 0: return T("$MTFUI_Apply_Already");
            case -2: return T("$MTFUI_Apply_NoTarget");
            case -3: return T("$MTFUI_Apply_TooManyNpcs");
            case -4: return T("$MTFUI_Apply_EmptyName");
            case -5: return T("$MTFUI_Apply_Unreadable");
            case -6: return TF("$MTFUI_Apply_NoSlots", fix);
            case -7: return TF("$MTFUI_Apply_Truncated", fix);
            default: return TF("$MTFUI_Apply_Unexpected", rc);
        }
    }
}
