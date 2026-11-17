#pragma once

#include <cstdint>

// FormID layout for MagicTattoosSpellbook.esp. Source of truth is skyrim-magic-tattoo-spellbook's
// tools/EspGen/layout.json (copied to tests/fixtures/spellbook-layout.json). Saves reference these IDs, so they
// never move; bump layoutVersion in that JSON when the layout itself changes.
namespace mtfui::model {
    inline constexpr int kSpellbookLayoutVersion = 1;
    inline constexpr std::uint32_t kSpellbookLayoutVersionGlobal = 0xA81;

    inline constexpr std::uint32_t kSpellbookQuest = 0xA80;
    inline constexpr std::uint32_t kCastEffectBase = 0x800;
    inline constexpr std::uint32_t kCastSpellBase = 0x900;
    inline constexpr std::uint32_t kDecayEffectBase = 0x880;
    inline constexpr std::uint32_t kDecaySpellBase = 0x980;
    inline constexpr std::uint32_t kDispelEffectBase = 0xA00;
    inline constexpr std::uint32_t kDispelSpellBase = 0xA03;
    inline constexpr std::uint32_t kCastStride = 0x20;

    constexpr std::uint32_t SpellbookCastEffectId(int v, int slot) { return kCastEffectBase + static_cast<std::uint32_t>(v) * kCastStride + slot; }
    constexpr std::uint32_t SpellbookCastSpellId(int v, int slot) { return kCastSpellBase + static_cast<std::uint32_t>(v) * kCastStride + slot; }
    constexpr std::uint32_t SpellbookDecayEffectId(int slot) { return kDecayEffectBase + slot; }
    constexpr std::uint32_t SpellbookDecaySpellId(int slot) { return kDecaySpellBase + slot; }
    constexpr std::uint32_t SpellbookDispelEffectId(int d) { return kDispelEffectBase + d; }
    constexpr std::uint32_t SpellbookDispelSpellId(int d) { return kDispelSpellBase + d; }
}
