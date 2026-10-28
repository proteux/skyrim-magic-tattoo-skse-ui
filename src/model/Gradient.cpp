#include "model/Gradient.h"

#include "io/Translations.h"
#include "io/UiSettings.h"
#include "model/Describe.h"

#include <algorithm>
#include <cmath>

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

        std::vector<Slot> slots;
        slots.push_back(base);
        slots[0].layers = look(plan.defaultT);
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
            s.pulse = base.pulse;
            slots.push_back(std::move(s));
        }
        while (static_cast<int>(slots.size()) < kSlotCount) {
            slots.push_back(DefaultSlot());
        }
        preset.slots = std::move(slots);
        TrimTrailingEmptySlots(preset);
    }
}
