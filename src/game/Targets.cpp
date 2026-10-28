#include "game/Targets.h"

#include "game/Papyrus.h"
#include "io/Translations.h"
#include "io/UiSettings.h"

namespace mtfui::game {
    namespace {
        ActorInfo Info(RE::Actor* actor, const RE::NiPoint3& from) {
            ActorInfo i;
            i.id = actor->GetFormID();
            const char* name = actor->GetDisplayFullName();
            i.name = name && *name ? name : T("$MTFUI_Target_Unnamed");
            i.player = actor->IsPlayerRef();
            i.distance = from.GetDistance(actor->GetPosition());
            if (auto* base = actor->GetActorBase()) {
                i.female = base->GetSex() == RE::SEX::kFemale;
            }
            return i;
        }

        bool Usable(RE::Actor* a) {
            return a && a->GetFormID() != 0 && a->Is3DLoaded() && !a->IsDead();
        }

        std::string DiffuseOf(RE::BGSTextureSet* set) {
            if (!set) {
                return {};
            }
            const char* path = set->textures[0].textureName.c_str();
            return path ? std::string(path) : std::string{};
        }
    }

    ActorInfo PlayerInfo() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return { 0x14, T("$MTFUI_Target_You"), 0.0f, true, false };
        }
        ActorInfo i = Info(player, player->GetPosition());
        i.name = TF("$MTFUI_Target_YouNamed", i.name);
        return i;
    }

    std::optional<ActorInfo> CrosshairActor() {
        auto* pick = RE::CrosshairPickData::GetSingleton();
        if (!pick) {
            return std::nullopt;
        }
        auto ref = pick->targetActor[0].get();
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!Usable(actor)) {
            return std::nullopt;
        }
        return Info(actor, RE::PlayerCharacter::GetSingleton()->GetPosition());
    }

    std::optional<ActorInfo> ConsoleActor() {
        auto ref = RE::Console::GetSelectedRef();
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!Usable(actor)) {
            return std::nullopt;
        }
        return Info(actor, RE::PlayerCharacter::GetSingleton()->GetPosition());
    }

    std::vector<ActorInfo> NearbyActors(float radius) {
        std::vector<ActorInfo> out;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!player || !lists) {
            return out;
        }
        const auto origin = player->GetPosition();
        lists->ForAllActors([&](RE::Actor* a) {
            if (a != player && Usable(a)) {
                const float d = origin.GetDistance(a->GetPosition());
                if (std::isfinite(d) && d <= radius) {
                    out.push_back(Info(a, origin));
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        std::sort(out.begin(), out.end(), [](const ActorInfo& a, const ActorInfo& b) { return a.distance < b.distance; });
        if (out.size() > 24) {
            out.resize(24);
        }
        return out;
    }

    std::optional<ActorInfo> Describe(RE::FormID id) {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor) {
            return std::nullopt;
        }
        if (actor->IsPlayerRef()) {
            return PlayerInfo();
        }
        return Info(actor, RE::PlayerCharacter::GetSingleton()->GetPosition());
    }

    SkinInfo SkinFor(RE::FormID id) {
        SkinInfo s;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
        if (!actor) {
            return s;
        }
        auto* base = actor->GetActorBase();
        s.female = base && base->GetSex() == RE::SEX::kFemale;
        const auto sex = s.female ? RE::SEX::kFemale : RE::SEX::kMale;
        auto* skin = actor->GetSkin();
        if (!skin) {
            return s;
        }
        using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
        for (auto* arma : skin->armorAddons) {
            if (!arma) {
                continue;
            }
            const auto diffuse = DiffuseOf(arma->skinTextures[sex]);
            if (diffuse.empty()) {
                continue;
            }
            if (arma->HasPartOf(Slot::kBody) && s.body.empty()) {
                s.body = diffuse;
            }
            if (arma->HasPartOf(Slot::kHands) && s.hands.empty()) {
                s.hands = diffuse;
            }
            if (arma->HasPartOf(Slot::kFeet) && s.feet.empty()) {
                s.feet = diffuse;
            }
        }
        return s;
    }

    namespace {
        // Papyrus SetDontMove is what holds the actor. The kMovementBlocked flag looked like the same thing, but the
        // engine clears it within a fraction of a second, so it held nobody. Switching AI off would stop the actor's
        // updates, and with them the live preview. Followers aren't told to wait either: WaitingForPlayer outlives the
        // menu if anything goes wrong, and strands them.
        //
        // Papyrus calls run later, so a save can be written while the actor is still held. mtfui_dontmove marks the
        // actor in PapyrusUtil's co-save before the hold and is cleared after the release, so the load cleanup knows
        // which actors this plugin left unable to move.
        constexpr const char* kHoldKey = "mtfui_dontmove";

        struct Held {
            RE::FormID id = 0;
            RE::NiPoint3 anchor;  // where CheckHold last saw them
            std::chrono::steady_clock::time_point lastMoveLog;
        };
        Held g_held;
        // SKSE tasks and Papyrus callbacks run on several threads. Recursive because a call that fails to dispatch
        // answers at once, on the thread that holds the lock.
        std::recursive_mutex g_heldMutex;

        RE::Actor* ActorById(RE::FormID id) { return id != 0 ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr; }

        bool Wanted(RE::FormID id) {
            std::scoped_lock lock(g_heldMutex);
            return g_held.id == id;
        }

        void SetMarker(RE::FormID id, std::function<void()> then = {}) {
            if (auto* actor = ActorById(id)) {
                CallStatic(
                    "StorageUtil", "SetIntValue",
                    [then = std::move(then)](const RE::BSScript::Variable&) {
                        if (then) {
                            then();
                        }
                    },
                    static_cast<RE::TESForm*>(actor), RE::BSFixedString(kHoldKey), static_cast<std::int32_t>(1));
            }
        }

        // Held again while this was in flight: the marker must stay.
        void ClearMarker(RE::FormID id) {
            if (auto* actor = ActorById(id)) {
                CallStatic(
                    "StorageUtil", "UnsetIntValue",
                    [id](const RE::BSScript::Variable&) {
                        if (Wanted(id)) {
                            SetMarker(id);
                        }
                    },
                    static_cast<RE::TESForm*>(actor), RE::BSFixedString(kHoldKey));
            }
        }

        // A hold and a release dispatched close together can finish in either order, so each result is checked
        // against what is wanted now and corrected until the two agree.
        void SetDontMove(RE::FormID id, bool on) {
            auto* actor = ActorById(id);
            if (!actor) {
                return;
            }
            CallMethod(
                actor, "Actor", "SetDontMove",
                [id, on](const RE::BSScript::Variable&) {
                    const bool want = Wanted(id);
                    if (want != on) {
                        SetDontMove(id, want);
                    } else if (!on) {
                        ClearMarker(id);
                    }
                },
                on);
        }

        void Hold(RE::Actor* actor) {
            const RE::FormID id = actor->GetFormID();
            g_held = { id, actor->GetPosition() };
            SetMarker(id, [id] { SetDontMove(id, true); });
        }

        void Release() {
            const RE::FormID id = std::exchange(g_held, {}).id;
            if (id != 0) {
                SetDontMove(id, false);
                logger::info("Released {:08X}", id);
            }
        }

        std::string ActorState(RE::Actor* actor) {
            std::string s = "no package";
            if (auto* pkg = actor->GetCurrentPackage()) {
                s = std::format("package {:08X}", pkg->GetFormID());
                const char* edid = pkg->GetFormEditorID();
                if (edid && *edid) {
                    s += std::format(" {}", edid);
                }
                if (auto* file = pkg->GetFile(0)) {
                    s += std::format(" from {}", file->GetFilename());
                }
            }
            if (auto* state = actor->AsActorState()) {
                s += std::format(", sit/sleep state {}", static_cast<int>(state->GetSitSleepState()));
            }
            if (actor->IsInCombat()) {
                s += ", in combat";
            }
            return s;
        }
    }

    void CheckHold() {
        std::scoped_lock lock(g_heldMutex);
        auto* actor = ActorById(g_held.id);
        if (!actor) {
            return;
        }
        const auto pos = actor->GetPosition();
        const auto now = std::chrono::steady_clock::now();
        if (pos.GetDistance(g_held.anchor) > 64.0f && now - g_held.lastMoveLog > 2s) {
            logger::warn("{:08X} moved {:.0f} units while held. {}", g_held.id, pos.GetDistance(g_held.anchor), ActorState(actor));
            g_held.anchor = pos;
            g_held.lastMoveLog = now;
        }
    }

    RE::FormID HoldStill(RE::FormID id) {
        std::scoped_lock lock(g_heldMutex);
        if (id == g_held.id) {
            return g_held.id;
        }
        Release();
        auto* actor = ActorById(id);
        if (actor && !actor->IsPlayerRef()) {
            Hold(actor);
            const char* name = actor->GetDisplayFullName();
            logger::info("Holding {:08X} ({}) in place", id, name ? name : "");
        }
        return g_held.id;
    }

    void ReleaseLeftover(const std::vector<RE::FormID>& ids) {
        {
            std::scoped_lock lock(g_heldMutex);
            g_held = {};
        }
        for (const auto id : ids) {
            auto* actor = ActorById(id);
            if (!actor || actor->IsPlayerRef()) {
                continue;
            }
            CallStatic(
                "StorageUtil", "GetIntValue",
                [id](const RE::BSScript::Variable& r) {
                    if (r.IsInt() && r.GetSInt() == 1) {
                        logger::info("Letting {:08X} move again, held when this save was made", id);
                        SetDontMove(id, false);
                    }
                },
                static_cast<RE::TESForm*>(actor), RE::BSFixedString(kHoldKey), static_cast<std::int32_t>(0));
        }
    }

    RE::FormID ReleaseForSave() {
        std::scoped_lock lock(g_heldMutex);
        const RE::FormID id = g_held.id;
        if (id != 0) {
            logger::info("Releasing {:08X} before saving", id);
            Release();
        }
        return id;
    }

    std::string TexturePath(std::string path) {
        std::replace(path.begin(), path.end(), '/', '\\');
        while (!path.empty() && path.front() == '\\') {
            path.erase(0, 1);
        }
        if (io::ToLower(path).rfind("data\\", 0) == 0) {
            path.erase(0, 5);
        }
        if (io::ToLower(path).rfind("textures\\", 0) != 0) {
            path = "textures\\" + path;
        }
        return path;
    }
}
