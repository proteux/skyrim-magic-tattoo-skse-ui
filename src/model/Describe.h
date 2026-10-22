#pragma once

#include "model/Catalogs.h"
#include "model/Preset.h"

#include <string>

namespace mtfui::model {
    std::string SlotLabel(const Preset& preset, int slot);

    std::array<ParamValue, 5> ConditionParams(const ConditionItem& item);

    // "is at least 50% aroused during a sexual encounter"
    std::string DescribeCondition(const Catalogs& cat, const ConditionItem& item);
    std::string DescribeEffect(const Catalogs& cat, const Effect& effect);
    std::string DescribeVisual(const Catalogs& cat, const Preset& preset, int slot);

    // One-line summary for the tier ladder: "When <conditions>: <look>, <pulse>, <effects>".
    std::string DescribeSlot(const Catalogs& cat, const Preset& preset, int slot);

    // Fills a ConditionItem/Effect's params with the catalog defaults.
    void ApplyConditionDefaults(const CatalogEntry& entry, ConditionItem& item);
    void ApplyEffectDefaults(const CatalogEntry& entry, Effect& effect);
}
