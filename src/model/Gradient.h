#pragma once

#include "model/Catalogs.h"
#include "model/Preset.h"

#include <array>
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

    // An effect every tier gets. Slider parameters go from low at the low end of the range to high at the high end;
    // the others take low's value.
    struct EffectRamp {
        std::string key;  // "<plugin>:<effect>"
        std::array<ParamValue, 5> low{ std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 } };
        std::array<ParamValue, 5> high{ std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 }, std::int64_t{ 0 } };
        bool operator==(const EffectRamp&) const = default;
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
        // The pulse only scales emission, so these need some glow to show.
        std::optional<Ramp> pulseRate;  // cycles per second
        std::optional<Ramp> pulseDepth;  // percent
        std::optional<Ramp> pulsePause;  // seconds
        std::optional<std::string> lowWaveform;  // for bands at or below lowWaveformUpTo; "" = built-in cosine
        double lowWaveformUpTo = 0.25;
        std::vector<EffectRamp> effects;
        bool effectsOnDefault = true;
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

    // Slider values are rounded to the parameter's step. An effect that isn't installed keeps low's values.
    Effect SampleEffect(const Catalogs& cat, const EffectRamp& ramp, double t);

    bool IsBelowCondition(const CatalogEntry& entry);

    struct GradientCondition {
        std::string key;  // "<plugin>:<condition>"
        const PluginCatalog* plugin = nullptr;
        const CatalogEntry* entry = nullptr;
    };
    // Conditions with a slider threshold, Attributes (health, magicka, stamina) first.
    std::vector<GradientCondition> GradientConditions(const Catalogs& cat);

    // Replaces every tier with the gradient. Default keeps its tattoo and effects and takes the color of the band
    // no tier covers; tiers inherit Default's tattoo, layers and pulse with the colors and ramps applied. Default
    // gets the ramps too, at its end of the range. With effectsOnDefault, each ramped effect replaces Default's first
    // effect of the same kind, or is added.
    void ApplyGradient(const Catalogs& cat, Preset& preset, GradientSpec spec, int maxSlots);

    // What the Gradient tiers window is set to. Options that are switched off keep their values, so a preset's
    // window reopens the way it was left.
    struct GradientSettings {
        std::string conditionId;
        bool below = true;
        int lo = 0;
        int hi = 100;
        int steps = 10;
        bool tint = true;
        std::vector<std::uint32_t> tintColors{ 0xE02020, 0xF0D020, 0x30C040 };  // low end of the range first
        bool emissive = true;
        std::vector<std::uint32_t> glowColors{ 0xE02020, 0xF0D020, 0x30C040 };
        bool glow = true;
        float glowLow = 3.0f;
        float glowHigh = 1.0f;
        bool fade = false;
        int alphaLow = 100;
        int alphaHigh = 100;
        bool pulseRate = false;
        float rateLow = 3.0f;
        float rateHigh = 0.5f;
        bool pulseDepth = false;
        int depthLow = 80;
        int depthHigh = 20;
        bool pulsePause = false;
        float pauseLow = 0.0f;
        float pauseHigh = 1.0f;
        bool lowWave = false;
        std::string lowWaveform = "heartbeat";
        int lowWaveUpTo = 25;  // percent of the range
        std::vector<EffectRamp> effects;
        bool effectsOnDefault = true;
        bool operator==(const GradientSettings&) const = default;
    };

    // Colors spread evenly from 0 to 1.
    std::vector<ColorStop> EvenStops(const std::vector<std::uint32_t>& colors);
    GradientSpec SpecFromSettings(const GradientSettings& s);

    // A fingerprint of the tiers (not Default), so the window can tell when they were edited after it made them.
    std::string TiersFingerprint(const Preset& preset);
    // Stored as preset.gradient after Make tiers, with the fingerprint of the tiers it made.
    Json GradientSettingsToJson(const GradientSettings& s, const std::string& fingerprint);
    std::optional<GradientSettings> GradientSettingsFromJson(const Json& j);
    // The preset has stored settings, but its tiers no longer match what they made.
    bool GradientTiersEdited(const Preset& preset);

    // For presets without stored settings (made before they were stored, or saved from MTF's menu): reads them back
    // from tiers that form a ladder on one threshold condition. Colors come from the ends and middle of the range.
    // Effects are read when every tier has the same ones in the same order; their ends are extended from the outer
    // tiers, or taken from Default when it has them.
    std::optional<GradientSettings> InferGradient(const Catalogs& cat, const Preset& preset);
}
