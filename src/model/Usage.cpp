#include "model/Usage.h"

#include "io/JsonStore.h"
#include "io/UiSettings.h"

#include <algorithm>
#include <set>

namespace mtfui::model {
    namespace {
        void AddUnique(std::vector<std::string>& list, const std::string& name) {
            if (std::find(list.begin(), list.end(), name) == list.end()) {
                list.push_back(name);
            }
        }

        std::string PluginOf(const Catalogs& cat, const std::string& key, bool condition) {
            const PluginCatalog* owner = nullptr;
            if (condition ? cat.FindCondition(key, &owner) : cat.FindEffect(key, &owner); owner) {
                return io::ToLower(owner->pluginId);
            }
            return io::ToLower(SplitKey(key).first);
        }
    }

    const std::vector<std::string>* UsageIndex::Entry(const std::string& packId, const std::string& entryId) const {
        auto it = entries.find(io::ToLower(packId + "/" + entryId));
        return it == entries.end() ? nullptr : &it->second;
    }

    const std::vector<std::string>* UsageIndex::Plugin(const std::string& pluginId) const {
        auto it = plugins.find(io::ToLower(pluginId));
        return it == plugins.end() ? nullptr : &it->second;
    }

    UsageIndex BuildUsage(const Catalogs& cat, const std::vector<PresetFileInfo>& presets) {
        UsageIndex index;
        for (const auto& info : presets) {
            auto json = io::ReadJson(info.path);
            if (!json || !json->is_object()) {
                continue;
            }
            const auto preset = PresetFromJson(info.name, *json);
            const std::string& who = info.displayName.empty() ? info.name : info.displayName;
            for (int i = 0; i < static_cast<int>(preset.slots.size()); ++i) {
                const auto& s = preset.slots[i];
                if (i > 0 && !s.Configured()) {
                    continue;
                }
                const auto vis = ResolveVisual(preset, i);
                if (!vis.none && !vis.packId.empty() && !vis.entryId.empty()) {
                    AddUnique(index.entries[io::ToLower(vis.packId + "/" + vis.entryId)], who);
                }
                for (const auto& c : s.conditions) {
                    if (!c.pluginId.empty()) {
                        AddUnique(index.plugins[PluginOf(cat, c.pluginId, true)], who);
                    }
                }
                for (const auto& fx : s.effects) {
                    if (!fx.key.empty()) {
                        AddUnique(index.plugins[PluginOf(cat, fx.key, false)], who);
                    }
                }
            }
        }
        return index;
    }
}
