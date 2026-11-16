#pragma once

#include <cstdint>

// FormID layout for MagicTattoosTactical.esp. Source of truth is skyrim-magic-tattoo-tactical's
// tools/EspGen/layout.json (copied to tests/fixtures/tactical-layout.json). Saves reference these IDs, so they
// never move; bump layoutVersion in that JSON when the layout itself changes.
namespace mtfui::model {
    inline constexpr int kTacticalLayoutVersion = 1;
    inline constexpr std::uint32_t kTacticalLayoutVersionGlobal = 0xA81;

    inline constexpr std::uint32_t kTacticalQuest = 0xA80;
    inline constexpr std::uint32_t kCastEffectBase = 0x800;
    inline constexpr std::uint32_t kCastSpellBase = 0x900;
    inline constexpr std::uint32_t kDecayEffectBase = 0x880;
    inline constexpr std::uint32_t kDecaySpellBase = 0x980;
    inline constexpr std::uint32_t kDispelEffectBase = 0xA00;
    inline constexpr std::uint32_t kDispelSpellBase = 0xA03;
    inline constexpr std::uint32_t kCastStride = 0x20;

    constexpr std::uint32_t TacticalCastEffectId(int v, int slot) { return kCastEffectBase + static_cast<std::uint32_t>(v) * kCastStride + slot; }
    constexpr std::uint32_t TacticalCastSpellId(int v, int slot) { return kCastSpellBase + static_cast<std::uint32_t>(v) * kCastStride + slot; }
    constexpr std::uint32_t TacticalDecayEffectId(int slot) { return kDecayEffectBase + slot; }
    constexpr std::uint32_t TacticalDecaySpellId(int slot) { return kDecaySpellBase + slot; }
    constexpr std::uint32_t TacticalDispelEffectId(int d) { return kDispelEffectBase + d; }
    constexpr std::uint32_t TacticalDispelSpellId(int d) { return kDispelSpellBase + d; }
}
