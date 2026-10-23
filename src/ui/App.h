#pragma once

#include "io/UiSettings.h"
#include "model/Catalogs.h"
#include "model/Lint.h"
#include "model/Preset.h"
#include "model/Usage.h"

namespace mtfui::ui {
    namespace fs = std::filesystem;

    struct Paths {
        fs::path data;  // <game>/Data
        fs::path mtfRoot;  // StorageUtilData/MagicTattoosFramework
        fs::path uiDir;  // SKSE/Plugins/MagicTattoosUI
    };

    struct Editor {
        std::optional<model::Preset> preset;  // working copy
        model::Preset saved;  // as on disk
        model::Preset committed;  // last undo checkpoint
        bool isNew = false;  // not written yet
        bool builtin = false;  // read-only preset shipped with MTF
        int slot = 0;
        std::vector<model::Preset> undo;
        std::vector<model::Preset> redo;
    };

    enum class Tab { Presets, Tattoos, General, Plugins };

    // Copied parts of a tier, for pasting into another tier or preset.
    struct TierClipboard {
        std::optional<model::Slot> tier;
        model::ResolvedVisual tierVisual;
        std::optional<model::Layer> layer;
        std::optional<model::Pulse> pulse;
    };

    // <game>/Data, from the executable's folder rather than the working directory.
    fs::path GameDataDir();

    class App {
    public:
        static App& Get();

        void Init();
        void SetWindow(SKSEMenuFramework::Model::WindowInterface* window) { window_ = window; }
        void DrawSection();
        void DrawWindow();
        void OnMenuOpen();
        void OnMenuClose();
        void OnGameLoaded();
        void OnSaving();  // game thread, right before a save is written

        // Shared state for the tabs.
        Paths paths;
        model::Catalogs cat;
        io::UiSettings settings;
        std::vector<model::PresetFileInfo> library;
        std::vector<model::PresetFileInfo> hiddenPresets;  // soft-deleted user presets
        model::UsageIndex usage;  // user presets only
        Editor ed;
        std::vector<model::LintIssue> issues;

        RE::FormID targetId = 0x14;
        std::string targetName = "You";
        std::vector<std::string> applied;
        bool appliedKnown = false;
        bool livePreview = false;
        bool previewEffects = false;

        // Tattoos tab in "pick for this tier" mode.
        bool picking = false;
        int pickSlot = 0;
        TierClipboard clip;
        std::optional<model::LintTab> inspectorTab;  // tab the tier editor switches to next frame
        bool focusSearch = false;  // Ctrl+F: the preset list's search box takes focus next frame
        void RequestTab(Tab tab) { requestedTab_ = tab; }
        void ResetPanelLayout() { resetLayout_ = true; }

        void Toast(std::string text, bool error = false);
        void ReloadAll();
        void ReloadLibrary();
        bool Dirty() const;
        bool HasPreset() const { return ed.preset.has_value(); }
        model::Preset& Preset() { return *ed.preset; }

        void Open(const std::string& name);  // asks to save first when dirty
        void WhenSafe(std::function<void()> then);  // runs then now, or after the unsaved-changes prompt
        void CreateNew(const std::string& displayName, const std::string& packId = {}, const std::string& entryId = {});  // tattoo goes on Default
        void Duplicate(const std::string& displayName);
        void Delete(const std::string& name);
        bool Save();
        void Revert();
        void Undo();
        void Redo();
        void Relint();
        bool RestoreBackup(const fs::path& backup, const std::string& name);
        bool Undelete(const std::string& name);
        void ExportToClipboard();
        void ImportFromClipboard();
        void ApplyLintFix(const model::LintIssue& issue);
        void MoveTier(int from, int to);  // keeps the moved tier selected

        void SetTarget(RE::FormID id, std::string name);
        void ApplyToTarget(const std::string& name);
        void RemoveFromTarget(const std::string& name);
        void RemoveAllFromTarget();
        void RefreshApplied();
        void UpdatePreview();
        void SaveSettings();

        const model::PresetFileInfo* FindLibrary(const std::string& name) const;
        bool IsApplied(const std::string& name) const;  // on the current target
        bool BuiltinNameTaken(const std::string& name) const;
        std::string UniqueName(const std::string& displayName) const;
        model::LintContext LintCtx() const;
        void UseEntry(int slot, const std::string& packId, const std::string& entryId);

    private:
        void BeginFrame();
        void Draw(bool presetsTab, bool targetBar);
        bool DrawPanels();  // false once the main strip's X is clicked
        void DrawPanel(const char* key, const char* title, const std::function<void()>& body, ImGuiMCP::ImGuiWindowFlags flags = 0);
        io::PanelState& Panel(const std::string& key);
        void HandlePanelRequests();
        void ShowEditorWindows(bool show);
        void DrawStatus();
        void DrawModals();
        void CommitFrame();
        void HandleShortcuts();
        void HandlePlainShortcuts();
        void LoadPresetNow(const std::string& name);
        void CreateNewNow(const std::string& displayName, const std::string& packId, const std::string& entryId);
        void ClearEditor();
        // Unsaved edits survive a game exit in drafts/current.json.
        fs::path DraftPath() const;
        void WriteDraft();
        void DiscardDraft();
        void RestoreDraft();
        void HoldVanityCamera(bool hold);
        void HoldActor(RE::FormID id);  // 0 releases

        SKSEMenuFramework::Model::WindowInterface* window_ = nullptr;
        std::optional<Tab> requestedTab_;
        std::function<void()> pendingAction_;  // runs once the unsaved-changes prompt is answered
        bool confirmOpen_ = false;
        bool draftPending_ = false;  // edits not yet in the draft file
        double lastDraft_ = 0.0;
        bool draftOffer_ = false;  // ask whether to restore a draft from an earlier session
        std::string draftName_;
        struct ToastMsg {
            std::string text;
            bool error;
            double until;
        };
        std::vector<ToastMsg> toasts_;
        double lastLint_ = 0.0;
        bool initialized_ = false;
        bool vanityHeld_ = false;
        RE::FormID holdWanted_ = 0;
        bool resetLayout_ = false;
        bool placeAlways_ = false;  // this frame, saved panel rects override wherever the user dragged them
        bool peeking_ = false;
        bool pickHintShown_ = false;
        int lastSectionFrame_ = -100;
        int beganFrame_ = -1;
        int committedFrame_ = -1;
        int modalsFrame_ = -1;
        struct PreviewInputs {
            RE::FormID target = 0;
            int slot = -1;
            bool effects = false;
            model::Slot tier;
            model::Slot base;  // Default, which the tier may inherit its tattoo from
            bool operator==(const PreviewInputs&) const = default;
        };
        std::optional<PreviewInputs> previewInputs_;
        struct LintInputs {
            std::uint64_t textures = 0;
            bool fresh = false;
            std::map<std::string, bool> pluginEnabled;
            std::array<int, 4> overlayStart{};
            bool operator==(const LintInputs&) const = default;
        };
        LintInputs lintInputs_;
        std::map<std::string, int> overlayCapacity_;
    };
}
