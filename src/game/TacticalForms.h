#pragma once

#include "model/TacticalConfig.h"
#include "model/TacticalLayout.h"

// FormIDs in MagicTattoosTactical.esp. Formulas live in model/TacticalLayout.h and tools/EspGen/layout.json
// (copied to tests/fixtures/tactical-layout.json). Saves reference them, so they never move.
namespace mtfui::game {
    inline constexpr const char* kTacticalPlugin = "MagicTattoosTactical.esp";
    inline constexpr int kDispelVersions = 3;  // self, touch, ranged
    inline constexpr int kTacticalLayoutVersion = model::kTacticalLayoutVersion;

    // Version v of slot i, v being a model::CastType.
    constexpr RE::FormID TacticalCastEffect(int v, int slot) { return model::TacticalCastEffectId(v, slot); }
    constexpr RE::FormID TacticalCastSpell(int v, int slot) { return model::TacticalCastSpellId(v, slot); }
    constexpr RE::FormID TacticalDecayEffect(int slot) { return model::TacticalDecayEffectId(slot); }
    constexpr RE::FormID TacticalDecaySpell(int slot) { return model::TacticalDecaySpellId(slot); }
    constexpr RE::FormID TacticalDispelEffect(int d) { return model::TacticalDispelEffectId(d); }
    constexpr RE::FormID TacticalDispelSpell(int d) { return model::TacticalDispelSpellId(d); }
    constexpr RE::FormID TacticalLayoutVersionGlobal() { return model::kTacticalLayoutVersionGlobal; }

    // AlterationNovice00 to RestorationNovice00 in Skyrim.esm, in model::School order. A spell's casting perk is what
    // makes the school's cost-reduction perks apply to it.
    inline constexpr RE::FormID kNovicePerkBase = 0x000F2CA6;

    // Vanilla magic effects whose casting art, light, projectile and hit FX are copied onto tactical spells at
    // runtime. Indexed [school][hostile][castType]. Hostile 0 is a Blessing, 1 a Hex. Skyrim.esm FormIDs.
    // Templates: Alteration Oakflesh/DetectLife/Magelight vs Paralyze/Telekinesis; Conjuration BoundSword/SoulTrap
    // vs BanishDaedra/RaiseZombie; Destruction FlameCloak/Flames/Firebolt vs FrostCloak/Frostbite/IceSpike;
    // Illusion Harmony/Clairvoyance/Courage vs Mayhem/Fury; Restoration
    // FastHealing/Healing/HealingHands/HealOther vs CircleOfProtection/WardLesser/TurnUndead.
    inline constexpr RE::FormID kTacticalVisuals[model::kSchoolCount][2][model::kCastTypeCount] = {
        // Alteration
        {
            {0x051B15, 0x01EA74, 0x01EA6D, 0x01EA6D},  // blessing
            {0x01EA6E, 0x01A4CB, 0x01EA6E, 0x01EA6E},  // hex
        },
        // Conjuration
        {
            {0x01CE9F, 0x01CE9F, 0x04DBA3, 0x04DBA3},
            {0x06D22B, 0x06D22B, 0x06D22B, 0x065BD6},
        },
        // Destruction
        {
            {0x03AE9E, 0x013CA9, 0x013CA9, 0x012F03},
            {0x03AEA0, 0x013CAA, 0x013CAA, 0x01CEA2},
        },
        // Illusion
        {
            {0x09E0BD, 0x02113F, 0x01EA79, 0x01EA79},
            {0x07E8D6, 0x07E8D6, 0x04DEE6, 0x04DEE6},
        },
        // Restoration
        {
            {0x01CEA6, 0x01CEA4, 0x01CEA5, 0x01CEA7},
            {0x05312C, 0x00014C, 0x05DD5B, 0x05DD5B},
        },
    };
}
