#pragma once

#include "model/Colors.h"
#include "model/PluginCatalog.h"
#include "model/VisualPacks.h"
#include "model/Waveform.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mtfui::model {
    namespace fs = std::filesystem;

    struct PresetFileInfo {
        std::string name;
        std::string displayName;
        fs::path path;
        bool builtin = false;
        int hidden = 0;
        fs::file_time_type modified{};
    };

    // MTF's on-disk data under Data/SKSE/Plugins/StorageUtilData/MagicTattoosFramework.
    struct Catalogs {
        fs::path root;
        std::vector<PluginCatalog> plugins;
        std::vector<VisualPack> packs;
        std::vector<Waveform> waveforms;
        std::vector<NamedColor> colors;
        std::map<std::string, std::string> moduleMap;  // "cond:<id>" / "eff:<id>" lowercase -> plugin id

        void Load(const fs::path& mtfRoot);

        fs::path PresetsDir() const { return root / "presets"; }
        fs::path BuiltinPresetsDir() const { return root / "presets_builtin"; }
        fs::path PresetPath(const std::string& name) const { return PresetsDir() / (name + ".json"); }

        const PluginCatalog* FindPlugin(const std::string& pluginId) const;
        // Accepts "plugin:id" keys, case-insensitively, including keys from before MTF's module split.
        const CatalogEntry* FindCondition(const std::string& key, const PluginCatalog** owner = nullptr) const;
        const CatalogEntry* FindEffect(const std::string& key, const PluginCatalog** owner = nullptr) const;
        const VisualPack* FindPack(const std::string& packId) const;
        const VisualEntry* FindEntry(const std::string& packId, const std::string& entryId) const;
        const Waveform* FindWaveform(const std::string& name) const;  // nullptr for "" (built-in cosine)

        std::vector<PresetFileInfo> ListPresets(bool includeBuiltin) const;
    };

    bool IEquals(const std::string& a, const std::string& b);
}
