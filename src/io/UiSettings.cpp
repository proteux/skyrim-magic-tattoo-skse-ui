#include "io/UiSettings.h"

#include "io/JsonStore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <type_traits>

namespace mtfui::io {
    namespace {
        // Each reader leaves out untouched when the key is missing or has another type.
        void GetBool(const nlohmann::json& j, const char* key, bool& out) {
            if (auto it = j.find(key); it != j.end() && it->is_boolean()) {
                out = it->get<bool>();
            }
        }

        template <class T>
        void GetNumber(const nlohmann::json& j, const char* key, T& out) {
            if (auto it = j.find(key); it != j.end() && it->is_number()) {
                const double v = it->get<double>();
                if constexpr (std::is_floating_point_v<T>) {
                    if (std::isfinite(v)) {
                        out = static_cast<T>(v);
                    }
                } else if (std::isfinite(v) && v >= static_cast<double>(std::numeric_limits<T>::min()) &&
                           v <= static_cast<double>(std::numeric_limits<T>::max())) {
                    out = static_cast<T>(v);
                }
            }
        }

        void GetString(const nlohmann::json& j, const char* key, std::string& out) {
            if (auto it = j.find(key); it != j.end() && it->is_string()) {
                out = it->get<std::string>();
            }
        }
    }

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
        GetBool(*j, "pauseGame", pauseGame);
        GetBool(*j, "livePreview", livePreviewEnabled);
        GetBool(*j, "holdTarget", holdTarget);
        GetNumber(*j, "thumbSize", thumbSize);
        thumbSize = std::clamp(thumbSize, kThumbSizeMin, kThumbSizeMax);
        GetBool(*j, "showDetails", showDetails);
        GetString(*j, "lastPreset", lastPreset);
        GetBool(*j, "splitPanels", splitPanels);
        GetNumber(*j, "panelOpacity", panelOpacity);
        panelOpacity = std::clamp(panelOpacity, 20, 100);
        if (auto it = j->find("panels"); it != j->end() && it->is_object()) {
            for (const auto& [name, v] : it->items()) {
                if (!v.is_object()) {
                    continue;
                }
                PanelState s;
                GetNumber(v, "x", s.x);
                GetNumber(v, "y", s.y);
                GetNumber(v, "w", s.w);
                GetNumber(v, "h", s.h);
                GetBool(v, "open", s.open);
                s.w = std::max(0.0f, s.w);
                s.h = std::max(0.0f, s.h);
                panels[name] = s;
            }
        }
        const auto ids = [&](const char* key, std::vector<std::uint32_t>& out) {
            out.clear();
            if (auto it = j->find(key); it != j->end() && it->is_array()) {
                for (const auto& v : *it) {
                    if (v.is_number_unsigned() || v.is_number_integer()) {
                        out.push_back(v.get<std::uint32_t>());
                    }
                }
            }
        };
        ids("previewActors", previewActors);
        ids("heldActors", heldActors);
    }

    bool UiSettings::Save(const std::filesystem::path& path) const {
        nlohmann::json j;
        j["favorites"] = favorites;
        j["recents"] = recents;
        j["pauseGame"] = pauseGame;
        j["livePreview"] = livePreviewEnabled;
        j["holdTarget"] = holdTarget;
        j["thumbSize"] = thumbSize;
        j["showDetails"] = showDetails;
        j["lastPreset"] = lastPreset;
        j["splitPanels"] = splitPanels;
        j["panelOpacity"] = panelOpacity;
        auto& jp = j["panels"] = nlohmann::json::object();
        for (const auto& [name, s] : panels) {
            jp[name] = { { "x", s.x }, { "y", s.y }, { "w", s.w }, { "h", s.h }, { "open", s.open } };
        }
        j["previewActors"] = previewActors;
        j["heldActors"] = heldActors;
        // Preset names are in the ANSI code page, which isn't always valid UTF-8.
        return WriteTextAtomic(path, j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace));
    }
}
