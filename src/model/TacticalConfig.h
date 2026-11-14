#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <string>

// Magic Tattoos Tactical's spell setup, kept in StorageUtilData/MagicTattoosTactical/spells.json in PapyrusUtil's
// flat JsonUtil layout ({"float": {...}, "int": {...}, "string": {...}}), because its scripts read it with JsonUtil.
// Only this UI writes it.
namespace mtfui::model {
    inline constexpr int kTacticalSlots = 20;  // the spell slots MagicTattoosTactical.esp carries
    inline constexpr int kSchoolCount = 5;
    inline constexpr int kTacticalCostMax = 1000;
    inline constexpr double kDecayMin = 1.0;
    inline constexpr double kDecayMax = 86400.0;

    // The values are the scripts' and the ESP's: version v of slot i is spell 0x900 + v*0x20 + i.
    enum class CastType : int { SelfInstant = 0, SelfChannel = 1, Touch = 2, Ranged = 3 };
    inline constexpr int kCastTypeCount = 4;

    // Concentration spells: the tattoo stays while the channel holds, and the cost is per second.
    bool IsChanneled(CastType cast);

    // Schools in the order of the novice perks AlterationNovice00 (0x0F2CA6) to RestorationNovice00 (0x0F2CAA).
    enum class School : int { Alteration = 0, Conjuration = 1, Destruction = 2, Illusion = 3, Restoration = 4 };

    struct TacticalSlot {
        std::string name;    // empty: the default name
        std::string preset;  // MTF preset file name; empty: not set up
        CastType cast = CastType::Ranged;
        bool hostile = true;  // a Hex; false is a Blessing
        int cost = 30;        // magicka, per second when channeled
        School school = School::Illusion;
        double decay = 60.0;  // seconds the tattoo lasts after the cast or the channel ends
        bool permanent = false;
        bool operator==(const TacticalSlot&) const = default;
    };

    // The Dispel spell only comes as self, touch or ranged.
    struct TacticalDispel {
        std::string name;
        CastType cast = CastType::Ranged;
        int cost = 30;
        School school = School::Alteration;
        bool operator==(const TacticalDispel&) const = default;
    };

    struct TacticalConfig {
        std::array<TacticalSlot, kTacticalSlots> slots;
        TacticalDispel dispel;
        bool operator==(const TacticalConfig&) const = default;
    };

    // The Dispel version index (0 self, 1 touch, 2 ranged) for a cast type; SelfChannel counts as self.
    int DispelVersion(CastType cast);

    // Missing or malformed values keep their defaults, and every value is clamped to what the spells support.
    TacticalConfig TacticalFromJson(const nlohmann::json& j);
    nlohmann::json TacticalToJson(const TacticalConfig& config);
}
