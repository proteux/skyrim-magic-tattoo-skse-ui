#pragma once

#include "model/Preset.h"

namespace mtfui::game {
    inline constexpr const char* kPreviewPreset = "_mtfui_preview";

    // Shows one tier's look on an actor by writing a single-tier temporary preset to
    // presets_builtin (hidden from MTF's own pickers) and applying it through MTF.
    //
    // Applied presets that would draw the same tattoo again are taken off the actor while the preview runs
    // ("suspended") and put back when it stops, moves to another actor, or fails.
    class LivePreview {
    public:
        static LivePreview& Get();

        // known: actors that have worn the preview, newest first. A save made during a preview keeps it on them, so
        // every load removes it from all of them, not only from those of the current session.
        void Init(const std::filesystem::path& builtinDir, std::vector<RE::FormID> known, std::function<void(const std::vector<RE::FormID>&)> recordActors);
        // error: a suspended preset could not be put back. appliedChanged: presets went back on an actor.
        void SetListeners(std::function<void(const std::string&)> error, std::function<void()> appliedChanged);

        // Debounced: the latest request wins and is applied once edits pause. suspend: presets applied to the actor
        // that come off while this preview shows.
        void Request(RE::FormID actor, const model::Preset& source, int slot, bool includeEffects, std::vector<std::string> suspend);
        void Tick();  // UI thread, every frame
        void Stop();
        // Removes leftovers from a previous session (after a save is loaded), and puts back what a save made
        // mid-preview had suspended.
        void Cleanup();

        bool Active() const { return applied_ != 0 || pending_.has_value() || busy_ || !suspended_.empty(); }
        RE::FormID Actor() const { return applied_; }
        const std::string& Status() const { return status_; }

        const std::vector<std::string>& SuspendedOn(RE::FormID actor) const;
        bool IsSuspended(RE::FormID actor, const std::string& name) const;
        // The user removed a suspended preset: it stays off when the preview stops.
        void Forget(RE::FormID actor, const std::string& name);

    private:
        struct Pending {
            RE::FormID actor = 0;
            nlohmann::json doc;
            std::vector<std::string> suspend;
            std::chrono::steady_clock::time_point due;
        };
        using Step = std::function<void(std::function<void()> next)>;

        void Remember(RE::FormID actor);
        void RunSteps(std::shared_ptr<std::vector<Step>> steps, std::size_t i, int gen);
        // Re-applies names on actor. rewriteRecord: the saved list is then set to whatever failed to go back.
        void Restore(RE::FormID actor, std::vector<std::string> names, bool rewriteRecord, std::function<void()> done = {});

        std::filesystem::path file_;
        std::function<void(const std::vector<RE::FormID>&)> record_;
        std::function<void(const std::string&)> onError_;
        std::function<void()> onAppliedChanged_;
        std::optional<Pending> pending_;
        nlohmann::json last_;  // last document sent to MTF, who it was for, and what it suspended
        RE::FormID lastActor_ = 0;
        std::vector<std::string> lastSuspend_;
        RE::FormID applied_ = 0;
        bool busy_ = false;
        std::chrono::steady_clock::time_point busySince_;
        int generation_ = 0;  // bumped when in-flight work should no longer count
        std::string status_;
        std::vector<RE::FormID> touched_;  // this session, removed on Stop
        std::vector<RE::FormID> known_;
        std::map<RE::FormID, std::vector<std::string>> suspended_;  // taken off in game, owed back
    };

    model::Preset MakePreviewPreset(const model::Preset& source, int slot, bool includeEffects);
}
