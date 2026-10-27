// The open preset: loading, creating, saving, undo, drafts, and the library file operations around it.

#include "ui/App.h"

#include "game/LivePreview.h"
#include "game/MtfBridge.h"
#include "io/JsonStore.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    void App::Open(const std::string& name) {
        if (ed.preset && model::IEquals(ed.preset->name, name) && !ed.isNew) {
            return;
        }
        WhenSafe([this, name] { LoadPresetNow(name); });
    }

    void App::WhenSafe(std::function<void()> then) {
        if (!Dirty()) {
            then();
            return;
        }
        pendingAction_ = std::move(then);
        confirmOpen_ = true;
    }

    void App::ClearEditor() {
        ed = {};
        picking = false;
        DiscardDraft();
    }

    fs::path App::DraftPath() const {
        return paths.uiDir / "drafts" / "current.json";
    }

    void App::WriteDraft() {
        draftPending_ = false;
        if (!ed.preset || !Dirty()) {
            DiscardDraft();
            return;
        }
        nlohmann::json j;
        j["name"] = ed.preset->name;
        j["isNew"] = ed.isNew;
        j["builtin"] = ed.builtin;
        j["preset"] = model::PresetToJson(*ed.preset);
        std::string error;
        if (!io::WriteTextAtomic(DraftPath(), j.dump(1, '\t', false, nlohmann::json::error_handler_t::replace), &error)) {
            logger::warn("Could not write the draft: {}", error);
        }
    }

    void App::DiscardDraft() {
        draftPending_ = false;
        std::error_code ec;
        fs::remove(DraftPath(), ec);
    }

    void App::RestoreDraft() {
        auto j = io::ReadJson(DraftPath());
        DiscardDraft();
        if (!j || !j->is_object() || !j->contains("preset") || !(*j)["preset"].is_object()) {
            return;
        }
        const std::string name = j->value("name", std::string{});
        const bool isNew = j->value("isNew", true);
        ed = {};
        picking = false;
        ed.preset = model::PresetFromJson(name, (*j)["preset"]);
        ed.builtin = j->value("builtin", false);
        ed.isNew = isNew;
        ed.saved = *ed.preset;
        if (const auto* info = isNew ? nullptr : FindLibrary(name)) {
            if (auto disk = io::ReadJson(info->path); disk && disk->is_object()) {
                ed.saved = model::PresetFromJson(name, *disk);
            }
        } else {
            ed.isNew = true;
        }
        ed.committed = *ed.preset;
        Relint();
        Toast("Restored unsaved edits to " + ed.preset->displayName);
        logger::info("Restored draft of {}", name);
    }

    void App::LoadPresetNow(const std::string& name) {
        const auto* it = FindLibrary(name);
        if (!it) {
            Toast("Preset '" + name + "' no longer exists", true);
            return;
        }
        std::string error;
        auto json = io::ReadJson(it->path, &error);
        if (!json || !json->is_object()) {
            Toast("Could not read " + io::PathText(it->path.filename()) + ": " + (json ? "not a JSON object" : error), true);
            return;
        }
        ClearEditor();
        ed.preset = model::PresetFromJson(it->name, *json);
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.builtin = it->builtin;
        settings.lastPreset = it->name;
        SaveSettings();
        Relint();
    }

    void App::CreateNew(const std::string& displayName, const std::string& packId, const std::string& entryId) {
        WhenSafe([this, displayName, packId, entryId] { CreateNewNow(displayName, packId, entryId); });
    }

    void App::CreateNewNow(const std::string& displayName, const std::string& packId, const std::string& entryId) {
        ClearEditor();
        ed.preset = model::NewPreset(UniqueName(displayName), displayName);
        if (!packId.empty()) {
            ed.preset->slots[0].packId = packId;
            ed.preset->slots[0].entryId = entryId;
            settings.PushRecent(packId + "/" + entryId);
            SaveSettings();
        }
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.isNew = true;
        draftPending_ = true;
        Relint();
    }

    void App::Duplicate(const std::string& displayName, const std::string& source) {
        const bool fromEditor = source.empty() || (ed.preset && !ed.isNew && model::IEquals(ed.preset->name, source));
        if (fromEditor) {
            if (ed.preset) {
                DuplicateNow(*ed.preset, displayName);
            }
            return;
        }
        const auto* info = FindLibrary(source);
        if (!info) {
            Toast("Preset '" + source + "' no longer exists", true);
            return;
        }
        std::string error;
        auto json = io::ReadJson(info->path, &error);
        if (!json || !json->is_object()) {
            Toast("Could not read " + io::PathText(info->path.filename()) + ": " + (json ? "not a JSON object" : error), true);
            return;
        }
        WhenSafe([this, displayName, original = model::PresetFromJson(info->name, *json)] { DuplicateNow(original, displayName); });
    }

    void App::DuplicateNow(model::Preset copy, const std::string& displayName) {
        copy.displayName = displayName;
        copy.name = UniqueName(displayName);
        copy.hidden = 0;
        ClearEditor();
        ed.preset = std::move(copy);
        ed.saved = *ed.preset;
        ed.committed = *ed.preset;
        ed.isNew = true;
        draftPending_ = true;
        Relint();
    }

    void App::Delete(const std::string& name) {
        const auto path = cat.PresetPath(name);
        auto json = io::ReadJson(path);
        if (!json) {
            Toast("Only your own presets can be deleted", true);
            return;
        }
        std::string error;
        const bool backedUp = io::Backup(path, paths.uiDir / "backups", 10, &error);
        if (!backedUp) {
            logger::warn("No backup of {} before delete: {}", name, error);
        }
        // Same soft delete as MTF: a hidden=1 tombstone that MTF skips and a later save can reclaim.
        (*json)["hidden"] = 1;
        if (!io::WriteJsonStyled(path, *json, &error)) {
            Toast("Delete failed: " + error, true);
            return;
        }
        if (ed.preset && model::IEquals(ed.preset->name, name)) {
            ClearEditor();
        }
        game::MtfBridge::Get().PresetChanged(name, false);
        ReloadLibrary();
        logger::info("Deleted preset {}", name);
        Toast("Deleted " + name + ". Bring it back from General > Deleted presets.");
    }

    bool App::Undelete(const std::string& name) {
        const auto path = cat.PresetPath(name);
        std::string error;
        auto json = io::ReadJson(path, &error);
        if (!json || !json->is_object()) {
            Toast("Could not read " + name + ": " + (json ? "not a JSON object" : error), true);
            return false;
        }
        (*json)["hidden"] = 0;
        if (!io::WriteJsonStyled(path, *json, &error)) {
            Toast("Could not restore " + name + ": " + error, true);
            return false;
        }
        game::MtfBridge::Get().PresetChanged(name, false);
        ReloadLibrary();
        logger::info("Undeleted preset {}", name);
        Toast("Restored " + json->value("displayname", name));
        return true;
    }

    bool App::RestoreBackup(const fs::path& backup, const std::string& name) {
        if (BuiltinNameTaken(name)) {
            Toast("'" + name + "' is an MTF built-in name; it can't be restored over", true);
            return false;
        }
        std::string error;
        auto json = io::ReadJson(backup, &error);
        if (!json || !json->is_object()) {
            Toast("Could not read the backup: " + (json ? std::string("not a JSON object") : error), true);
            return false;
        }
        const auto path = cat.PresetPath(name);
        std::error_code ec;
        // The version being replaced becomes a backup itself, so a restore can be undone the same way.
        if (fs::exists(path, ec) && !io::Backup(path, paths.uiDir / "backups", 10, &error)) {
            logger::warn("No backup of {} before restoring: {}", name, error);
        }
        (*json)["hidden"] = 0;
        if (!io::WriteJsonStyled(path, *json, &error)) {
            Toast("Restore failed: " + error, true);
            return false;
        }
        game::MtfBridge::Get().PresetChanged(name, false);
        ReloadLibrary();
        if (ed.preset && model::IEquals(ed.preset->name, name) && !ed.isNew) {
            if (Dirty()) {
                // Keep the unsaved edits; saving them would overwrite the restored file.
                ed.saved = model::PresetFromJson(name, *json);
                Toast("Restored " + name + ". Your open edits weren't touched; Revert shows the restored version.");
                return true;
            }
            LoadPresetNow(name);
        }
        logger::info("Restored {} from {}", name, io::PathText(backup.filename()));
        Toast("Restored " + json->value("displayname", name) + " from the backup");
        return true;
    }

    void App::ExportToClipboard() {
        if (!ed.preset) {
            return;
        }
        const auto text = model::PresetToJson(*ed.preset).dump(1, '\t', false, nlohmann::json::error_handler_t::replace);
        SetClipboardText(text.c_str());
        Toast("Copied " + ed.preset->displayName + " to the clipboard as JSON");
    }

    void App::ImportFromClipboard() {
        const char* raw = GetClipboardText();
        std::string error;
        auto json = model::ParsePresetText(raw ? raw : "", &error);
        if (!json) {
            Toast("Nothing to import: " + error, true);
            return;
        }
        WhenSafe([this, j = std::move(*json)] {
            const std::string display = j.value("displayname", std::string("Imported preset"));
            ClearEditor();
            ed.preset = model::PresetFromJson(UniqueName(display), j);
            ed.preset->hidden = 0;
            ed.saved = model::NewPreset(ed.preset->name, display);
            ed.committed = *ed.preset;
            ed.isNew = true;
            draftPending_ = true;
            Relint();
            Toast("Imported " + display + ". Save it to keep it.");
        });
    }

    void App::CopyTiersFrom(const std::string& target, const std::string& source) {
        const auto paste = [this, target, source] {
            const auto* info = FindLibrary(source);
            std::string error;
            auto json = info ? io::ReadJson(info->path, &error) : std::nullopt;
            if (!json || !json->is_object()) {
                Toast("Could not read " + source + (error.empty() ? std::string{} : ": " + error), true);
                return;
            }
            if (!ed.preset || !model::IEquals(ed.preset->name, target) || ed.isNew) {
                LoadPresetNow(target);
            }
            if (!ed.preset || !model::IEquals(ed.preset->name, target)) {
                return;
            }
            const auto src = model::PresetFromJson(info->name, *json);
            model::CopyTiers(*ed.preset, src);
            Toast("Copied the tiers of " + src.displayName + " into " + ed.preset->displayName + ". Save to keep them.");
        };
        if (ed.preset && model::IEquals(ed.preset->name, target) && !ed.isNew) {
            paste();
        } else {
            WhenSafe(paste);
        }
    }

    void App::SetGroup(const std::string& name, std::string group) {
        group.erase(0, group.find_first_not_of(' '));
        group.erase(group.find_last_not_of(' ') + 1);
        const auto path = cat.PresetPath(name);
        std::string error;
        auto json = io::ReadJson(path, &error);
        if (!json || !json->is_object()) {
            Toast("Only your own presets can be grouped", true);
            return;
        }
        if (!io::Backup(path, paths.uiDir / "backups", 10, &error)) {
            logger::warn("No backup of {} before regrouping: {}", name, error);
        }
        if (group.empty()) {
            json->erase(model::kGroupKey);
        } else {
            (*json)[model::kGroupKey] = group;
        }
        if (!io::WriteJsonStyled(path, *json, &error)) {
            Toast("Could not move " + name + ": " + error, true);
            return;
        }
        if (ed.preset && model::IEquals(ed.preset->name, name) && !ed.isNew) {
            for (auto* p : { &*ed.preset, &ed.saved, &ed.committed }) {
                p->group = group;
            }
        }
        game::MtfBridge::Get().PresetChanged(name, false);
        ReloadLibrary();
        logger::info("Moved preset {} to group '{}'", name, group);
    }

    std::vector<std::string> App::Groups() const {
        std::vector<std::string> out;
        for (const auto& info : library) {
            if (!info.builtin && !info.group.empty() &&
                std::none_of(out.begin(), out.end(), [&](const std::string& g) { return model::IEquals(g, info.group); })) {
                out.push_back(info.group);
            }
        }
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return io::ToLower(a) < io::ToLower(b); });
        return out;
    }

    void App::MoveTier(int from, int to) {
        if (ed.preset && model::MoveTier(*ed.preset, from, to)) {
            ed.slot = to;
        }
    }

    void App::ApplyLintFix(const model::LintIssue& issue) {
        switch (issue.fix.kind) {
            case model::LintFix::Kind::MoveAbove:
                if (issue.slot > issue.fix.targetSlot) {
                    MoveTier(issue.slot, issue.fix.targetSlot);
                }
                break;
            case model::LintFix::Kind::EnablePlugin:
                game::MtfBridge::Get().SetPluginEnabled(issue.fix.pluginId, true);
                Toast("Enabling " + issue.fix.pluginId + " in MTF");
                break;
            default:
                break;
        }
    }

    bool App::Save() {
        if (!ed.preset) {
            return false;
        }
        auto& p = *ed.preset;
        if (ed.builtin) {
            Toast("Built-in presets are read-only. Use Duplicate to make your own copy.", true);
            return false;
        }
        if (p.name.empty()) {
            p.name = UniqueName(p.displayName);
        }
        if (BuiltinNameTaken(p.name)) {
            Toast("'" + p.name + "' is reserved by an MTF built-in preset. Duplicate it under another name.", true);
            return false;
        }
        const auto path = cat.PresetPath(p.name);
        std::string error;
        if (!io::Backup(path, paths.uiDir / "backups", 10, &error)) {
            // The edit is still worth saving; the user just can't roll this one back.
            logger::warn("No backup of {} before save: {}", p.name, error);
            Toast("Could not back up the previous version: " + error, true);
        }
        const auto json = model::PresetToJson(p);
        if (!io::WriteJsonStyled(path, json, &error)) {
            Toast("Save failed: " + error, true);
            return false;
        }
        p.raw = json;
        p.schemaVersion = model::kSchemaVersion;
        p.hidden = 0;
        ed.saved = p;
        ed.committed = p;
        ed.isNew = false;
        DiscardDraft();
        settings.lastPreset = p.name;
        SaveSettings();
        ReloadLibrary();
        const std::string name = p.name;
        const std::string display = p.displayName;
        // Hidden by the live preview: it goes back on, edits included, when the preview stops.
        const bool hidden = game::LivePreview::Get().IsSuspended(targetId, name);
        const bool wearing = IsApplied(name) && !hidden;
        // The target can change before MTF answers; refresh the one that was wearing it.
        const auto target = targetId;
        const auto who = targetName;
        game::MtfBridge::Get().PresetChanged(name, false, [this, name, display, wearing, hidden, target, who] {
            if (hidden) {
                Toast("Saved " + display + ". " + who + " shows it again when the live preview stops");
            } else if (wearing) {
                // MTF redraws on tier changes only; re-applying shows the edit right away.
                game::MtfBridge::Get().Remove(target, name, [this, target, name, display, who] {
                    game::MtfBridge::Get().Apply(target, name, [this, target, display, who](int rc) {
                        Toast(rc == 1 || rc == 0 ? "Saved " + display + " and refreshed it on " + who : "Saved, but re-applying failed: " + game::ApplyResultText(rc, target == 0x14), rc < 0);
                        RefreshApplied();
                    });
                });
            } else {
                Toast("Saved " + display);
            }
        });
        logger::info("Saved preset {}", name);
        return true;
    }

    void App::Revert() {
        if (!ed.preset) {
            return;
        }
        if (ed.isNew) {
            // Nothing on disk to go back to: this throws the preset away, and undo can't bring it back.
            pendingAction_ = [] {};
            confirmOpen_ = true;
            return;
        }
        ed.undo.push_back(*ed.preset);
        *ed.preset = ed.saved;
        ed.committed = ed.saved;
        draftPending_ = true;
        Relint();
    }

    void App::Undo() {
        if (!ed.preset || ed.undo.empty()) {
            return;
        }
        ed.redo.push_back(*ed.preset);
        *ed.preset = ed.undo.back();
        ed.undo.pop_back();
        ed.committed = *ed.preset;
        draftPending_ = true;
        Relint();
    }

    void App::Redo() {
        if (!ed.preset || ed.redo.empty()) {
            return;
        }
        ed.undo.push_back(*ed.preset);
        *ed.preset = ed.redo.back();
        ed.redo.pop_back();
        ed.committed = *ed.preset;
        draftPending_ = true;
        Relint();
    }

    void App::UseEntry(int slot, const std::string& packId, const std::string& entryId) {
        if (!ed.preset || slot < 0 || slot >= static_cast<int>(ed.preset->slots.size())) {
            return;
        }
        auto& s = ed.preset->slots[slot];
        s.packId = packId;
        s.entryId = entryId;
        settings.PushRecent(packId + "/" + entryId);
        SaveSettings();
    }
}
