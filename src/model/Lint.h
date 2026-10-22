#pragma once

#include "model/Catalogs.h"
#include "model/Preset.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mtfui::model {
    enum class Severity { Info, Warning, Error };

    struct LintIssue {
        Severity severity = Severity::Warning;
        int slot = -1;  // -1 = whole preset
        std::string message;
    };

    struct LintContext {
        std::function<bool(const std::string&)> textureExists;  // path relative to Data/textures
        std::function<bool(const std::string&)> pluginEnabled;  // plugin id
        std::map<std::string, int> overlayStart;  // area -> MTF's first overlay slot
        std::map<std::string, int> overlayCapacity;  // area -> SKEE overlay count
    };

    std::vector<LintIssue> LintPreset(const Catalogs& cat, const Preset& preset, const LintContext& ctx);
}
