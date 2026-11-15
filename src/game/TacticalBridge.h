#pragma once

#include "game/TacticalForms.h"
#include "model/TacticalConfig.h"

namespace mtfui::game {
    // Index kTacticalDispel is the Dispel spell; 0 to kTacticalSlots - 1 are the spell slots.
    inline constexpr int kTacticalDispel = model::kTacticalSlots;
    inline constexpr int kTacticalSpells = model::kTacticalSlots + 1;

    // Per spell, the version the player knows (a model::CastType value), or -1.
    using KnownSpells = std::array<int, kTacticalSpells>;

    // Magic Tattoos Tactical, when its ESP is loaded. Its spells are set up here: the UI writes spells.json, and the
    // names, costs, schools, hostility, casting visuals and decay times go onto the ESP's records while the game
    // runs. Those changes aren't saved, so they go on again at every load. Which spells the player knows is the
    // player's spell list, which the save keeps.
    class TacticalBridge {
    public:
        static TacticalBridge& Get();

        void OnDataLoaded(const std::filesystem::path& configFile);
        bool Installed() const { return installed_; }
        const model::TacticalConfig& Config() const { return config_; }

        // Writes spells.json, makes PapyrusUtil reread it, updates the records, and swaps a known spell to the
        // version for its new cast type. done gets what the player knows afterwards.
        bool Save(const model::TacticalConfig& config, std::string* error, std::function<void(KnownSpells)> done);
        void OnGameLoaded();

        void ReadKnown(std::function<void(KnownSpells)> done);
        // Learns the version for the spell's saved cast type.
        void Learn(int index, std::function<void(KnownSpells)> done);
        void Forget(int index, std::function<void(KnownSpells)> done);

        // The name the spell has in game: its own, or the default.
        static std::string SpellName(const model::TacticalConfig& config, int index);

    private:
        RE::SpellItem* Version(int index, int cast) const;  // nullptr for a cast type the spell doesn't have
        void ApplyRecords(const model::TacticalConfig& config);  // game thread
        void SyncKnown(const model::TacticalConfig& config);  // game thread
        void SetKnown(int index, int cast);  // game thread; cast -1 forgets every version
        KnownSpells ReadKnownNow() const;  // game thread

        bool installed_ = false;
        std::filesystem::path file_;
        model::TacticalConfig config_;
        std::array<std::array<RE::SpellItem*, model::kCastTypeCount>, model::kTacticalSlots> cast_{};
        std::array<RE::SpellItem*, model::kTacticalSlots> decay_{};
        std::array<RE::SpellItem*, kDispelVersions> dispel_{};
    };
}
