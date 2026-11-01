#include "model/Gradient.h"

#include "io/Translations.h"
#include "io/UiSettings.h"
#include "model/Describe.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <variant>

namespace mtfui::model {
    namespace {
        struct Lab {
            double l, a, b;
        };

        double ToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
        double ToSrgb(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }

        Lab ToOklab(std::uint32_t rgb) {
            const double r = ToLinear(((rgb >> 16) & 0xFF) / 255.0);
            const double g = ToLinear(((rgb >> 8) & 0xFF) / 255.0);
            const double b = ToLinear((rgb & 0xFF) / 255.0);
            const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
            const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
            const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
            return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
        }

        std::uint32_t FromOklab(const Lab& c) {
            const double l = std::pow(c.l + 0.3963377774 * c.a + 0.2158037573 * c.b, 3.0);
            const double m = std::pow(c.l - 0.1055613458 * c.a - 0.0638541728 * c.b, 3.0);
            const double s = std::pow(c.l - 0.0894841775 * c.a - 1.2914855480 * c.b, 3.0);
            const double rgb[3] = { 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s };
            std::uint32_t out = 0;
            for (double v : rgb) {
                const auto byte = static_cast<std::uint32_t>(std::lround(std::clamp(ToSrgb(std::clamp(v, 0.0, 1.0)), 0.0, 1.0) * 255.0));
                out = (out << 8) | byte;
            }
            return out;
        }
    }

    GradientPlan PlanGradient(const GradientSpec& spec) {
        GradientPlan plan;
        const std::int64_t lo = std::min(spec.lo, spec.hi);
        const std::int64_t hi = std::max(spec.lo, spec.hi);
        const int steps = std::max(1, spec.steps);
        std::vector<std::int64_t> bounds;
        for (int k = 1; k <= steps; ++k) {
            const auto b = static_cast<std::int64_t>(std::llround(lo + static_cast<double>(hi - lo) * k / (steps + 1)));
            if (b > lo && b <= hi && (bounds.empty() || b > bounds.back())) {
                bounds.push_back(b);
            }
        }
        const int n = static_cast<int>(bounds.size());
        // Band j is [bound j-1, bound j); band 0 starts at lo and band n ends at hi.
        auto bandT = [n](int j) { return n == 0 ? 0.0 : static_cast<double>(j) / n; };
        for (int k = 0; k < n; ++k) {
            if (spec.below) {
                plan.tiers.push_back({ bounds[k], bandT(k) });
            } else {
                plan.tiers.push_back({ bounds[n - 1 - k], bandT(n - k) });
            }
        }
        plan.defaultT = spec.below ? bandT(n) : 0.0;
        return plan;
    }

