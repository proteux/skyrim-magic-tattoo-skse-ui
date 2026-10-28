#include "model/Describe.h"

#include "io/Translations.h"

namespace mtfui::model {
    std::string SlotLabel(const Preset& preset, int slot) {
        if (slot >= 0 && slot < static_cast<int>(preset.slots.size()) && !preset.slots[slot].name.empty()) {
            return preset.slots[slot].name;
        }
        return slot == 0 ? std::string(T("$MTFUI_Tier_Default")) : TF("$MTFUI_Tier_Numbered", slot);
    }

    std::array<ParamValue, 5> ConditionParams(const ConditionItem& item) {
        return { item.param, item.param2, item.param3, std::int64_t{ 0 }, std::int64_t{ 0 } };
    }

    std::string DescribeCondition(const Catalogs& cat, const ConditionItem& item) {
        const PluginCatalog* owner = nullptr;
        const auto* entry = cat.FindCondition(item.pluginId, &owner);
        if (!entry) {
            return TF("$MTFUI_Describe_NotInstalled", item.pluginId);
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
            return TF("$MTFUI_Describe_NotInstalled", effect.key);
        }
        if (entry->description.empty()) {
            return entry->label;
        }
        return FillDescription(*entry, effect.params);
    }

    std::string DescribeVisual(const Catalogs& cat, const Preset& preset, int slot) {
        const auto r = ResolveVisual(preset, slot);
        if (r.none) {
            return T("$MTFUI_Describe_NoTattoo");
        }
        std::string label = r.entryId;
        if (const auto* e = cat.FindEntry(r.packId, r.entryId)) {
            label = e->label;
        } else {
            label = TF("$MTFUI_Describe_Missing", label);
        }
        return r.inherited ? TF("$MTFUI_Describe_FromDefault", label) : label;
    }

    std::string DescribeSlot(const Catalogs& cat, const Preset& preset, int slot) {
        const Slot& s = preset.slots[slot];
        std::string when;
        if (slot == 0) {
            when = T("$MTFUI_Describe_Otherwise");
        } else if (!s.Configured()) {
            return T("$MTFUI_Describe_Unused");
        } else {
            std::string conditions;
            for (std::size_t i = 0; i < s.conditions.size(); ++i) {
                const auto c = DescribeCondition(cat, s.conditions[i]);
                conditions = i == 0 ? c : TF(s.op == 1 ? "$MTFUI_Describe_JoinOr" : "$MTFUI_Describe_JoinAnd", conditions, c);
            }
            when = TF("$MTFUI_Describe_When", conditions);
        }
        std::string out = TF("$MTFUI_Describe_Slot", when, DescribeVisual(cat, preset, slot));
        if (!ResolveVisual(preset, slot).none) {
            const Layer& layer = s.layers[0];
            if ((layer.tint & 0xFFFFFF) != 0xFFFFFF) {
                out = TF("$MTFUI_Describe_Tinted", out, NearestColorName(cat.colors, layer.tint));
            }
            if (layer.emissiveMult > 0.0) {
                out = TF("$MTFUI_Describe_Glowing", out, NearestColorName(cat.colors, layer.emissive));
            }
        }
        if (s.pulse.Enabled() && s.pulse.rate > 0.0 && s.pulse.depth > 0) {
            out = TF("$MTFUI_Describe_Pulsing", out, s.pulse.rate, s.pulse.depth);
        }
        if (!s.effects.empty()) {
            out = s.effects.size() == 1 ? TF("$MTFUI_Describe_EffectOne", out, s.effects.size()) : TF("$MTFUI_Describe_EffectMany", out, s.effects.size());
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
