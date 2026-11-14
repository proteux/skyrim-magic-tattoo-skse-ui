#pragma once

#include "model/TacticalConfig.h"

// FormIDs in MagicTattoosTactical.esp, as its generator (skyrim-magic-tattoo-tactical, tools/EspGen) lays them out.
// Saves reference them, so they never move.
namespace mtfui::game {
    inline constexpr const char* kTacticalPlugin = "MagicTattoosTactical.esp";
    inline constexpr int kDispelVersions = 3;  // self, touch, ranged

    // Version v of slot i, v being a model::CastType.
    constexpr RE::FormID TacticalCastEffect(int v, int slot) { return 0x800 + v * 0x20 + slot; }
    constexpr RE::FormID TacticalCastSpell(int v, int slot) { return 0x900 + v * 0x20 + slot; }
    constexpr RE::FormID TacticalDecayEffect(int slot) { return 0x880 + slot; }
    constexpr RE::FormID TacticalDecaySpell(int slot) { return 0x980 + slot; }
    constexpr RE::FormID TacticalDispelEffect(int d) { return 0xA00 + d; }
    constexpr RE::FormID TacticalDispelSpell(int d) { return 0xA03 + d; }

    // AlterationNovice00 to RestorationNovice00 in Skyrim.esm, in model::School order. A spell's casting perk is what
    // makes the school's cost-reduction perks apply to it.
    inline constexpr RE::FormID kNovicePerkBase = 0x000F2CA6;
}
