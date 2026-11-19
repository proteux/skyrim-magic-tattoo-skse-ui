#include "game/PapyrusNatives.h"

#include "game/SpellbookBridge.h"
#include "model/SpellbookConfig.h"

namespace mtfui::game {
    namespace {
        constexpr const char* kScript = "MagicTattoosUI";

        RE::ActorValue PowerModifierOf(RE::ActorValue skill) {
            switch (skill) {
            case RE::ActorValue::kAlteration:
                return RE::ActorValue::kAlterationPowerModifier;
            case RE::ActorValue::kConjuration:
                return RE::ActorValue::kConjurationPowerModifier;
            case RE::ActorValue::kDestruction:
                return RE::ActorValue::kDestructionPowerModifier;
            case RE::ActorValue::kIllusion:
                return RE::ActorValue::kIllusionPowerModifier;
            case RE::ActorValue::kRestoration:
                return RE::ActorValue::kRestorationPowerModifier;
            default:
                return RE::ActorValue::kNone;
            }
        }

        // Fortify (School) potions and enchantments write the school's PowerModifier AV (percent).
        float SchoolPower(RE::Actor* caster, RE::SpellItem* spell) {
            float power = 1.0f;
            auto* costliest = spell->GetCostliestEffectItem();
            if (!costliest || !costliest->baseEffect) {
                return power;
            }
            const auto powerAv = PowerModifierOf(costliest->baseEffect->data.associatedSkill);
            if (powerAv != RE::ActorValue::kNone) {
                // AE 1.6.629+ moves ActorValueOwner; call through AsActorValueOwner, not Actor::GetActorValue.
                if (auto* values = caster->AsActorValueOwner()) {
                    power += values->GetActorValue(powerAv) / 100.0f;
                }
            }
            return power;
        }

        // Lands each effect on the target in place: no projectile from the caster. Magnitude follows the caster's
        // Fortify (School) power and Mod Spell Magnitude perks (Augmented Flames, and so on), then afScale. The
        // caster is blamed for anger, kill credit and crime.
        void CastAsScaled(RE::StaticFunctionTag*, RE::Actor* caster, RE::SpellItem* spell, RE::TESObjectREFR* target, float scale) {
            if (!caster || !spell || !target || scale <= 0.0f) {
                return;
            }
            auto* magicTarget = target->GetMagicTarget();
            if (!magicTarget) {
                return;
            }

            const float power = SchoolPower(caster, spell);
            for (auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) {
                    continue;
                }

                float magnitude = effect->GetMagnitude();
                using Flag = RE::EffectSetting::EffectSettingData::Flag;
                if (effect->baseEffect->data.flags.any(Flag::kPowerAffectsMagnitude)) {
                    magnitude *= power;
                    RE::BGSEntryPoint::HandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kModSpellMagnitude, caster, static_cast<RE::MagicItem*>(spell),
                        effect->baseEffect, std::addressof(magnitude));
                }
                magnitude *= scale;

                // AddTarget keeps effectItem.duration. Firebolt-style FF ValueModifiers use duration 1 as a
                // vestigial projectile value; left alone they multi-hit for that second. Concentration pulses
                // already bake time into afScale, so they need a single burst too. Lasting Fear / poison / buffs
                // (other archetypes, or VM duration > 1) keep their duration.
                const auto archetype = effect->baseEffect->GetArchetype();
                const bool valueMod = archetype == RE::EffectArchetype::kValueModifier || archetype == RE::EffectArchetype::kDualValueModifier;
                const bool burst = valueMod && (effect->baseEffect->data.castingType == RE::MagicSystem::CastingType::kConcentration ||
                                                effect->effectItem.duration <= 1);
                const auto savedDuration = effect->effectItem.duration;
                if (burst) {
                    effect->effectItem.duration = 0;
                }

                RE::MagicTarget::AddTargetData data{};
                data.caster = caster;
                data.magicItem = spell;
                data.effect = effect;
                data.source = nullptr;
                data.postCreationCallback = nullptr;
                data.resultsCollector = nullptr;
                data.explosionPoint = target->GetPosition();
                data.magnitude = magnitude;
                data.power = 1.0f;
                data.castingSource = RE::MagicSystem::CastingSource::kInstant;
                data.areaTarget = false;
                data.dualCasted = false;
                magicTarget->AddTarget(data);

                if (burst) {
                    effect->effectItem.duration = savedDuration;
                }
            }
        }

        void CastAs(RE::StaticFunctionTag* tag, RE::Actor* caster, RE::SpellItem* spell, RE::TESObjectREFR* target) {
            CastAsScaled(tag, caster, spell, target, 1.0f);
        }

        // Magic Tattoos Spellbook slot fields the scripts need at cast time. Read from the UI's in-memory config so
        // they don't depend on PapyrusUtil reloading spells.json.
        RE::BSFixedString GetSlotPreset(RE::StaticFunctionTag*, std::int32_t slot) {
            if (!SpellbookBridge::Get().Installed() || slot < 0 || slot >= model::kSpellbookSlots) {
                return "";
            }
            return SpellbookBridge::Get().Config().slots[slot].preset.c_str();
        }

        bool IsSlotPermanent(RE::StaticFunctionTag*, std::int32_t slot) {
            if (!SpellbookBridge::Get().Installed() || slot < 0 || slot >= model::kSpellbookSlots) {
                return false;
            }
            return SpellbookBridge::Get().Config().slots[slot].permanent;
        }

        bool Register(RE::BSScript::IVirtualMachine* vm) {
            vm->RegisterFunction("CastAs", kScript, CastAs);
            vm->RegisterFunction("CastAsScaled", kScript, CastAsScaled);
            vm->RegisterFunction("GetSlotPreset", kScript, GetSlotPreset);
            vm->RegisterFunction("IsSlotPermanent", kScript, IsSlotPermanent);
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
