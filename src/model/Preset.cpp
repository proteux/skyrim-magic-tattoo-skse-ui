#include "model/Preset.h"

#include <algorithm>
#include <cstdio>

namespace mtfui::model {
    namespace {
        std::string GetStr(const Json& j, const char* key, const std::string& def = {}) {
            if (!j.is_object()) {
                return def;
            }
            auto it = j.find(key);
            if (it == j.end()) {
                return def;
            }
            if (it->is_string()) {
                return it->get<std::string>();
            }
            if (it->is_number_integer()) {
                return std::to_string(it->get<std::int64_t>());
            }
            return def;
        }

        std::int64_t GetInt(const Json& j, const char* key, std::int64_t def = 0) {
            if (!j.is_object()) {
                return def;
            }
            auto it = j.find(key);
            if (it == j.end()) {
                return def;
            }
            if (it->is_number_integer() || it->is_number_unsigned()) {
                return it->get<std::int64_t>();
            }
            if (it->is_number_float()) {
                return static_cast<std::int64_t>(it->get<double>());
            }
            if (it->is_boolean()) {
                return it->get<bool>() ? 1 : 0;
            }
            if (it->is_string()) {
                try {
                    return std::stoll(it->get<std::string>());
                } catch (...) {
                    return def;
                }
            }
            return def;
        }

        double GetFloat(const Json& j, const char* key, double def = 0.0) {
            if (!j.is_object()) {
                return def;
            }
            auto it = j.find(key);
            if (it == j.end()) {
                return def;
            }
            if (it->is_number()) {
                return it->get<double>();
            }
            return def;
        }

        const Json& Child(const Json& j, const char* key) {
            static const Json kNull;
            if (!j.is_object()) {
                return kNull;
            }
            auto it = j.find(key);
            return it == j.end() ? kNull : *it;
        }

        Json& ObjectAt(Json& parent, const char* key) {
            Json& c = parent[key];
            if (!c.is_object()) {
                c = Json::object();
            }
            return c;
        }

        // JsonUtil stores Papyrus floats, so values go through single precision before being widened.
        Json PapyrusFloat(double v) {
            return Json(static_cast<double>(static_cast<float>(v)));
        }

        Slot SlotFromJson(const Json& s) {
            Slot slot;
            const Json& cond = Child(s, "cond");
            slot.packId = GetStr(cond, "packid");
            slot.entryId = GetStr(cond, "entryid");
            slot.op = static_cast<int>(GetInt(cond, "op", 0));
            if (const Json& items = Child(cond, "items"); items.is_array()) {
                for (const auto& it : items) {
                    ConditionItem c;
                    c.pluginId = GetStr(it, "pluginid");
                    if (it.is_object()) {
                        if (auto p = it.find("param"); p != it.end()) {
                            c.param = ParamFromJson(*p);
                        }
                        if (auto p = it.find("param2"); p != it.end()) {
                            c.param2 = ParamFromJson(*p);
                        }
                        if (auto p = it.find("param3"); p != it.end()) {
                            c.param3 = ParamFromJson(*p);
                        }
                    }
                    if (!c.pluginId.empty()) {
                        slot.conditions.push_back(std::move(c));
                    }
                }
            }
            slot.name = GetStr(s, "name");
            slot.persistMin = GetInt(Child(s, "persist"), "min", 0);
            slot.allowOverride = GetInt(Child(s, "persist"), "allowOverride", 1);
            slot.coolMin = GetInt(Child(s, "cool"), "min", 0);
            const Json& pulse = Child(s, "pulse");
            slot.pulse.rate = GetFloat(pulse, "rate");
            slot.pulse.depth = GetInt(pulse, "depth");
            slot.pulse.pause = GetFloat(pulse, "pause");
            slot.pulse.waveform = GetStr(pulse, "waveform");
            if (const Json& layers = Child(s, "layer"); layers.is_array()) {
                for (std::size_t i = 0; i < layers.size() && i < kLayerCount; ++i) {
                    const Json& l = layers[i];
                    Layer& out = slot.layers[i];
                    out.tint = ParseHexColor(Child(l, "tint"), 0xFFFFFF);
                    out.emissive = ParseHexColor(Child(l, "emissive"), 0xFFFFFF);
                    out.emissiveMult = GetFloat(l, "emissivemult", 0.0);
                    out.alpha = GetInt(l, "alpha", 100);
                }
            }
            if (const Json& effects = Child(s, "effect"); effects.is_array()) {
                for (const auto& e : effects) {
                    Effect fx;
                    fx.key = GetStr(e, "key");
                    if (fx.key.empty()) {
                        continue;
                    }
                    for (int n = 0; n < 5; ++n) {
                        const std::string k = "param" + std::to_string(n + 1);
                        if (auto p = e.find(k); p != e.end()) {
                            fx.params[n] = ParamFromJson(*p);
                        }
                    }
                    slot.effects.push_back(std::move(fx));
                }
            }
            return slot;
        }

