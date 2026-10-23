#include "model/Catalogs.h"

#include "io/JsonStore.h"
#include "io/UiSettings.h"

#include <algorithm>

namespace mtfui::model {
    namespace {
        std::vector<fs::path> JsonFiles(const fs::path& dir) { return io::ListFiles(dir, ".json"); }

        // A third-party catalog with an unexpected type must not take the whole menu down.
        template <class T, class Fn>
        void LoadEach(const fs::path& dir, std::vector<T>& out, Fn load, std::vector<std::string>& errors) {
            for (const auto& f : JsonFiles(dir)) {
                try {
                    if (auto v = load(f)) {
                        out.push_back(std::move(*v));
                    } else {
                        std::string why;
                        errors.push_back(io::PathText(f) + ": " + (io::ReadJson(f, &why) ? std::string("not in the expected format") : why));
                    }
                } catch (const std::exception& e) {
                    errors.push_back(io::PathText(f) + ": " + e.what());
                }
            }
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
        loadErrors.clear();
        LoadEach(root / "plugins", plugins, LoadPluginCatalog, loadErrors);
        LoadEach(root / "visuals", packs, LoadVisualPack, loadErrors);
        LoadEach(root / "waveforms", waveforms, LoadWaveform, loadErrors);
        try {
            colors = LoadColors(root / "colors.json");
        } catch (const std::exception& e) {
            colors.clear();
            loadErrors.push_back("colors.json: " + std::string(e.what()));
        }
        if (auto j = io::ReadJson(root / "mtf.module_map.json"); j && j->is_object()) {
            if (auto s = j->find("string"); s != j->end() && s->is_object()) {
                for (auto it = s->begin(); it != s->end(); ++it) {
                    if (it.value().is_string() && !it.key().empty() && it.key().front() != '_') {
                        moduleMap[io::ToLower(it.key())] = it.value().get<std::string>();
                    }
                }
            }
        }
        ++generation;
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

    std::vector<PresetFileInfo> Catalogs::ListPresets(bool includeBuiltin, std::vector<PresetFileInfo>* hidden) const {
        if (hidden) {
            hidden->clear();
        }
        std::vector<PresetFileInfo> out;
        auto scan = [&](const fs::path& dir, bool builtin) {
            for (const auto& f : JsonFiles(dir)) {
                PresetFileInfo info;
                info.name = io::NarrowName(f, true).value_or("");
                if (info.name.empty() || info.name.rfind("_mtfui", 0) == 0) {
                    continue;
                }
                info.path = f;
                info.builtin = builtin;
                std::error_code ec;
                info.modified = fs::last_write_time(f, ec);
                info.displayName = info.name;
                if (auto j = io::ReadJson(f); j && j->is_object()) {
                    if (auto d = j->find("displayname"); d != j->end() && d->is_string()) {
                        info.displayName = d->get<std::string>();
                    }
                    if (auto h = j->find("hidden"); h != j->end() && h->is_number()) {
                        info.hidden = h->get<int>();
                    }
                    info.json = std::move(*j);
                }
                if (info.hidden == 1) {
                    if (hidden && !builtin) {
                        hidden->push_back(std::move(info));
                    }
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
