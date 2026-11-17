#pragma once

#include "game/SpellbookForms.h"
#include "model/SpellbookConfig.h"

namespace mtfui::game {
    // Index kSpellbookDispel is the Dispel spell; 0 to kSpellbookSlots - 1 are the spell slots.
    inline constexpr int kSpellbookDispel = model::kSpellbookSlots;
    inline constexpr int kSpellbookSpells = model::kSpellbookSlots + 1;

    // Per spell, the version the player knows (a model::CastType value), or -1.
    using KnownSpells = std::array<int, kSpellbookSpells>;

    // What SyncKnown did while saving or loading: forgotten presets and cast-type swaps (hotkeys need re-equip).
    struct KnownSync {
        KnownSpells known{};
        int forgotten = 0;
        int swapped = 0;
    };

    // Magic Tattoos Spellbook, when its ESP is loaded. Its spells are set up here: the UI writes spells.json, and the
    // names, costs, schools, hostility, casting visuals and decay times go onto the ESP's records while the game
    // runs. Those changes aren't saved, so they go on again at every load. Which spells the player knows is the
    // player's spell list, which the save keeps.
    class SpellbookBridge {
    public:
        static SpellbookBridge& Get();

        void OnDataLoaded(const std::filesystem::path& configFile);
        bool Installed() const { return installed_; }
        const model::SpellbookConfig& Config() const { return config_; }

        // Writes spells.json, makes PapyrusUtil reread it, updates the records, and swaps a known spell to the
        // version for its new cast type. done gets what the player knows afterwards, plus forget/swap counts.
        bool Save(const model::SpellbookConfig& config, std::string* error, std::function<void(KnownSync)> done);
        void OnGameLoaded();

        void ReadKnown(std::function<void(KnownSpells)> done);
        // Learns the version for the spell's saved cast type.
        void Learn(int index, std::function<void(KnownSpells)> done);
        void Forget(int index, std::function<void(KnownSpells)> done);

        // The name the spell has in game: its own, or the default.
        static std::string SpellName(const model::SpellbookConfig& config, int index);

    private:
        RE::SpellItem* Version(int index, int cast) const;  // nullptr for a cast type the spell doesn't have
        void ApplyRecords(const model::SpellbookConfig& config);  // game thread
        KnownSync SyncKnown(const model::SpellbookConfig& config);  // game thread
        void SetKnown(int index, int cast);  // game thread; cast -1 forgets every version
        KnownSpells ReadKnownNow() const;  // game thread

        bool installed_ = false;
        std::filesystem::path file_;
        model::SpellbookConfig config_;
        std::array<std::array<RE::SpellItem*, model::kCastTypeCount>, model::kSpellbookSlots> cast_{};
        std::array<RE::SpellItem*, model::kSpellbookSlots> decay_{};
        std::array<RE::SpellItem*, kDispelVersions> dispel_{};
    };
}