        void SlotToJson(const Slot& slot, Json& s) {
            if (!s.is_object()) {
                s = Json::object();
            }
            Json& cond = ObjectAt(s, "cond");
            std::vector<const ConditionItem*> items;
            for (const auto& c : slot.conditions) {
                if (!c.pluginId.empty()) {
                    items.push_back(&c);
                }
            }
            if (items.empty()) {
                cond.erase("op");
                cond.erase("items");
            } else {
                cond["op"] = slot.op;
                Json arr = Json::array();
                for (const auto* c : items) {
                    Json it = Json::object();
                    it["pluginid"] = c->pluginId;
                    it["param"] = ParamToJson(c->param);
                    it["param2"] = ParamToJson(c->param2);
                    it["param3"] = ParamToJson(c->param3);
                    arr.push_back(std::move(it));
                }
                cond["items"] = std::move(arr);
            }
            cond["packid"] = slot.packId;
            cond["entryid"] = slot.entryId;

            if (slot.name.empty()) {
                s.erase("name");
            } else {
                s["name"] = slot.name;
            }
            Json& persist = ObjectAt(s, "persist");
            persist["min"] = slot.persistMin;
            persist["allowOverride"] = slot.allowOverride;
            ObjectAt(s, "cool")["min"] = slot.coolMin;

            if (slot.pulse.Enabled()) {
                Json& pulse = ObjectAt(s, "pulse");
                pulse["rate"] = PapyrusFloat(slot.pulse.rate);
                pulse["depth"] = slot.pulse.depth;
                if (slot.pulse.pause > 0.0) {
                    pulse["pause"] = PapyrusFloat(slot.pulse.pause);
                } else {
                    pulse.erase("pause");
                }
                if (slot.pulse.waveform.empty()) {
                    pulse.erase("waveform");
                } else {
                    pulse["waveform"] = slot.pulse.waveform;
                }
            } else {
                s.erase("pulse");
            }

            Json& layers = s["layer"];
            if (!layers.is_array()) {
                layers = Json::array();
            }
            while (layers.size() < kLayerCount) {
                layers.push_back(Json::object());
            }
            for (int i = 0; i < kLayerCount; ++i) {
                Json& l = layers[i];
                if (!l.is_object()) {
                    l = Json::object();
                }
                const Layer& in = slot.layers[i];
                l["tint"] = FormatHexColor(in.tint);
                l["emissive"] = FormatHexColor(in.emissive);
                l["emissivemult"] = PapyrusFloat(in.emissiveMult);
                l["alpha"] = in.alpha;
            }

            Json effects = Json::array();
            for (const auto& fx : slot.effects) {
                if (fx.key.empty()) {
                    continue;
                }
                Json e = Json::object();
                e["key"] = fx.key;
                for (int n = 0; n < 5; ++n) {
                    const auto& p = fx.params[n];
                    const std::string k = "param" + std::to_string(n + 1);
                    if (std::holds_alternative<std::string>(p)) {
                        if (!std::get<std::string>(p).empty()) {
                            e[k] = std::get<std::string>(p);
                        }
                    } else {
                        e[k] = std::get<std::int64_t>(p);
                    }
                }
                effects.push_back(std::move(e));
            }
            if (effects.empty()) {
                s.erase("effect");
            } else {
                s["effect"] = std::move(effects);
            }
        }
    }

