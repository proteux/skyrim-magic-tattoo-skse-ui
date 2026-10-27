#pragma once

#include "model/Slots.h"

namespace mtfui::game {
    inline constexpr const char* kMtfPlugin = "MagicTattoosFramework.esp";
    inline constexpr RE::FormID kMainQuestLocalId = 0x803;
    inline constexpr const char* kMainQuestScript = "MTF_MainQuest";
    inline constexpr const char* kMcmScript = "MTF_MCMQuest";

    // Overlay areas in the order MTF names them for SetOverlaySlot.
    inline constexpr std::array<const char*, 4> kAreas{ "Body", "Face", "Hands", "Feet" };

    struct MtfSnapshot {
        bool loaded = false;  // quest found and script bound
        bool fresh = false;  // at least one refresh completed
        bool modActive = false;
        bool debugMode = false;
        float updateInterval = 0.1f;
        std::array<int, 4> overlayStart{ 2, 0, 0, 0 };
        int visualPackCount = 0;
        bool rescanOnLoad = true;
        bool skyrimNet = false;
        bool snPersist = false;
        int pluginCount = 0;
        std::map<std::string, bool> pluginEnabled;  // lowercase plugin id
        std::vector<std::string> registeredPlugins;  // lowercase plugin ids
        std::string mcmEditing;  // preset the MCM has loaded for editing
    };

    // The player's overlay slots: RaceMenu's counts, MTF's blocks and what is drawn in each slot.
    struct SlotUsage {
        std::array<model::AreaSlots, 4> areas;  // in kAreas order
        // MTF caches RaceMenu's counts in its save the first time it asks and never asks again, so a raised
        // iNumOverlays went unnoticed. Reading corrects the cache; this is what it held when it was wrong, else -1.
        std::array<int, 4> staleCounts{ -1, -1, -1, -1 };
    };

    // Everything the UI asks of MTF's Papyrus side. Calls are queued to the game thread;
    // results are handed back through PostToUi so UI state is only touched while rendering.
    class MtfBridge {
    public:
        static MtfBridge& Get();

        void OnDataLoaded();
        bool Installed() const { return quest_ != nullptr; }
        RE::TESQuest* Quest() const { return quest_; }

        const MtfSnapshot& Snapshot() const { return snap_; }  // UI thread only
        void Refresh();
        void RefreshPlugins(const std::vector<std::string>& pluginIds);

        void SetModActive(bool on);
        void SetDebugMode(bool on);
        void SetUpdateInterval(float seconds);
        // Moves MTF's start slot and repacks the presets applied to the player above it, keeping their state.
        void MoveOverlayStart(int area, int start, std::function<void()> done);
        // Empties the preset loaded in MTF's MCM, the player's character default, as the MCM's "New preset" does,
        // and repacks the presets applied to the player into the slots it held. The preset's file stays on disk.
        void ClearMcmPreset(std::function<void()> done);
        void ReadSlotUsage(std::function<void(SlotUsage)> done);
        void ReloadVisualPacks();
        void SetRescanOnLoad(bool on);
        void SetPluginEnabled(const std::string& pluginId, bool on);
        void SetSkyrimNetPersist(bool on);
        void StopShadersOn(RE::FormID actor);

        // After a preset file changes on disk: drop PapyrusUtil's cached copy and MTF's parsed
        // copy so actors wearing it pick up the edit, and refresh MTF's own editor if it has it loaded.
        void PresetChanged(const std::string& name, bool builtin, std::function<void()> done = {});

        void Apply(RE::FormID actor, const std::string& name, std::function<void(int)> done);
        void Remove(RE::FormID actor, const std::string& name, std::function<void()> done);
        void ListApplied(RE::FormID actor, std::function<void(std::vector<std::string>)> done);

        // Presets the live preview took off an actor, stored on the actor with StorageUtil so the list lives in the
        // save: a save made mid-preview gets them back on load, and older saves never see the list.
        void WriteSuspended(RE::FormID actor, std::vector<std::string> names, std::function<void()> done = {});
        void ReadSuspended(RE::FormID actor, std::function<void(std::vector<std::string>)> done);

        // Thread hand-off.
        static void RunOnGame(std::function<void()> fn);
        static void PostToUi(std::function<void()> fn);
        static void DrainUi();
        // After a save loads: callbacks from calls started earlier are dropped instead of run.
        static void NewSession();

    private:
        RE::TESQuest* quest_ = nullptr;
        RE::TESQuest* mcmQuest_ = nullptr;
        MtfSnapshot snap_;
    };

    // Formats an AddAppliedPreset return code for the user.
    std::string ApplyResultText(int rc, bool onPlayer = true);
}
