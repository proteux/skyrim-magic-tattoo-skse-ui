#pragma once

#include "model/Catalogs.h"
#include "model/Preset.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mtfui::model {
    enum class Severity { Info, Warning, Error };

    enum class LintTab { None, Condition, Look, Pulse, Effects, Timing };

    // One-click repair the UI can offer next to an issue.
    struct LintFix {
        enum class Kind { None, MoveAbove, EnablePlugin } kind = Kind::None;
        int targetSlot = -1;  // MoveAbove: the tier to move above
        std::string pluginId;  // EnablePlugin
    };

    struct LintIssue {
        Severity severity = Severity::Warning;
        int slot = -1;  // -1 = whole preset
        std::string message;
        LintTab tab = LintTab::None;  // where in the tier editor it can be fixed
        LintFix fix;
    };

    struct LintContext {
        std::function<bool(const std::string&)> textureExists;  // path relative to Data/textures
        std::function<bool(const std::string&)> pluginEnabled;  // plugin id
        std::map<std::string, int> overlayStart;  // area -> MTF's first overlay slot
        std::map<std::string, int> overlayCapacity;  // area -> SKEE overlay count
    };

    std::vector<LintIssue> LintPreset(const Catalogs& cat, const Preset& preset, const LintContext& ctx);
}