    std::int64_t ParamInt(const ParamValue& v) {
        if (const auto* i = std::get_if<std::int64_t>(&v)) {
            return *i;
        }
        try {
            return std::stoll(std::get<std::string>(v));
        } catch (...) {
            return 0;
        }
    }

    std::string ParamStr(const ParamValue& v) {
        if (const auto* s = std::get_if<std::string>(&v)) {
            return *s;
        }
        return std::to_string(std::get<std::int64_t>(v));
    }

    ParamValue ParamFromJson(const Json& j) {
        if (j.is_string()) {
            return j.get<std::string>();
        }
        if (j.is_number_float()) {
            return static_cast<std::int64_t>(j.get<double>());
        }
        if (j.is_number()) {
            return j.get<std::int64_t>();
        }
        if (j.is_boolean()) {
            return std::int64_t{ j.get<bool>() ? 1 : 0 };
        }
        return std::int64_t{ 0 };
    }

    Json ParamToJson(const ParamValue& v) {
        if (const auto* s = std::get_if<std::string>(&v)) {
            return *s;
        }
        return std::get<std::int64_t>(v);
    }

    std::uint32_t ParseHexColor(const Json& j, std::uint32_t fallback) {
        if (j.is_number()) {
            return static_cast<std::uint32_t>(std::clamp<std::int64_t>(j.get<std::int64_t>(), 0, 0xFFFFFF));
        }
        if (!j.is_string()) {
            return fallback;
        }
        std::string s = j.get<std::string>();
        if (!s.empty() && s[0] == '#') {
            s.erase(0, 1);
        } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s.erase(0, 2);
        }
        if (s.empty() || s.size() > 6) {
            return fallback;
        }
        try {
            return static_cast<std::uint32_t>(std::stoul(s, nullptr, 16)) & 0xFFFFFF;
        } catch (...) {
            return fallback;
        }
    }

    std::string FormatHexColor(std::uint32_t rgb) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
        return buf;
    }

    std::string SanitizePresetName(const std::string& raw) {
        std::string out;
        for (std::size_t i = 0; i < raw.size() && out.size() < 32; ++i) {
            const unsigned char c = static_cast<unsigned char>(raw[i]);
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            out += ok ? static_cast<char>(c) : '_';
        }
        return out;
    }

    Preset PresetFromJson(const std::string& name, const Json& j) {
        Preset p;
        p.name = name;
        p.raw = j.is_object() ? j : Json::object();
        p.displayName = GetStr(j, "displayname", name);
        p.hidden = static_cast<int>(GetInt(j, "hidden", 0));
        p.schemaVersion = static_cast<int>(GetInt(j, "schemaversion", 1));
        p.transitionDuration = GetFloat(Child(j, "transition"), "duration", 1.0);
        const Json& fade = Child(j, "fadeondeath");
        p.fade.enabled = GetInt(fade, "enabled", 0) > 0;
        p.fade.mode = static_cast<int>(GetInt(fade, "mode", 0));
        if (p.fade.mode < 0 || p.fade.mode > 2) {
            p.fade.mode = 0;
        }
        p.fade.durationMs = GetInt(fade, "durationms", 2000);
        if (p.fade.durationMs < 1) {
            p.fade.durationMs = 2000;
        }
        if (const Json& slots = Child(j, "slot"); slots.is_array()) {
            for (const auto& s : slots) {
                p.slots.push_back(SlotFromJson(s));
            }
        }
        while (p.slots.size() < kSlotCount) {
            p.slots.push_back(DefaultSlot());
        }
        return p;
    }

    std::optional<Json> ParsePresetText(const std::string& text, std::string* error) {
        auto fail = [error](std::string why) -> std::optional<Json> {
            if (error) {
                *error = std::move(why);
            }
            return std::nullopt;
        };
        Json j = Json::parse(text, nullptr, false);
        if (j.is_discarded()) {
            return fail("the clipboard does not hold JSON");
        }
        if (!j.is_object() || !j.contains("slot") || !j["slot"].is_array()) {
            return fail("the clipboard JSON is not a Magic Tattoos preset");
        }
        return j;
    }

    Json PresetToJson(const Preset& preset) {
        Json j = preset.raw.is_object() ? preset.raw : Json::object();
        j["hidden"] = 0;
        j["displayname"] = preset.displayName;
        j["schemaversion"] = kSchemaVersion;
        ObjectAt(j, "transition")["duration"] = PapyrusFloat(preset.transitionDuration);
        Json& fade = ObjectAt(j, "fadeondeath");
        fade["enabled"] = preset.fade.enabled ? 1 : 0;
        fade["mode"] = preset.fade.mode;
        fade["durationms"] = preset.fade.durationMs;

        Json& slots = j["slot"];
        if (!slots.is_array()) {
            slots = Json::array();
        }
        if (slots.size() > preset.slots.size()) {
            slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(preset.slots.size()), slots.end());
        }
        while (slots.size() < preset.slots.size()) {
            slots.push_back(Json::object());
        }
        for (std::size_t i = 0; i < preset.slots.size(); ++i) {
            SlotToJson(preset.slots[i], slots[i]);
        }
        return j;
    }

    Slot DefaultSlot() {
        Slot s;
        return s;
    }

    Preset NewPreset(const std::string& name, const std::string& displayName) {
        Preset p;
        p.name = name;
        p.displayName = displayName;
        p.transitionDuration = 1.0;
        for (int i = 0; i < kSlotCount; ++i) {
            p.slots.push_back(DefaultSlot());
        }
        p.slots[0].name.clear();
        return p;
    }

    ResolvedVisual ResolveVisual(const Preset& preset, int slot) {
        ResolvedVisual r;
        if (slot < 0 || slot >= static_cast<int>(preset.slots.size())) {
            r.none = true;
            return r;
        }
        const Slot& s = preset.slots[slot];
        if (slot > 0 && s.packId.empty()) {
            r.inherited = true;
            r.packId = preset.slots[0].packId;
            r.entryId = preset.slots[0].entryId;
        } else {
            r.packId = s.packId;
            r.entryId = s.entryId;
        }
        r.none = r.packId.empty() || r.packId == kNoTexture || r.entryId.empty();
        return r;
    }

    bool MoveTier(Preset& preset, int from, int to) {
        const int count = static_cast<int>(preset.slots.size());
        if (from == to || from < 1 || to < 1 || from >= count || to >= count) {
            return false;
        }
        auto slot = std::move(preset.slots[from]);
        preset.slots.erase(preset.slots.begin() + from);
        preset.slots.insert(preset.slots.begin() + to, std::move(slot));
        return true;
    }

    void SetLook(Slot& dst, bool dstIsDefault, const Slot& src, const ResolvedVisual& srcVisual) {
        dst.layers = src.layers;
        dst.pulse = src.pulse;
        if (srcVisual.inherited && !dstIsDefault) {
            dst.packId.clear();
            dst.entryId.clear();
        } else if (srcVisual.none) {
            dst.packId = dstIsDefault ? "" : kNoTexture;
            dst.entryId.clear();
        } else {
            dst.packId = srcVisual.packId;
            dst.entryId = srcVisual.entryId;
        }
    }

    void CopyLook(Preset& preset, int from, int to) {
        const int count = static_cast<int>(preset.slots.size());
        if (from == to || from < 0 || to < 0 || from >= count || to >= count) {
            return;
        }
        const auto vis = ResolveVisual(preset, from);
        const Slot src = preset.slots[from];
        SetLook(preset.slots[to], to == 0, src, vis);
    }
}
