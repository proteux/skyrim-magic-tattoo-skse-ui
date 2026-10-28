#pragma once

#include <string>
#include <vector>

namespace mtfui::model {
    // An applied preset's block of overlay slots in one area, as MTF stored it.
    struct SlotRange {
        std::string preset;
        int base = -1;
        int layers = 0;
    };

    // One overlay area (Body, Face, Hands or Feet) on the player.
    struct AreaSlots {
        int total = 0;  // RaceMenu's overlay count
        int start = 0;  // MTF's start slot
        int mcmWant = 0;  // layers the preset loaded in MTF's MCM needs here, before MTF caps it to what is left
        std::vector<SlotRange> presets;  // applied presets with layers here, in MTF's order
        std::vector<std::string> textures;  // per slot, empty where nothing is drawn
    };

    enum class SlotUse { Free, Mcm, Preset };

    struct SlotCell {
        SlotUse use = SlotUse::Free;
        std::string preset;  // for SlotUse::Preset
        int presetIndex = -1;
        bool other = false;  // holds an overlay MTF didn't draw
        std::string texture;
    };

    // The MCM preset's block at a given start, capped the way MTF's _playerBaseLayers caps it.
    int McmLayersAt(const AreaSlots& a, int start);

    // Where things are now, from the bases MTF stored.
    std::vector<SlotCell> CurrentSlots(const AreaSlots& a);

    // What a move of the start slot leads to: the MCM block at start, then every applied preset packed above it,
    // which is how MTF's _compactAppliedPresets lays them out. skip: a preset left out of the plan.
    struct StartPlan {
        std::vector<SlotCell> cells;
        int needed = 0;  // slots the MCM block and the presets need from start
        bool fits = true;
        std::vector<int> covered;  // slots with an overlay MTF didn't draw, inside MTF's blocks
        std::vector<int> exposed;  // such slots above MTF's blocks, where the next applied preset goes
    };
    StartPlan PlanStart(const AreaSlots& a, int start, const std::string& skip = {});

    // "3", "3 and 5", "3, 5 and 7"
    std::string ListSlots(const std::vector<int>& slots);
}
