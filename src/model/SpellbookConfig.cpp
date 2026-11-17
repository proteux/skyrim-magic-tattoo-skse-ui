#include "model/SpellbookConfig.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace mtfui::model {
    namespace {
        using Json = nlohmann::json;

        // PapyrusUtil keeps each type in its own object and matches keys case-insensitively; these are lowercase.
        std::string SlotKey(int slot, const char* field) { return std::format("slot{}.{}", slot, field); }

        const Json* Section(const Json& j, const char* type) {
            auto it = j.find(type);
            return it != j.end() && it->is_object() ? &*it : nullptr;
        }

        void GetString(const Json& j, const std::string& key, std::string& out) {
            if (const auto* s = Section(j, "string")) {
                if (auto it = s->find(key); it != s->end() && it->is_string()) {
                    out = it->get<std::string>();
                }
            }
        }

        bool GetInt(const Json& j, const std::string& key, int& out) {
            if (const auto* s = Section(j, "int")) {
                if (auto it = s->find(key); it != s->end() && it->is_number()) {
                    const double v = it->get<double>();
                    if (std::isfinite(v) && std::abs(v) < 1e9) {
                        out = static_cast<int>(v);
                        return true;
                    }
                }
            }
            return false;
        }

        void GetFloat(const Json& j, const std::string& key, double& out) {
            if (const auto* s = Section(j, "float")) {
                if (auto it = s->find(key); it != s->end() && it->is_number()) {
                    const double v = it->get<double>();
                    if (std::isfinite(v)) {
                        out = v;
                    }
                }
            }
        }

        void GetBool(const Json& j, const std::string& key, bool& out) {
            int v = 0;
            if (GetInt(j, key, v)) {
                out = v != 0;
            }
        }

        void GetCast(const Json& j, const std::string& key, CastType& out) {
            int v = 0;
            if (GetInt(j, key, v) && v >= 0 && v < kCastTypeCount) {
                out = static_cast<CastType>(v);
            }
        }

        void GetSchool(const Json& j, const std::string& key, School& out) {
            int v = 0;
            if (GetInt(j, key, v) && v >= 0 && v < kSchoolCount) {
                out = static_cast<School>(v);
            }
        }

        void GetCost(const Json& j, const std::string& key, int& out) {
            if (GetInt(j, key, out)) {
                out = std::clamp(out, 0, kSpellbookCostMax);
            }
        }
    }

    bool IsChanneled(CastType cast) { return cast == CastType::SelfChannel || cast == CastType::Touch; }

    int DispelVersion(CastType cast) {
        switch (cast) {
        case CastType::Touch:
            return 1;
        case CastType::Ranged:
            return 2;
        default:
            return 0;
        }
    }

    int DuplicatePresetSlot(const SpellbookConfig& config, int slot) {
        if (slot < 0 || slot >= kSpellbookSlots) {
            return -1;
        }
        const auto& preset = config.slots[slot].preset;
        if (preset.empty()) {
            return -1;
        }
        for (int i = 0; i < kSpellbookSlots; ++i) {
            if (i != slot && config.slots[i].preset == preset) {
                return i;
            }
        }
        return -1;
    }

    SpellbookConfig SpellbookFromJson(const Json& j) {
        SpellbookConfig c;
        if (!j.is_object()) {
            return c;
        }
        for (int i = 0; i < kSpellbookSlots; ++i) {
            auto& s = c.slots[i];
            GetString(j, SlotKey(i, "name"), s.name);
            GetString(j, SlotKey(i, "preset"), s.preset);
            GetCast(j, SlotKey(i, "cast"), s.cast);
            GetBool(j, SlotKey(i, "hostile"), s.hostile);
            GetCost(j, SlotKey(i, "cost"), s.cost);
            GetSchool(j, SlotKey(i, "school"), s.school);
            GetFloat(j, SlotKey(i, "decay"), s.decay);
            s.decay = std::clamp(s.decay, kDecayMin, kDecayMax);
            GetBool(j, SlotKey(i, "permanent"), s.permanent);
        }
        auto& d = c.dispel;
        GetString(j, "dispel.name", d.name);
        GetCast(j, "dispel.cast", d.cast);
        if (d.cast == CastType::SelfChannel) {
            d.cast = CastType::SelfInstant;
        }
        GetCost(j, "dispel.cost", d.cost);
        GetSchool(j, "dispel.school", d.school);
        return c;
    }

    Json SpellbookToJson(const SpellbookConfig& c) {
        Json ints = Json::object();
        Json floats = Json::object();
        Json strings = Json::object();
        for (int i = 0; i < kSpellbookSlots; ++i) {
            const auto& s = c.slots[i];
            strings[SlotKey(i, "name")] = s.name;
            strings[SlotKey(i, "preset")] = s.preset;
            ints[SlotKey(i, "cast")] = static_cast<int>(s.cast);
            ints[SlotKey(i, "hostile")] = s.hostile ? 1 : 0;
            ints[SlotKey(i, "cost")] = s.cost;
            ints[SlotKey(i, "school")] = static_cast<int>(s.school);
            floats[SlotKey(i, "decay")] = s.decay;
            ints[SlotKey(i, "permanent")] = s.permanent ? 1 : 0;
        }
        strings["dispel.name"] = c.dispel.name;
        ints["dispel.cast"] = static_cast<int>(c.dispel.cast);
        ints["dispel.cost"] = c.dispel.cost;
        ints["dispel.school"] = static_cast<int>(c.dispel.school);
        return Json{ { "float", floats }, { "int", ints }, { "string", strings } };
    }
}
