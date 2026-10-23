#pragma once

#include "model/Preset.h"

namespace mtfui::game {
    inline constexpr const char* kPreviewPreset = "_mtfui_preview";

    // Shows one tier's look on an actor by writing a single-tier temporary preset to
    // presets_builtin (hidden from MTF's own pickers) and applying it through MTF.
    class LivePreview {
    public:
        static LivePreview& Get();

        // known: actors that have worn the preview, newest first. A save made during a preview keeps it on them, so
        // every load removes it from all of them, not only from those of the current session.
        void Init(const std::filesystem::path& builtinDir, std::vector<RE::FormID> known, std::function<void(const std::vector<RE::FormID>&)> recordActors);

        // Debounced: the latest request wins and is applied once edits pause.
        void Request(RE::FormID actor, const model::Preset& source, int slot, bool includeEffects);
        void Tick();  // UI thread, every frame
        void Stop();
        // Removes leftovers from a previous session (after a save is loaded).
        void Cleanup();

        bool Active() const { return applied_ != 0 || pending_.has_value(); }
        RE::FormID Actor() const { return applied_; }
        const std::string& Status() const { return status_; }

    private:
        struct Pending {
            RE::FormID actor = 0;
            nlohmann::json doc;
            std::chrono::steady_clock::time_point due;
        };

        void Remember(RE::FormID actor);

        std::filesystem::path file_;
        std::function<void(const std::vector<RE::FormID>&)> record_;
        std::optional<Pending> pending_;
        nlohmann::json last_;
        RE::FormID applied_ = 0;
        bool busy_ = false;
        std::chrono::steady_clock::time_point busySince_;
        int generation_ = 0;  // bumped when in-flight work should no longer count
        std::string status_;
        std::vector<RE::FormID> touched_;  // this session, removed on Stop
        std::vector<RE::FormID> known_;
    };

    model::Preset MakePreviewPreset(const model::Preset& source, int slot, bool includeEffects);
}
