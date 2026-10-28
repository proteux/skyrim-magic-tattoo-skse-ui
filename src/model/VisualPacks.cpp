#include "model/VisualPacks.h"

#include "io/JsonStore.h"
#include "io/Translations.h"
#include "io/UiSettings.h"

namespace mtfui::model {
    const VisualEntry* VisualPack::Find(const std::string& entryId) const {
        for (const auto& e : entries) {
            if (_stricmp(e.id.c_str(), entryId.c_str()) == 0) {
                return &e;
            }
        }
        return nullptr;
    }

    std::optional<VisualPack> LoadVisualPack(const std::filesystem::path& file) {
        auto j = io::ReadJson(file);
        if (!j || !j->is_object()) {
            return std::nullopt;
        }
        VisualPack p;
        p.file = file;
        p.packId = j->value("packId", j->value("packid", file.stem().string()));
        p.label = j->value("label", p.packId);
        p.area = NormalizeArea(j->value("area", std::string{ "Body" }));
        if (auto it = j->find("entries"); it != j->end() && it->is_array()) {
            for (const auto& e : *it) {
                if (!e.is_object()) {
                    continue;
                }
                VisualEntry v;
                v.id = e.value("id", std::string{});
                v.label = e.value("label", v.id);
                v.description = e.value("description", std::string{});
                if (auto l = e.find("layers"); l != e.end() && l->is_array()) {
                    for (const auto& layer : *l) {
                        if (layer.is_object()) {
                            v.textures.push_back(layer.value("texture", std::string{}));
                        } else if (layer.is_string()) {
                            v.textures.push_back(layer.get<std::string>());
                        }
                    }
                }
                if (auto t = e.find("tags"); t != e.end() && t->is_object()) {
                    v.subject = t->value("subject", std::string{});
                    v.style = t->value("style", std::string{});
                    v.placement = t->value("placement", std::string{});
                }
                if (!v.id.empty()) {
                    p.entries.push_back(std::move(v));
                }
            }
        }
        return p;
    }

    std::string NormalizeArea(const std::string& area) {
        const auto a = io::ToLower(area);
        if (a == "face" || a == "head") {
            return "Face";
        }
        if (a == "hand" || a == "hands") {
            return "Hands";
        }
        if (a == "feet" || a == "foot") {
            return "Feet";
        }
        return "Body";
    }

    const char* AreaLabel(std::string_view area) {
        if (area == "Face") {
            return T("$MTFUI_Area_Face");
        }
        if (area == "Hands") {
            return T("$MTFUI_Area_Hands");
        }
        if (area == "Feet") {
            return T("$MTFUI_Area_Feet");
        }
        return T("$MTFUI_Area_Body");
    }
}
