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
        RE::FormID g_held = 0;

        void EnableAIOn(RE::FormID id) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (actor && !actor->IsAIEnabled()) {
                actor->EnableAI(true);
            }
        }
    }

    RE::FormID HoldStill(RE::FormID id) {
        if (id == g_held) {
            return g_held;
        }
        if (g_held != 0) {
            EnableAIOn(g_held);
            g_held = 0;
        }
        auto* actor = id != 0 ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        if (actor && !actor->IsPlayerRef() && actor->IsAIEnabled()) {
            actor->EnableAI(false);
            g_held = id;
        }
        return g_held;
    }

    void ReleaseLeftover(RE::FormID id) {
        g_held = 0;
        if (id != 0) {
            EnableAIOn(id);
        }
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
