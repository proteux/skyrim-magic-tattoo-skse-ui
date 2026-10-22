#include "io/UiSettings.h"

#include "io/JsonStore.h"

#include <algorithm>
#include <cctype>

namespace mtfui::io {
    std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool UiSettings::IsFavorite(const std::string& key) const {
        const auto k = ToLower(key);
        return std::find(favorites.begin(), favorites.end(), k) != favorites.end();
    }

    void UiSettings::ToggleFavorite(const std::string& key) {
        const auto k = ToLower(key);
        auto it = std::find(favorites.begin(), favorites.end(), k);
        if (it == favorites.end()) {
            favorites.push_back(k);
        } else {
            favorites.erase(it);
        }
    }

    void UiSettings::PushRecent(const std::string& key, std::size_t max) {
        const auto k = ToLower(key);
        std::erase(recents, k);
        recents.insert(recents.begin(), k);
        if (recents.size() > max) {
            recents.resize(max);
        }
    }

    void UiSettings::Load(const std::filesystem::path& path) {
        auto j = ReadJson(path);
        if (!j || !j->is_object()) {
            return;
        }
        auto strings = [&](const char* key, std::vector<std::string>& out) {
            if (auto it = j->find(key); it != j->end() && it->is_array()) {
                out.clear();
                for (const auto& v : *it) {
                    if (v.is_string()) {
                        out.push_back(ToLower(v.get<std::string>()));
                    }
                }
            }
        };
        strings("favorites", favorites);
        strings("recents", recents);
        pauseGame = j->value("pauseGame", pauseGame);
        livePreviewEnabled = j->value("livePreview", livePreviewEnabled);
        holdTarget = j->value("holdTarget", holdTarget);
        heldActor = j->value("heldActor", heldActor);
        thumbSize = std::clamp(j->value("thumbSize", thumbSize), 48, 256);
        lastPreset = j->value("lastPreset", lastPreset);
        splitPanels = j->value("splitPanels", splitPanels);
        panelOpacity = std::clamp(j->value("panelOpacity", panelOpacity), 20, 100);
        if (auto it = j->find("panels"); it != j->end() && it->is_object()) {
            for (const auto& [name, v] : it->items()) {
                if (!v.is_object()) {
                    continue;
                }
                PanelState s;
                s.x = v.value("x", 0.0f);
                s.y = v.value("y", 0.0f);
                s.w = std::max(0.0f, v.value("w", 0.0f));
                s.h = std::max(0.0f, v.value("h", 0.0f));
                s.open = v.value("open", false);
                panels[name] = s;
            }
        }
        previewActors.clear();
        if (auto it = j->find("previewActors"); it != j->end() && it->is_array()) {
            for (const auto& v : *it) {
                if (v.is_number_unsigned() || v.is_number_integer()) {
                    previewActors.push_back(v.get<std::uint32_t>());
                }
            }
        }
    }

    bool UiSettings::Save(const std::filesystem::path& path) const {
        nlohmann::json j;
        j["favorites"] = favorites;
        j["recents"] = recents;
        j["pauseGame"] = pauseGame;
        j["livePreview"] = livePreviewEnabled;
        j["holdTarget"] = holdTarget;
        j["heldActor"] = heldActor;
        j["thumbSize"] = thumbSize;
        j["lastPreset"] = lastPreset;
        j["splitPanels"] = splitPanels;
        j["panelOpacity"] = panelOpacity;
        auto& jp = j["panels"] = nlohmann::json::object();
        for (const auto& [name, s] : panels) {
            jp[name] = { { "x", s.x }, { "y", s.y }, { "w", s.w }, { "h", s.h }, { "open", s.open } };
        }
        j["previewActors"] = previewActors;
        return WriteTextAtomic(path, j.dump(2));
    }
}
