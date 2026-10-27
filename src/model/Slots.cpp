#include "model/Slots.h"

#include "model/Catalogs.h"

#include <algorithm>

namespace mtfui::model {
    namespace {
        void Claim(std::vector<SlotCell>& cells, int from, int count, SlotUse use, const std::string& preset = {}, int index = -1) {
            for (int i = std::max(0, from); i < from + count && i < static_cast<int>(cells.size()); ++i) {
                cells[i].use = use;
                cells[i].preset = preset;
                cells[i].presetIndex = index;
            }
        }

        std::vector<SlotCell> Empty(const AreaSlots& a) {
            std::vector<SlotCell> cells(static_cast<std::size_t>(std::max(0, a.total)));
            for (std::size_t i = 0; i < cells.size() && i < a.textures.size(); ++i) {
                cells[i].texture = a.textures[i];
            }
            return cells;
        }
    }

    int McmLayersAt(const AreaSlots& a, int start) {
        return std::clamp(a.mcmWant, 0, std::max(0, a.total - start));
    }

    std::vector<SlotCell> CurrentSlots(const AreaSlots& a) {
        auto cells = Empty(a);
        Claim(cells, a.start, McmLayersAt(a, a.start), SlotUse::Mcm);
        for (int p = 0; p < static_cast<int>(a.presets.size()); ++p) {
            const auto& r = a.presets[p];
            if (r.base >= 0) {
                Claim(cells, r.base, r.layers, SlotUse::Preset, r.preset, p);
            }
        }
        for (auto& c : cells) {
            c.other = c.use == SlotUse::Free && !c.texture.empty();
        }
        return cells;
    }

    StartPlan PlanStart(const AreaSlots& a, int start, const std::string& skip) {
        const auto now = CurrentSlots(a);
        StartPlan plan;
        plan.cells = Empty(a);
        for (std::size_t i = 0; i < plan.cells.size(); ++i) {
            plan.cells[i].other = now[i].other;
            if (!now[i].other) {
                plan.cells[i].texture.clear();
            }
        }
        int floor = start + a.mcmWant;
        Claim(plan.cells, start, McmLayersAt(a, start), SlotUse::Mcm);
        for (int p = 0; p < static_cast<int>(a.presets.size()); ++p) {
            const auto& r = a.presets[p];
            if (r.layers <= 0 || (!skip.empty() && IEquals(r.preset, skip))) {
                continue;
            }
            Claim(plan.cells, floor, r.layers, SlotUse::Preset, r.preset, p);
            floor += r.layers;
        }
        plan.needed = floor - start;
        plan.fits = floor <= a.total;
        for (int i = 0; i < static_cast<int>(plan.cells.size()); ++i) {
            if (!plan.cells[i].other) {
                continue;
            }
            if (plan.cells[i].use != SlotUse::Free) {
                plan.covered.push_back(i);
            } else if (i >= floor) {
                plan.exposed.push_back(i);
            }
        }
        return plan;
    }

    std::string ListSlots(const std::vector<int>& slots) {
        std::string out;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i > 0) {
                out += i + 1 == slots.size() ? " and " : ", ";
            }
            out += std::to_string(slots[i]);
        }
        return out;
    }
}
