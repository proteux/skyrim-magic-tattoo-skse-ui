#include "model/Describe.h"

#include <cstdio>

namespace mtfui::model {
    std::string SlotLabel(const Preset& preset, int slot) {
        if (slot >= 0 && slot < static_cast<int>(preset.slots.size()) && !preset.slots[slot].name.empty()) {
            return preset.slots[slot].name;
        }
        return slot == 0 ? "Default" : "Tier " + std::to_string(slot);
    }

    std::array<ParamValue, 5> ConditionParams(const ConditionItem& item) {
        return { item.param, item.param2, item.param3, std::int64_t{ 0 }, std::int64_t{ 0 } };
    }

    std::string DescribeCondition(const Catalogs& cat, const ConditionItem& item) {
        const PluginCatalog* owner = nullptr;
        const auto* entry = cat.FindCondition(item.pluginId, &owner);
        if (!entry) {
            return item.pluginId + " (plugin not installed)";
        }
        if (entry->description.empty()) {
            std::string s = entry->label;
            if (entry->params[0].kind != ParamKind::None) {
                s += " " + entry->params[0].Display(item.param);
            }
            return s;
        }
        return FillDescription(*entry, ConditionParams(item));
    }

    std::string DescribeEffect(const Catalogs& cat, const Effect& effect) {
        const auto* entry = cat.FindEffect(effect.key);
        if (!entry) {
            return effect.key + " (plugin not installed)";
        }
        if (entry->description.empty()) {
            return entry->label;
        }
        return FillDescription(*entry, effect.params);
    }

    std::string DescribeVisual(const Catalogs& cat, const Preset& preset, int slot) {
        const auto r = ResolveVisual(preset, slot);
        if (r.none) {
            return "no tattoo";
        }
        std::string label = r.entryId;
        if (const auto* e = cat.FindEntry(r.packId, r.entryId)) {
            label = e->label;
        } else {
            label += " (missing)";
        }
        return r.inherited ? label + " (from Default)" : label;
    }

    std::string DescribeSlot(const Catalogs& cat, const Preset& preset, int slot) {
        const Slot& s = preset.slots[slot];
        std::string out;
        if (slot == 0) {
            out = "Otherwise";
        } else if (!s.Configured()) {
            return "Unused";
        } else {
            out = "When the wearer ";
            for (std::size_t i = 0; i < s.conditions.size(); ++i) {
                if (i > 0) {
                    out += s.op == 1 ? ", or " : ", and ";
                }
                out += DescribeCondition(cat, s.conditions[i]);
            }
        }
        out += ": " + DescribeVisual(cat, preset, slot);
        if (!ResolveVisual(preset, slot).none) {
            const Layer& layer = s.layers[0];
            if ((layer.tint & 0xFFFFFF) != 0xFFFFFF) {
                out += ", tinted " + NearestColorName(cat.colors, layer.tint);
            }
            if (layer.emissiveMult > 0.0) {
                out += ", glowing " + NearestColorName(cat.colors, layer.emissive);
            }
        }
        if (s.pulse.Enabled() && s.pulse.rate > 0.0 && s.pulse.depth > 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), ", pulsing %.2g/s at %lld%%", s.pulse.rate, static_cast<long long>(s.pulse.depth));
            out += buf;
        }
        if (!s.effects.empty()) {
            out += ", " + std::to_string(s.effects.size()) + (s.effects.size() == 1 ? " effect" : " effects");
        }
        return out;
    }

    void ApplyConditionDefaults(const CatalogEntry& entry, ConditionItem& item) {
        ParamValue* slots[] = { &item.param, &item.param2, &item.param3 };
        for (int i = 0; i < 3; ++i) {
            *slots[i] = entry.params[i].kind == ParamKind::None ? ParamValue{ std::int64_t{ 0 } } : entry.params[i].def;
        }
    }

    void ApplyEffectDefaults(const CatalogEntry& entry, Effect& effect) {
        for (int i = 0; i < 5; ++i) {
            effect.params[i] = entry.params[i].kind == ParamKind::None ? ParamValue{ std::int64_t{ 0 } } : entry.params[i].def;
        }
    }
}
