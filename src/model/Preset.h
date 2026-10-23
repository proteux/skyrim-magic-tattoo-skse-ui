#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mtfui::model {
    using Json = nlohmann::json;

    inline constexpr int kSlotCount = 8;  // Default + the 7 tiers MTF's MCM shows
    inline constexpr int kLayerCount = 4;
    inline constexpr int kEffectCap = 8;
    inline constexpr int kConditionCap = 8;
    inline constexpr int kSchemaVersion = 9;
    inline constexpr const char* kNoTexture = "<none>";

    // A catalog parameter value: sliders and colors are ints, menus and text are string ids.
    using ParamValue = std::variant<std::int64_t, std::string>;

    std::int64_t ParamInt(const ParamValue& v);
    std::string ParamStr(const ParamValue& v);
    ParamValue ParamFromJson(const Json& j);
    Json ParamToJson(const ParamValue& v);

    struct ConditionItem {
        std::string pluginId;  // "<plugin>:<condition>"
        ParamValue param = std::int64_t{ 0 };
        ParamValue param2 = std::int64_t{ 0 };
        ParamValue param3 = std::int64_t{ 0 };
        bool operator==(const ConditionItem&) const = default;
    };

    struct Layer {
        std::uint32_t tint = 0xFFFFFF;
        std::uint32_t emissive = 0xFFFFFF;
        double emissiveMult = 0.0;
        std::int64_t alpha = 100;
        bool operator==(const Layer&) const = default;
    };

    struct Pulse {
        double rate = 0.0;  // cycles per second
        std::int64_t depth = 0;  // percent
        double pause = 0.0;  // seconds held at the trough between cycles
        std::string waveform;  // empty = built-in cosine
        bool Enabled() const { return rate > 0.0 || depth > 0 || pause > 0.0 || !waveform.empty(); }
        bool operator==(const Pulse&) const = default;
    };

    struct Effect {
        std::string key;  // "<plugin>:<effect>"
        std::array<ParamValue, 5> params{ std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 } };
        bool operator==(const Effect&) const = default;
    };

    struct Slot {
        std::string name;
        std::string packId;  // "" inherits Default (tiers) / no texture (Default); "<none>" hides
        std::string entryId;
        int op = 0;  // 0 = all conditions (AND), 1 = any (OR)
        std::vector<ConditionItem> conditions;
        std::int64_t persistMin = 0;
        std::int64_t allowOverride = 1;
        std::int64_t coolMin = 0;
        Pulse pulse;
        std::array<Layer, kLayerCount> layers;
        std::vector<Effect> effects;

        bool Configured() const { return !conditions.empty() && !conditions.front().pluginId.empty(); }
        bool operator==(const Slot&) const = default;
    };

    struct FadeOnDeath {
        bool enabled = false;
        int mode = 0;  // 0 overlay alpha to 0, 1 emissive to 0, 2 inverted (dip then recover)
        std::int64_t durationMs = 2000;
        bool operator==(const FadeOnDeath&) const = default;
    };

    // Typed view of a preset document. The original JSON is kept so keys this UI does not
    // know about (future MTF fields, extra backend slots) survive a save.
    struct Preset {
        std::string name;  // file stem; MTF's identifier
        std::string displayName;
        int hidden = 0;  // 1 = deleted tombstone
        int schemaVersion = kSchemaVersion;
        double transitionDuration = 1.0;
        FadeOnDeath fade;
        std::vector<Slot> slots;  // at least kSlotCount entries
        Json raw = Json::object();

        bool operator==(const Preset& o) const {
            return name == o.name && displayName == o.displayName && hidden == o.hidden && transitionDuration == o.transitionDuration &&
                   fade == o.fade && slots == o.slots;
        }
    };

    std::uint32_t ParseHexColor(const Json& j, std::uint32_t fallback);
    std::string FormatHexColor(std::uint32_t rgb);

    // MTF's _sanitizePresetName: [A-Za-z0-9_-] kept, everything else becomes '_', max 32 chars.
    std::string SanitizePresetName(const std::string& raw);

    Preset PresetFromJson(const std::string& name, const Json& j);
    // A preset document pasted as text: a JSON object with a "slot" array. nullopt with error otherwise.
    std::optional<Json> ParsePresetText(const std::string& text, std::string* error);
    // Merges the typed fields back into preset.raw and returns the document SavePreset would write.
    Json PresetToJson(const Preset& preset);

    Slot DefaultSlot();
    Preset NewPreset(const std::string& name, const std::string& displayName);

    // Effective pack/entry after the Default slot's inheritance is applied.
    struct ResolvedVisual {
        std::string packId;
        std::string entryId;
        bool none = false;
        bool inherited = false;
    };
    ResolvedVisual ResolveVisual(const Preset& preset, int slot);

    // Moves tier from to position to, shifting the ones between. Default (0) never moves. False when nothing moved.
    bool MoveTier(Preset& preset, int from, int to);

    // Gives dst the tattoo, layers and pulse of src, where srcVisual is src's resolved tattoo. A tier that inherited
    // Default's tattoo still inherits when pasted onto another tier; pasted onto Default, it gets the tattoo it showed.
    void SetLook(Slot& dst, bool dstIsDefault, const Slot& src, const ResolvedVisual& srcVisual);
    void CopyLook(Preset& preset, int from, int to);
}
