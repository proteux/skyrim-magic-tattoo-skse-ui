#include "game/PapyrusNatives.h"

namespace mtfui::game {
    namespace {
        constexpr const char* kScript = "MagicTattoosUI";

        // The spell lands as if the caster cast it: their perks, Fortify (School) potions and the difficulty
        // setting count, and they get the blame for anger, kill credit and crime. No animation or projectile.
        void CastAs(RE::StaticFunctionTag*, RE::Actor* caster, RE::SpellItem* spell, RE::TESObjectREFR* target) {
            if (!caster || !spell || !target) {
                return;
            }
            if (auto* magicCaster = caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
                magicCaster->CastSpellImmediate(spell, false, target, 1.0f, false, 0.0f, caster);
            }
        }

        bool Register(RE::BSScript::IVirtualMachine* vm) {
            vm->RegisterFunction("CastAs", kScript, CastAs);
            logger::info("Registered Papyrus natives on {}", kScript);
            return true;
        }
    }

    void RegisterPapyrusNatives() {
        if (!SKSE::GetPapyrusInterface()->Register(Register)) {
            logger::error("Could not register Papyrus natives on {}", kScript);
        }
    }
}
