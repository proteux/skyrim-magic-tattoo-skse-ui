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

        // Shared state for the tabs.
        Paths paths;
        model::Catalogs cat;
        io::UiSettings settings;
        std::vector<model::PresetFileInfo> library;
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
        void RequestTab(Tab tab) { requestedTab_ = tab; }

        void Toast(std::string text, bool error = false);
        void ReloadAll();
        void ReloadLibrary();
        bool Dirty() const;
        bool HasPreset() const { return ed.preset.has_value(); }
        model::Preset& Preset() { return *ed.preset; }

        void Open(const std::string& name);  // asks to save first when dirty
        void CreateNew(const std::string& displayName);
        void Duplicate(const std::string& displayName);
        void Delete(const std::string& name);
        bool Save();
        void Revert();
        void Undo();
        void Redo();
        void Relint();

        void SetTarget(RE::FormID id, std::string name);
        void ApplyToTarget(const std::string& name);
        void RemoveFromTarget(const std::string& name);
        void RemoveAllFromTarget();
        void RefreshApplied();
        void UpdatePreview();
        void SaveSettings();

        bool BuiltinNameTaken(const std::string& name) const;
        std::string UniqueName(const std::string& displayName) const;
        model::LintContext LintCtx() const;
        void UseEntry(int slot, const std::string& packId, const std::string& entryId);

    private:
        void Draw();
        void DrawToasts();
        void DrawModals();
        void CommitFrame();
        void HandleShortcuts();
        void LoadPresetNow(const std::string& name);

        SKSEMenuFramework::Model::WindowInterface* window_ = nullptr;
        std::optional<Tab> requestedTab_;
        std::string pendingOpen_;
        bool confirmOpen_ = false;
        struct ToastMsg {
            std::string text;
            bool error;
            double until;
        };
        std::vector<ToastMsg> toasts_;
        double lastLint_ = 0.0;
        bool initialized_ = false;
        std::map<std::string, int> overlayCapacity_;
    };
}
