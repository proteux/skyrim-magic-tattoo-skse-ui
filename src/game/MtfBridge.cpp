#include "game/MtfBridge.h"

#include "game/Papyrus.h"
#include "io/UiSettings.h"
#include "model/Catalogs.h"

namespace mtfui::game {
    namespace {
        std::mutex g_uiMutex;
        std::deque<std::function<void()>> g_uiQueue;

        RE::Actor* ActorFor(RE::FormID id) {
            return id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        }

        RE::BSScript::Variable* Prop(const RE::BSTSmartPointer<RE::BSScript::Object>& obj, const char* name) {
            return obj ? obj->GetProperty(name) : nullptr;
        }

        const char* AreaProperty(int area) {
            static constexpr const char* names[] = { "OverlaySlot", "FaceOverlaySlot", "HandOverlaySlot", "FeetOverlaySlot" };
            return names[std::clamp(area, 0, 3)];
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
            fn();
        }
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
                auto* fargs = RE::MakeFunctionArguments();
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb(
                    new CallbackFunctor([this, ids, remaining, idsMutex](const RE::BSScript::Variable& r) {
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
                    }));
                auto obj2 = p;
                VM()->DispatchMethodCall(obj2, "GetPluginId", fargs, cb);
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

    void MtfBridge::SetOverlaySlot(int area, int start) {
        area = std::clamp(area, 0, 3);
        snap_.overlayStart[area] = start;
        RunOnGame([this, area, start] {
            CallMethod(quest_, kMainQuestScript, "SetOverlaySlot", {}, RE::BSFixedString(kAreas[area]), static_cast<std::int32_t>(start));
        });
    }

    void MtfBridge::ReloadVisualPacks() {
        RunOnGame([this] {
            CallMethod(quest_, kMainQuestScript, "ForceReloadVisualCatalogs", [this](const RE::BSScript::Variable&) {
                CallMethod(quest_, kMainQuestScript, "setRedraw", {});
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
        RunOnGame([this, actor, name, done = std::move(done)]() mutable {
            auto* a = ActorFor(actor);
            if (!a || !quest_) {
                PostToUi([done = std::move(done)] {
                    if (done) {
                        done(-2);
                    }
                });
                return;
            }
            const bool sent = CallMethod(
                quest_, kMainQuestScript, "AddAppliedPreset",
                [done](const RE::BSScript::Variable& r) {
                    const int rc = r.IsInt() ? r.GetSInt() : -5;
                    PostToUi([done, rc] {
                        if (done) {
                            done(rc);
                        }
                    });
                },
                std::move(a), RE::BSFixedString(name));
            if (!sent) {
                PostToUi([done] {
                    if (done) {
                        done(-5);
                    }
                });
            }
        });
    }

    void MtfBridge::Remove(RE::FormID actor, const std::string& name, std::function<void()> done) {
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
                    if (done) {
                        PostToUi(std::move(done));
                    }
                },
                std::move(a), RE::BSFixedString(name));
        });
    }

    void MtfBridge::ListApplied(RE::FormID actor, std::function<void(std::vector<std::string>)> done) {
        RunOnGame([this, actor, done = std::move(done)]() mutable {
            auto* a = ActorFor(actor);
            if (!a || !quest_) {
                PostToUi([done = std::move(done)] { done({}); });
                return;
            }
            CallMethod(
                quest_, kMainQuestScript, "GetActorPresetCount",
                [this, actor, done](const RE::BSScript::Variable& r) {
                    const int n = r.IsInt() ? r.GetSInt() : 0;
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

    std::string ApplyResultText(int rc) {
        switch (rc) {
            case 1: return "Applied";
            case 0: return "Already applied to this actor";
            case -2: return "No target actor";
            case -3: return "MTF is already tracking the maximum number of NPCs";
            case -4: return "Preset name is empty";
            case -5: return "MTF could not read the preset";
            case -6: return "No free overlay slots on this actor for the preset's tattoos";
            case -7: return "Not enough overlay slots; the tattoo would be cut off";
            default: return "Unexpected result " + std::to_string(rc);
        }
    }
}