    std::uint32_t SampleGradient(const std::vector<ColorStop>& stops, double t) {
        if (stops.empty()) {
            return 0xFFFFFF;
        }
        auto sorted = stops;
        std::stable_sort(sorted.begin(), sorted.end(), [](const ColorStop& a, const ColorStop& b) { return a.at < b.at; });
        if (t <= sorted.front().at) {
            return sorted.front().rgb & 0xFFFFFF;
        }
        if (t >= sorted.back().at) {
            return sorted.back().rgb & 0xFFFFFF;
        }
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            const auto& a = sorted[i - 1];
            const auto& b = sorted[i];
            if (t > b.at) {
                continue;
            }
            const double span = b.at - a.at;
            const double f = span <= 0.0 ? 1.0 : (t - a.at) / span;
            const Lab ca = ToOklab(a.rgb);
            const Lab cb = ToOklab(b.rgb);
            return FromOklab({ ca.l + (cb.l - ca.l) * f, ca.a + (cb.a - ca.a) * f, ca.b + (cb.b - ca.b) * f });
        }
        return sorted.back().rgb & 0xFFFFFF;
    }

    bool IsBelowCondition(const CatalogEntry& entry) {
        const auto text = io::ToLower(entry.id + " " + entry.label + " " + entry.description);
        return text.find("below") != std::string::npos || text.find("under") != std::string::npos || text.find("less than") != std::string::npos;
    }

    std::vector<GradientCondition> GradientConditions(const Catalogs& cat) {
        std::vector<GradientCondition> out;
        for (const auto& plugin : cat.plugins) {
            for (const auto& c : plugin.conditions) {
                const auto& p = c.params[0];
                if (p.kind == ParamKind::Slider && p.max > p.min) {
                    out.push_back({ plugin.pluginId + ":" + c.id, &plugin, &c });
                }
            }
        }
        std::stable_partition(out.begin(), out.end(), [](const GradientCondition& c) { return IEquals(c.plugin->pluginId, "mtf.attributes"); });
        return out;
    }

    void ApplyGradient(const Catalogs& cat, Preset& preset, GradientSpec spec, int maxSlots) {
        if (preset.slots.empty()) {
            preset.slots.push_back(DefaultSlot());
        }
        spec.steps = std::clamp(spec.steps, 1, std::max(1, maxSlots - 1));
        const auto plan = PlanGradient(spec);
        const auto* entry = cat.FindCondition(spec.conditionId);

        Slot& base = preset.slots[0];
        const auto baseLayers = base.layers;
        auto look = [&](double t) {
            auto layers = baseLayers;
            const auto tint = SampleGradient(spec.tintStops, t);
            const auto emissive = SampleGradient(spec.emissiveStops, t);
            for (auto& l : layers) {
                if (!spec.tintStops.empty()) {
                    l.tint = tint;
                }
                if (!spec.emissiveStops.empty()) {
                    l.emissive = emissive;
                }
                if (spec.emissiveMult) {
                    // Stored as a Papyrus float; rounding now keeps the saved preset equal to the edited one.
                    l.emissiveMult = static_cast<float>(std::max(0.0, spec.emissiveMult->At(t)));
                }
                if (spec.alpha) {
                    l.alpha = std::clamp<std::int64_t>(std::llround(spec.alpha->At(t)), 0, 100);
                }
            }
            return layers;
        };
        const auto basePulse = base.pulse;
        auto pulse = [&](double t) {
            auto out = basePulse;
            // Rate and pause are Papyrus floats too.
            if (spec.pulseRate) {
                out.rate = static_cast<float>(std::max(0.0, spec.pulseRate->At(t)));
            }
            if (spec.pulseDepth) {
                out.depth = std::clamp<std::int64_t>(std::llround(spec.pulseDepth->At(t)), 0, 100);
            }
            if (spec.pulsePause) {
                out.pause = static_cast<float>(std::max(0.0, spec.pulsePause->At(t)));
            }
            if (spec.lowWaveform && t <= spec.lowWaveformUpTo + 1e-9) {
                out.waveform = *spec.lowWaveform;
            }
            return out;
        };

        std::vector<Slot> slots;
        slots.push_back(base);
        slots[0].layers = look(plan.defaultT);
        slots[0].pulse = pulse(plan.defaultT);
        for (const auto& tier : plan.tiers) {
            Slot s = DefaultSlot();
            ConditionItem item;
            item.pluginId = spec.conditionId;
            if (entry) {
                ApplyConditionDefaults(*entry, item);
                s.name = TF("$MTFUI_Gradient_TierName", entry->label, entry->params[0].Display(tier.threshold));
            }
            item.param = tier.threshold;
            s.conditions.push_back(std::move(item));
            s.layers = look(tier.t);
            s.pulse = pulse(tier.t);
            slots.push_back(std::move(s));
        }
        while (static_cast<int>(slots.size()) < kSlotCount) {
            slots.push_back(DefaultSlot());
        }
        preset.slots = std::move(slots);
        TrimTrailingEmptySlots(preset);
    }

    std::vector<ColorStop> EvenStops(const std::vector<std::uint32_t>& colors) {
        std::vector<ColorStop> stops;
        const auto n = colors.size();
        for (std::size_t i = 0; i < n; ++i) {
            stops.push_back({ n == 1 ? 0.0 : static_cast<double>(i) / (n - 1), colors[i] });
        }
        return stops;
    }

    namespace {
        Json ColorsToJson(const std::vector<std::uint32_t>& colors) {
            Json out = Json::array();
            for (auto c : colors) {
                out.push_back(FormatHexColor(c));
            }
            return out;
        }

        void ColorsFromJson(const Json& j, std::vector<std::uint32_t>& colors) {
            if (!j.is_array() || j.size() < 2) {
                return;
            }
            std::vector<std::uint32_t> out;
            for (const auto& c : j) {
                if (out.size() < 4) {
                    out.push_back(ParseHexColor(c, 0xFFFFFF));
                }
            }
            colors = std::move(out);
        }

        template <class T>
        void Read(const Json& j, const char* key, T& out) {
            const auto it = j.find(key);
            if (it == j.end()) {
                return;
            }
            if constexpr (std::is_same_v<T, bool>) {
                if (it->is_number()) {
                    out = it->template get<double>() != 0.0;
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (it->is_string()) {
                    out = it->template get<std::string>();
                }
            } else if (it->is_number()) {
                out = static_cast<T>(it->template get<double>());
            }
        }

        std::uint64_t Fnv1a(std::string_view text) {
            std::uint64_t h = 0xCBF29CE484222325ull;
            for (unsigned char c : text) {
                h = (h ^ c) * 0x100000001B3ull;
            }
            return h;
        }

        bool NearColor(std::uint32_t a, std::uint32_t b) {
            for (int shift : { 16, 8, 0 }) {
                if (std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)) > 3) {
                    return false;
                }
            }
            return true;
        }
    }

    GradientSpec SpecFromSettings(const GradientSettings& s) {
        GradientSpec spec;
        spec.conditionId = s.conditionId;
        spec.below = s.below;
        spec.lo = std::min(s.lo, s.hi);
        spec.hi = std::max(s.lo, s.hi);
        spec.steps = s.steps;
        if (s.tint) {
            spec.tintStops = EvenStops(s.tintColors);
        }
        if (s.emissive) {
            spec.emissiveStops = EvenStops(s.glowColors);
        }
        if (s.glow) {
            spec.emissiveMult = Ramp{ s.glowLow, s.glowHigh };
        }
        if (s.fade) {
            spec.alpha = Ramp{ static_cast<double>(s.alphaLow), static_cast<double>(s.alphaHigh) };
        }
        if (s.pulseRate) {
            spec.pulseRate = Ramp{ s.rateLow, s.rateHigh };
        }
        if (s.pulseDepth) {
            spec.pulseDepth = Ramp{ static_cast<double>(s.depthLow), static_cast<double>(s.depthHigh) };
        }
        if (s.pulsePause) {
            spec.pulsePause = Ramp{ s.pauseLow, s.pauseHigh };
        }
        if (s.lowWave) {
            spec.lowWaveform = s.lowWaveform;
            spec.lowWaveformUpTo = s.lowWaveUpTo / 100.0;
        }
        return spec;
    }

    std::string TiersFingerprint(const Preset& preset) {
        // Floats are rounded so a saved and reloaded preset gives the same text.
        std::string text;
        for (std::size_t i = 1; i < preset.slots.size(); ++i) {
            const Slot& s = preset.slots[i];
            text += std::format("|{}:{}:{}:{}:{}", i, s.op, s.packId, s.entryId, s.effects.size());
            for (const auto& c : s.conditions) {
                text += std::format(";{}={},{},{}", c.pluginId, ParamToJson(c.param).dump(), ParamToJson(c.param2).dump(), ParamToJson(c.param3).dump());
            }
            for (const auto& l : s.layers) {
                text += std::format(";{:06X},{:06X},{:.4f},{}", l.tint, l.emissive, l.emissiveMult, l.alpha);
            }
            text += std::format(";{:.4f},{},{:.4f},{}", s.pulse.rate, s.pulse.depth, s.pulse.pause, s.pulse.waveform);
        }
        return std::format("{:016x}", Fnv1a(text));
    }

    Json GradientSettingsToJson(const GradientSettings& s, const std::string& fingerprint) {
        return Json{
            { "condition", s.conditionId },
            { "below", s.below ? 1 : 0 },
            { "lo", s.lo },
            { "hi", s.hi },
            { "steps", s.steps },
            { "tint", s.tint ? 1 : 0 },
            { "tintcolors", ColorsToJson(s.tintColors) },
            { "emissive", s.emissive ? 1 : 0 },
            { "emissivecolors", ColorsToJson(s.glowColors) },
            { "glow", s.glow ? 1 : 0 },
            { "glowlow", s.glowLow },
            { "glowhigh", s.glowHigh },
            { "alpha", s.fade ? 1 : 0 },
            { "alphalow", s.alphaLow },
            { "alphahigh", s.alphaHigh },
            { "rate", s.pulseRate ? 1 : 0 },
            { "ratelow", s.rateLow },
            { "ratehigh", s.rateHigh },
            { "depth", s.pulseDepth ? 1 : 0 },
            { "depthlow", s.depthLow },
            { "depthhigh", s.depthHigh },
            { "pause", s.pulsePause ? 1 : 0 },
            { "pauselow", s.pauseLow },
            { "pausehigh", s.pauseHigh },
            { "lowwave", s.lowWave ? 1 : 0 },
            { "lowwaveform", s.lowWaveform },
            { "lowwaveupto", s.lowWaveUpTo },
            { "tiers", fingerprint },
        };
    }

    std::optional<GradientSettings> GradientSettingsFromJson(const Json& j) {
        if (!j.is_object() || !j.contains("condition") || !j["condition"].is_string()) {
            return std::nullopt;
        }
        GradientSettings s;
        Read(j, "condition", s.conditionId);
        Read(j, "below", s.below);
        Read(j, "lo", s.lo);
        Read(j, "hi", s.hi);
        Read(j, "steps", s.steps);
        Read(j, "tint", s.tint);
        ColorsFromJson(j.value("tintcolors", Json()), s.tintColors);
        Read(j, "emissive", s.emissive);
        ColorsFromJson(j.value("emissivecolors", Json()), s.glowColors);
        Read(j, "glow", s.glow);
        Read(j, "glowlow", s.glowLow);
        Read(j, "glowhigh", s.glowHigh);
        Read(j, "alpha", s.fade);
        Read(j, "alphalow", s.alphaLow);
        Read(j, "alphahigh", s.alphaHigh);
        Read(j, "rate", s.pulseRate);
        Read(j, "ratelow", s.rateLow);
        Read(j, "ratehigh", s.rateHigh);
        Read(j, "depth", s.pulseDepth);
        Read(j, "depthlow", s.depthLow);
        Read(j, "depthhigh", s.depthHigh);
        Read(j, "pause", s.pulsePause);
        Read(j, "pauselow", s.pauseLow);
        Read(j, "pausehigh", s.pauseHigh);
        Read(j, "lowwave", s.lowWave);
        Read(j, "lowwaveform", s.lowWaveform);
        Read(j, "lowwaveupto", s.lowWaveUpTo);
        s.steps = std::max(1, s.steps);
        return s;
    }

    bool GradientTiersEdited(const Preset& preset) {
        if (!preset.gradient.is_object()) {
            return false;
        }
        const auto it = preset.gradient.find("tiers");
        return it == preset.gradient.end() || !it->is_string() || it->get<std::string>() != TiersFingerprint(preset);
    }

    std::optional<GradientSettings> InferGradient(const Catalogs& cat, const Preset& preset) {
        int n = 0;
        while (n + 1 < static_cast<int>(preset.slots.size()) && preset.slots[n + 1].Configured()) {
            ++n;
        }
        for (std::size_t i = n + 1; i < preset.slots.size(); ++i) {
            if (preset.slots[i].Configured()) {
                return std::nullopt;
            }
        }
        if (n < 2) {
            return std::nullopt;
        }
        const std::string id = preset.slots[1].conditions.front().pluginId;
        const auto* entry = cat.FindCondition(id);
        if (!entry || entry->params[0].kind != ParamKind::Slider) {
            return std::nullopt;
        }
        std::vector<std::int64_t> thresholds;
        for (int k = 1; k <= n; ++k) {
            const auto& conds = preset.slots[k].conditions;
            if (conds.size() != 1 || !IEquals(conds.front().pluginId, id) || !std::holds_alternative<std::int64_t>(conds.front().param)) {
                return std::nullopt;
            }
            thresholds.push_back(ParamInt(conds.front().param));
        }
        bool rising = true, falling = true;
        for (int k = 1; k < n; ++k) {
            rising = rising && thresholds[k] > thresholds[k - 1];
            falling = falling && thresholds[k] < thresholds[k - 1];
        }
        if (!rising && !falling) {
            return std::nullopt;
        }

        GradientSettings s;
        s.conditionId = id;
        s.below = rising;
        s.steps = n;
        const auto [first, last] = std::minmax(thresholds.front(), thresholds.back());
        const double gap = static_cast<double>(last - first) / (n - 1);
        const auto& param = entry->params[0];
        s.lo = static_cast<int>(std::clamp<std::int64_t>(std::llround(first - gap), param.min, param.max));
        s.hi = static_cast<int>(std::clamp<std::int64_t>(std::llround(last + gap), param.min, param.max));

        // The slot holding the band at t: tier k sits at (k - 1) / n when below, (n - k + 1) / n when above.
        auto at = [&](double t) -> const Slot& {
            const auto j = static_cast<int>(std::lround(t * n));
            const int k = s.below ? j + 1 : n - j + 1;
            return preset.slots[k >= 1 && k <= n ? k : 0];
        };
        auto varies = [&](auto get) {
            for (int k = 1; k <= n; ++k) {
                if (get(preset.slots[k]) != get(preset.slots[0])) {
                    return true;
                }
            }
            return false;
        };
        auto colors = [&](auto get) {
            const auto low = get(at(0.0));
            const auto mid = get(at(0.5));
            const auto high = get(at(1.0));
            if (NearColor(SampleGradient({ { 0.0, low }, { 1.0, high } }, 0.5), mid)) {
                return std::vector<std::uint32_t>{ low, high };
            }
            return std::vector<std::uint32_t>{ low, mid, high };
        };

        auto tint = [](const Slot& x) { return x.layers[0].tint; };
        auto glowColor = [](const Slot& x) { return x.layers[0].emissive; };
        auto mult = [](const Slot& x) { return x.layers[0].emissiveMult; };
        auto alpha = [](const Slot& x) { return x.layers[0].alpha; };
        auto rate = [](const Slot& x) { return x.pulse.rate; };
        auto depth = [](const Slot& x) { return x.pulse.depth; };
        auto pause = [](const Slot& x) { return x.pulse.pause; };
        auto wave = [](const Slot& x) { return x.pulse.waveform; };

        s.tint = varies(tint);
        if (s.tint) {
            s.tintColors = colors(tint);
        }
        s.emissive = varies(glowColor);
        if (s.emissive) {
            s.glowColors = colors(glowColor);
        }
        s.glow = varies(mult);
        if (s.glow) {
            s.glowLow = static_cast<float>(mult(at(0.0)));
            s.glowHigh = static_cast<float>(mult(at(1.0)));
        }
        s.fade = varies(alpha);
        if (s.fade) {
            s.alphaLow = static_cast<int>(alpha(at(0.0)));
            s.alphaHigh = static_cast<int>(alpha(at(1.0)));
        }
        s.pulseRate = varies(rate);
        if (s.pulseRate) {
            s.rateLow = static_cast<float>(rate(at(0.0)));
            s.rateHigh = static_cast<float>(rate(at(1.0)));
        }
        s.pulseDepth = varies(depth);
        if (s.pulseDepth) {
            s.depthLow = static_cast<int>(depth(at(0.0)));
            s.depthHigh = static_cast<int>(depth(at(1.0)));
        }
        s.pulsePause = varies(pause);
        if (s.pulsePause) {
            s.pauseLow = static_cast<float>(pause(at(0.0)));
            s.pauseHigh = static_cast<float>(pause(at(1.0)));
        }
        s.lowWave = varies(wave);
        if (s.lowWave) {
            s.lowWaveform = wave(at(0.0));
            double upTo = 0.0;
            for (int k = 1; k <= n; ++k) {
                const double t = s.below ? static_cast<double>(k - 1) / n : static_cast<double>(n - k + 1) / n;
                if (preset.slots[k].pulse.waveform == s.lowWaveform) {
                    upTo = std::max(upTo, t);
                }
            }
            s.lowWaveUpTo = static_cast<int>(std::lround(upTo * 100.0));
        }
        return s;
    }
}
