#include "model/PluginCatalog.h"

#include "io/JsonStore.h"

#include <algorithm>
#include <cstdio>

namespace mtfui::model {
    namespace {
        ParamSpec ParseSpec(const Json& j) {
            ParamSpec spec;
            if (!j.is_object()) {
                return spec;
            }
            spec.label = j.value("label", std::string{});
            if (auto it = j.find("menu"); it != j.end() && it->is_array()) {
                spec.kind = ParamKind::Menu;
                for (const auto& o : *it) {
                    if (o.is_object()) {
                        spec.menu.push_back({ o.value("id", std::string{}), o.value("label", o.value("id", std::string{})) });
                    }
                }
            } else if (j.value("text", false)) {
                spec.kind = ParamKind::Text;
            } else if (auto c = j.find("color"); c != j.end() && ((c->is_number() && c->get<int>() != 0) || (c->is_boolean() && c->get<bool>()))) {
                spec.kind = ParamKind::Color;
                spec.min = 0;
                spec.max = 0xFFFFFF;
            } else {
                spec.kind = ParamKind::Slider;
                if (auto m = j.find("min"); m != j.end() && m->is_number()) {
                    spec.min = static_cast<std::int64_t>(m->get<double>());
                }
                if (auto m = j.find("max"); m != j.end() && m->is_number()) {
                    spec.max = static_cast<std::int64_t>(m->get<double>());
                }
                if (auto m = j.find("step"); m != j.end() && m->is_number()) {
                    spec.step = std::max<std::int64_t>(1, static_cast<std::int64_t>(m->get<double>()));
                }
                spec.format = j.value("format", std::string{});
            }
            if (auto d = j.find("default"); d != j.end()) {
                spec.def = spec.Coerce(ParamFromJson(*d));
            } else if (spec.kind == ParamKind::Menu && !spec.menu.empty()) {
                spec.def = spec.menu.front().id;
            } else if (spec.IsString()) {
                spec.def = std::string{};
            } else {
                spec.def = spec.kind == ParamKind::Slider ? spec.min : 0;
            }
            return spec;
        }

        CatalogEntry ParseEntry(const Json& j, bool condition) {
            CatalogEntry e;
            e.id = j.value("id", std::string{});
            e.label = j.value("label", e.id);
            e.description = j.value("description", std::string{});
            e.kind = j.value("kind", std::string{});
            if (condition) {
                static const char* keys[] = { "param", "param2", "param3" };
                for (int i = 0; i < 3; ++i) {
                    if (auto it = j.find(keys[i]); it != j.end()) {
                        e.params[i] = ParseSpec(*it);
                    }
                }
            } else {
                for (int i = 0; i < 5; ++i) {
                    if (auto it = j.find("param" + std::to_string(i + 1)); it != j.end()) {
                        e.params[i] = ParseSpec(*it);
                    }
                }
            }
            return e;
        }
    }

    std::string ParamSpec::Display(const ParamValue& v) const {
        switch (kind) {
            case ParamKind::Menu: {
                const auto id = ParamStr(v);
                for (const auto& o : menu) {
                    if (_stricmp(o.id.c_str(), id.c_str()) == 0) {
                        return o.label;
                    }
                }
                return id.empty() ? "(none)" : id;
            }
            case ParamKind::Text: {
                const auto s = ParamStr(v);
                return s.empty() ? "(any)" : s;
            }
            case ParamKind::Color: {
                return FormatHexColor(static_cast<std::uint32_t>(ParamInt(v)));
            }
            case ParamKind::Slider: {
                const auto n = std::to_string(ParamInt(v));
                if (format.empty()) {
                    return n;
                }
                std::string out = format;
                if (auto pos = out.find("{0}"); pos != std::string::npos) {
                    out.replace(pos, 3, n);
                }
                return out;
            }
            default:
                return ParamStr(v);
        }
    }

    ParamValue ParamSpec::Coerce(const ParamValue& v) const {
        if (IsString()) {
            return ParamStr(v);
        }
        if (std::holds_alternative<std::string>(v)) {
            return ParamInt(v);
        }
        return v;
    }

    int CatalogEntry::ParamCount() const {
        int n = 0;
        for (int i = 0; i < 5; ++i) {
            if (params[i].kind != ParamKind::None) {
                n = i + 1;
            }
        }
        return n;
    }

    std::optional<PluginCatalog> LoadPluginCatalog(const std::filesystem::path& file) {
        auto j = io::ReadJson(file);
        if (!j || !j->is_object()) {
            return std::nullopt;
        }
        PluginCatalog c;
        c.file = file;
        c.pluginId = j->value("pluginid", file.stem().string());
        c.label = j->value("pluginlabel", c.pluginId);
        if (auto it = j->find("conditions"); it != j->end() && it->is_array()) {
            for (const auto& e : *it) {
                if (e.is_object()) {
                    c.conditions.push_back(ParseEntry(e, true));
                }
            }
        }
        if (auto it = j->find("effects"); it != j->end() && it->is_array()) {
            for (const auto& e : *it) {
                if (e.is_object()) {
                    c.effects.push_back(ParseEntry(e, false));
                }
            }
        }
        return c;
    }

    std::pair<std::string, std::string> SplitKey(const std::string& key) {
        const auto pos = key.find(':');
        if (pos == std::string::npos) {
            return { key, {} };
        }
        return { key.substr(0, pos), key.substr(pos + 1) };
    }

    std::string FillDescription(const CatalogEntry& entry, const std::array<ParamValue, 5>& values) {
        std::string out = entry.description;
        for (int i = 0; i < 5; ++i) {
            const std::string token = "{param" + std::to_string(i + 1) + "}";
            for (auto pos = out.find(token); pos != std::string::npos; pos = out.find(token, pos)) {
                const auto shown = entry.params[i].Display(values[i]);
                out.replace(pos, token.size(), shown);
                pos += shown.size();
            }
        }
        return out;
    }
}
