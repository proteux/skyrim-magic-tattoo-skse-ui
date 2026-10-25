#include "game/Targets.h"

#include "io/UiSettings.h"

namespace mtfui::game {
    namespace {
        ActorInfo Info(RE::Actor* actor, const RE::NiPoint3& from) {
            ActorInfo i;
            i.id = actor->GetFormID();
            const char* name = actor->GetDisplayFullName();
            i.name = name && *name ? name : "Unnamed";
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
            return { 0x14, "You", 0.0f, true, false };
        }
        ActorInfo i = Info(player, player->GetPosition());
        i.name = "You (" + i.name + ")";
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
        using Flag = RE::Actor::BOOL_FLAGS;

        // What HoldStill changed on the held actor, so releasing undoes exactly that and nothing else.
        struct Held {
            RE::FormID id = 0;
            bool blockedMovement = false;
            bool setWaiting = false;
            float waitingBefore = 0.0f;
        };
        Held g_held;

        // Switching AI off would also stop the actor's updates, and with them the live preview.
        void Hold(RE::Actor* actor) {
            g_held = { actor->GetFormID() };
            auto& flags = actor->GetActorRuntimeData().boolFlags;
            if (!flags.all(Flag::kMovementBlocked)) {
                flags.set(Flag::kMovementBlocked);
                g_held.blockedMovement = true;
            }
            auto* av = actor->AsActorValueOwner();
            if (actor->IsPlayerTeammate() && av) {
                const float before = av->GetActorValue(RE::ActorValue::kWaitingForPlayer);
                if (before == 0.0f) {
                    av->SetActorValue(RE::ActorValue::kWaitingForPlayer, 1.0f);
                    g_held.setWaiting = true;
                    g_held.waitingBefore = before;
                    actor->EvaluatePackage();
                }
            }
        }

        void Release() {
            const Held held = std::exchange(g_held, {});
            auto* actor = held.id != 0 ? RE::TESForm::LookupByID<RE::Actor>(held.id) : nullptr;
            if (!actor) {
                return;
            }
            if (held.blockedMovement) {
                actor->GetActorRuntimeData().boolFlags.reset(Flag::kMovementBlocked);
            }
            if (held.setWaiting) {
                if (auto* av = actor->AsActorValueOwner()) {
                    av->SetActorValue(RE::ActorValue::kWaitingForPlayer, held.waitingBefore);
                }
                actor->EvaluatePackage();
            }
        }
    }

    RE::FormID HoldStill(RE::FormID id) {
        if (id == g_held.id) {
            return g_held.id;
        }
        Release();
        auto* actor = id != 0 ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        if (actor && !actor->IsPlayerRef()) {
            Hold(actor);
            const char* name = actor->GetDisplayFullName();
            logger::info("Holding {:08X} ({}) in place{}", id, name ? name : "", g_held.setWaiting ? ", told to wait" : "");
        }
        return g_held.id;
    }

    void ReleaseLeftover(RE::FormID id) {
        g_held = {};
        // FF ids are created at runtime and reused, so in this session the id may name an unrelated actor.
        auto* actor = id != 0 && (id >> 24) != 0xFF ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        if (!actor) {
            return;
        }
        logger::info("Letting {:08X} move again, held when the game last quit", id);
        actor->GetActorRuntimeData().boolFlags.reset(Flag::kMovementBlocked);
        // Builds before 1.2 held actors by switching their AI off.
        if (!actor->IsAIEnabled()) {
            actor->EnableAI(true);
        }
    }

    RE::FormID ReleaseForSave() {
        const RE::FormID id = g_held.id;
        if (id != 0) {
            Release();
            logger::info("Released {:08X} before saving", id);
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
