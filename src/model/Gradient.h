#pragma once

#include "model/Catalogs.h"
#include "model/Preset.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mtfui::model {
    struct ColorStop {
        double at = 0.0;  // 0 = the low end of the value range, 1 = the high end
        std::uint32_t rgb = 0xFFFFFF;
        bool operator==(const ColorStop&) const = default;
    };

    struct Ramp {
        double low = 0.0;  // value at the low end of the range
        double high = 0.0;
        double At(double t) const { return low + (high - low) * t; }
    };

    // Evenly spaced tiers on one threshold condition. MTF has no value-to-color mapping, so a "health bar" is a
    // ladder of tiers; the preset's transition cross-fades between them.
    struct GradientSpec {
        std::string conditionId;  // "<plugin>:<condition>" whose first parameter is a slider
        bool below = true;  // "below X" conditions: tier 1 holds the lowest values
        std::int64_t lo = 0;
        std::int64_t hi = 100;
        int steps = 10;  // tiers, on top of Default
        std::vector<ColorStop> tintStops;  // empty = keep Default's tint
        std::vector<ColorStop> emissiveStops;  // empty = keep Default's glow color
        std::optional<Ramp> emissiveMult;
        std::optional<Ramp> alpha;  // percent
    };

    struct GradientTier {
        std::int64_t threshold = 0;
        double t = 0.0;  // where its band sits in the range, for colors and ramps
    };

    struct GradientPlan {
        std::vector<GradientTier> tiers;  // in priority order
        double defaultT = 0.0;
    };

    // Splits [lo, hi] into steps + 1 bands; Default takes the band no tier covers. Thresholds that round to the same
    // integer are merged, so a plan can have fewer tiers than asked for.
    GradientPlan PlanGradient(const GradientSpec& spec);

    // Interpolated in OKLab, so red to green passes through yellow rather than brown.
    std::uint32_t SampleGradient(const std::vector<ColorStop>& stops, double t);

    bool IsBelowCondition(const CatalogEntry& entry);

    struct GradientCondition {
        std::string key;  // "<plugin>:<condition>"
        const PluginCatalog* plugin = nullptr;
        const CatalogEntry* entry = nullptr;
    };
    // Conditions with a slider threshold, Attributes (health, magicka, stamina) first.
    std::vector<GradientCondition> GradientConditions(const Catalogs& cat);

    // Replaces every tier with the gradient. Default keeps its tattoo and effects and takes the color of the band
    // no tier covers; tiers inherit Default's tattoo, layers and pulse with the colors and ramps applied.
    void ApplyGradient(const Catalogs& cat, Preset& preset, GradientSpec spec, int maxSlots);
}
