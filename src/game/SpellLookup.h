#pragma once

#include <unordered_set>

namespace mtfui::game {
    // Why a spell suits being applied straight to actors poorly, whatever the tattoo effect.
    enum class SpellCaveat { None, Summon, Corpse, CasterOnly, Ward };

    struct SpellEffectInfo {
        std::string name;
        float magnitude = 0.0f;  // 0 when the effect has none
        std::uint32_t area = 0;  // feet
        std::uint32_t duration = 0;  // seconds
    };

    struct SpellEntry {
        std::string name;
        int school = -1;  // model::School, or -1 for none
        int level = -1;  // 0 Novice to 4 Master, or -1 for none
        bool concentration = false;
        int delivery = 0;  // RE::MagicSystem::Delivery
        int cost = 0;  // base magicka, per second for concentration spells
        std::uint32_t duration = 0;  // longest lasting effect, seconds; 0 when every effect is instant
        bool harmful = false;  // the costliest effect is hostile
        SpellCaveat caveat = SpellCaveat::None;
        std::vector<SpellEffectInfo> effects;
        std::string plugin;
        std::string value;  // "Skyrim.esm|0x012FCD", as catalogs with "lookup": "spell" store it
        RE::FormID formId = 0;
    };

    // Every castable spell in the load order, for the spell picker. Built once the game's data has loaded; spells
    // never change while the game runs.
    class SpellLookup {
    public:
        static SpellLookup& Get();

        // Game thread, at kDataLoaded. Also hands the model its name lookup.
        void OnDataLoaded();
        bool Ready() const { return ready_; }
        const std::vector<SpellEntry>& All() const { return spells_; }
        const SpellEntry* Find(std::string_view value) const;

        // The form IDs of the listed spells the player knows. Dropped if a save loads before it returns.
        void ReadKnown(std::function<void(std::unordered_set<RE::FormID>)> done) const;

    private:
        bool ready_ = false;
        std::vector<SpellEntry> spells_;
        std::unordered_map<std::string, std::size_t> byValue_;  // normalized value to index
    };
}
