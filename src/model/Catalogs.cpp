#include "model/Catalogs.h"

#include "io/JsonStore.h"
#include "io/UiSettings.h"

#include <algorithm>

namespace mtfui::model {
    namespace {
        std::vector<fs::path> JsonFiles(const fs::path& dir) {
            std::vector<fs::path> out;
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) {
                return out;
            }
            for (const auto& e : fs::directory_iterator(dir, ec)) {
                if (e.is_regular_file(ec) && io::ToLower(e.path().extension().string()) == ".json") {
                    out.push_back(e.path());
                }
            }
            std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
                return io::ToLower(a.filename().string()) < io::ToLower(b.filename().string());
            });
            return out;
        }

        const CatalogEntry* FindIn(const std::vector<PluginCatalog>& plugins, const std::map<std::string, std::string>& moduleMap,
            const std::string& key, bool condition, const PluginCatalog** owner) {
            auto [pid, id] = SplitKey(key);
            auto search = [&](const std::string& pluginId) -> const CatalogEntry* {
                for (const auto& p : plugins) {
                    if (!IEquals(p.pluginId, pluginId)) {
                        continue;
                    }
                    for (const auto& e : condition ? p.conditions : p.effects) {
                        if (IEquals(e.id, id)) {
                            if (owner) {
                                *owner = &p;
                            }
                            return &e;
                        }
                    }
                }
                return nullptr;
            };
            if (auto* hit = search(pid)) {
                return hit;
            }
            auto it = moduleMap.find((condition ? "cond:" : "eff:") + io::ToLower(id));
            if (it != moduleMap.end() && !IEquals(it->second, pid)) {
                return search(it->second);
            }
            return nullptr;
        }
    }

    bool IEquals(const std::string& a, const std::string& b) {
        return a.size() == b.size() && _stricmp(a.c_str(), b.c_str()) == 0;
    }

    void Catalogs::Load(const fs::path& mtfRoot) {
        root = mtfRoot;
        plugins.clear();
        packs.clear();
        waveforms.clear();
        moduleMap.clear();
        for (const auto& f : JsonFiles(root / "plugins")) {
            if (auto c = LoadPluginCatalog(f)) {
                plugins.push_back(std::move(*c));
            }
        }
        for (const auto& f : JsonFiles(root / "visuals")) {
            if (auto p = LoadVisualPack(f)) {
                packs.push_back(std::move(*p));
            }
        }
        for (const auto& f : JsonFiles(root / "waveforms")) {
            if (auto w = LoadWaveform(f)) {
                waveforms.push_back(std::move(*w));
            }
        }
        colors = LoadColors(root / "colors.json");
        if (auto j = io::ReadJson(root / "mtf.module_map.json"); j && j->is_object()) {
            if (auto s = j->find("string"); s != j->end() && s->is_object()) {
                for (auto it = s->begin(); it != s->end(); ++it) {
                    if (it.value().is_string() && it.key().front() != '_') {
                        moduleMap[io::ToLower(it.key())] = it.value().get<std::string>();
                    }
                }
            }
        }
    }

    const PluginCatalog* Catalogs::FindPlugin(const std::string& pluginId) const {
        for (const auto& p : plugins) {
            if (IEquals(p.pluginId, pluginId)) {
                return &p;
            }
        }
        return nullptr;
    }

    const CatalogEntry* Catalogs::FindCondition(const std::string& key, const PluginCatalog** owner) const {
        return FindIn(plugins, moduleMap, key, true, owner);
    }

    const CatalogEntry* Catalogs::FindEffect(const std::string& key, const PluginCatalog** owner) const {
        return FindIn(plugins, moduleMap, key, false, owner);
    }

    const VisualPack* Catalogs::FindPack(const std::string& packId) const {
        for (const auto& p : packs) {
            if (IEquals(p.packId, packId)) {
                return &p;
            }
        }
        return nullptr;
    }

    const VisualEntry* Catalogs::FindEntry(const std::string& packId, const std::string& entryId) const {
        const auto* p = FindPack(packId);
        return p ? p->Find(entryId) : nullptr;
    }

    const Waveform* Catalogs::FindWaveform(const std::string& name) const {
        if (name.empty()) {
            return nullptr;
        }
        for (const auto& w : waveforms) {
            if (IEquals(w.name, name)) {
                return &w;
            }
        }
        return nullptr;
    }

    std::vector<PresetFileInfo> Catalogs::ListPresets(bool includeBuiltin) const {
        std::vector<PresetFileInfo> out;
        auto scan = [&](const fs::path& dir, bool builtin) {
            for (const auto& f : JsonFiles(dir)) {
                PresetFileInfo info;
                info.name = f.stem().string();
                if (info.name.rfind("_mtfui", 0) == 0) {
                    continue;
                }
                info.path = f;
                info.builtin = builtin;
                std::error_code ec;
                info.modified = fs::last_write_time(f, ec);
                if (auto j = io::ReadJson(f); j && j->is_object()) {
                    info.displayName = j->value("displayname", info.name);
                    if (auto h = j->find("hidden"); h != j->end() && h->is_number()) {
                        info.hidden = h->get<int>();
                    }
                } else {
                    info.displayName = info.name;
                }
                if (info.hidden == 1) {
                    continue;
                }
                const bool shadowed = std::any_of(out.begin(), out.end(), [&](const PresetFileInfo& o) { return IEquals(o.name, info.name); });
                if (!shadowed) {
                    out.push_back(std::move(info));
                }
            }
        };
        scan(PresetsDir(), false);
        if (includeBuiltin) {
            scan(BuiltinPresetsDir(), true);
        }
        return out;
    }
}
