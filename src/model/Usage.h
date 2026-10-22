#pragma once

#include "model/Catalogs.h"

#include <map>
#include <string>
#include <vector>

namespace mtfui::model {
    // Which presets reference each tattoo entry and plugin. Keys are lowercase.
    struct UsageIndex {
        std::map<std::string, std::vector<std::string>> entries;  // "pack/entry" -> preset display names
        std::map<std::string, std::vector<std::string>> plugins;  // plugin id -> preset display names

        const std::vector<std::string>* Entry(const std::string& packId, const std::string& entryId) const;
        const std::vector<std::string>* Plugin(const std::string& pluginId) const;
    };

    UsageIndex BuildUsage(const Catalogs& cat, const std::vector<PresetFileInfo>& presets);
}
