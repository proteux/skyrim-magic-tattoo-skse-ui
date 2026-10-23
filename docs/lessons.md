# Lessons and working notes

Notes for whoever works on Magic Tattoos UI next. Section 1 is carried over from New Day New Duds (NDND) and still applies. The rest is specific to this plugin and to Magic Tattoos Framework (MTF).

---

## 1. Carried over from New Day New Duds

- **Bump `VERSION` on every build that goes into the game, and check the log before testing.** The first line of `Documents/My Games/Skyrim Special Edition/SKSE/MagicTattoosUI.log` names the version. In NDND, "still broken" was often "still the old build".
- **Always fully restart Skyrim after a deploy.** Loading a save does not reload an SKSE DLL. Skyrim also locks the DLL while it runs, so `make live` stops before building when `SkyrimSE.exe` is running or the DLL can't be opened for writing, and says to quit to desktop.
- **Targets:** `make` and `make live` copy straight into `Data`, and `make staging` copies into the Vortex mod folder (then deploy in Vortex). While using `live`, keep the mod disabled in Vortex, or a deploy overwrites the files.
- **Git Bash path mangling:** set `MSYS_NO_PATHCONV=1` around Windows tools (`cmd /c`, `vcvars64.bat`). `build.sh` already does this.
- `make package` builds the Nexus zip with Windows `tar.exe -a`. Git Bash's GNU tar can't write zips, and `Compress-Archive` writes backslash paths.
- `build.sh` refuses to ship anything in `dist/` that isn't ours. Keep that check.
- Data in the SKSE co-save outlives the plugin. MTF keeps applied presets in StorageUtil, so the UI cleans up its own preview preset on the next load and never relies on uninstall hooks.
- Papyrus initial values are saved in the save game. Not a problem here because this plugin has no scripts, but it explains why MTF reads some settings through functions instead of properties.

## 2. Build machine

- No full Visual Studio is needed. The standalone "Build Tools for Visual Studio" with the C++ workload is enough. `tools/msvc.cmd` finds it with `vswhere` and runs `vcvars64.bat` before CMake.
- vcpkg lives at `M:/Repositories/vcpkg` (override with `VCPKG_ROOT`). CMake and Ninja come from `vcpkg fetch`, so they don't need a separate install.
- CommonLibSSE-NG comes from the overlay port in `ports/commonlibsse-ng`. Its `rapidcsv` dependency must be a plain name. A `version>=` constraint conflicts with the registry's version scheme.
- The first configure builds every dependency and takes a long time. Later builds are incremental.

## 3. How MTF stores and applies presets

- Presets are JsonUtil files in `Data/SKSE/Plugins/StorageUtilData/MagicTattoosFramework/presets/<name>.json`. Built-ins live in `presets_builtin/`, and a user file with the same name wins. MTF's `SavePreset` refuses names reserved by built-ins, and so does this UI.
- **Byte-for-byte compatibility matters.** PapyrusUtil writes JSON with jsoncpp's StyledStreamWriter: tab indent, `" : "`, CRLF, no trailing newline, and short arrays on one line. `io/StyledJson.cpp` reproduces it. The tests round-trip every fixture preset and fail on the first differing byte.
- Unknown keys are kept: the typed preset is merged back into the original document on save.
- **Applying an edit to a preset that's in use needs three steps.** First, `JsonUtil.Unload(file, false, false)` drops PapyrusUtil's cached copy. Second, `MTF_MainQuest._invalidateScratchCache(name)` drops MTF's parsed copy. Third, call `LoadPreset(name)` if MTF's own MCM has that preset open. MTF only redraws when the active tier changes, so the UI then removes and re-applies the preset on the target.
- Deleting uses MTF's soft delete: the file stays with `"hidden" : 1`. A backup goes to `SKSE/Plugins/MagicTattoosUI/backups` first. Undelete just writes `"hidden" : 0` back. A new preset may still reuse a hidden preset's file name, the same way MTF reclaims tombstones.
- Backups are `<stem>.<YYYYMMDD-HHMMSS-mmm>.json`, 10 per preset, and a backup identical to the newest one is skipped. Saves, deletes and restores all back up the file they replace. Writes go to a temp file, are flushed, and replace the target with `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
- Unsaved edits go to `MagicTattoosUI/drafts/current.json` at most every 3 seconds, and are deleted on save or discard. A draft found at startup is offered back once; the last preset isn't auto-opened while that offer is up.
- `AddAppliedPreset` return codes: 1 applied, 0 already applied, -2 no target, -3 too many presets, -4 empty name, -5 invalid preset, -6 no free overlay slots, -7 truncated.
- Live preview writes `presets_builtin/_mtfui_preview.json`, so MTF's own preset pickers never list it. The last 32 actors that wore it are saved in `settings.json`, and the preview is removed from them (and the player) on every game load. Papyrus calls are asynchronous, so removing it right before a save can't be guaranteed to finish in time; cleaning up on load is what keeps saves clean. The Applied list shows it as "Live preview (temporary)" so a stuck one can be removed by hand.
- Each preview request carries a generation number. A callback from an older generation (the user stopped or moved on while MTF was applying) removes the preview it put on instead of recording it. If MTF never answers, the preview resets after 10 seconds.
- Tier priority: the lowest tier number wins, and Default applies when no tier matches. On tiers, an empty pack inherits Default's tattoo, and `<none>` hides it.
- Layer *i* of a tier uses texture *i* of the chosen tattoo entry. Only as many layers as the entry has textures are drawn, so the other layer settings do nothing for that tattoo.
- The pulse only animates emission: `mult = (1 - depth) + depth * wave`.

## 4. SKSE Menu Framework

- The header needs `RE/Skyrim.h` included before it (see `src/pch.h`).
- Register in `kPostLoad`, not in `SKSEPluginLoad`. SKSE loads plugins alphabetically, and `MagicTattoosUI.dll` loads before `SKSEMenuFramework.dll`.
- The window's "pause game" flag is fixed when the window is registered, so changing it takes a restart.
- The Mod Control Panel can leave the game running, and menu input doesn't count as player activity. Skyrim's idle orbit camera (`fAutoVanityModeDelay:Camera`) then starts circling the target. `App` raises that setting while the editor is drawn and restores it on menu close.
- Live preview needs the game unpaused, since MTF redraws through Papyrus and the pulse runs on game time. "Hold in place" switches the NPC target's AI off while the editor is drawn, so they don't wander off. Only AI the UI switched off is switched back on, and the held actor is saved in `settings.json` so the next load releases it if the game quit with the menu open.
- One `AddWindow` render callback can open several top-level windows (`Begin`/`End` pairs). The split layout does this, and remembers panel rects in `settings.json` instead of relying on the framework's ImGui ini. The first frame logs which ini file and whether docking is on.
- The menu section and the standalone window are two callbacks in the same ImGui frame. `BeginFrame`, `CommitFrame` and `DrawModals` remember the frame number and run once per frame, whichever callback gets there first; otherwise shortcuts, undo checkpoints and the texture cache's frame counter all ran twice. In tabbed mode the section doesn't draw the editor while the window is open.
- Shortcuts are ignored while a text box has focus or any popup is open.
- After a game load, `MtfBridge::NewSession` drops every queued callback from before the load, and callbacks that arrive later from old Papyrus calls are dropped too.
- Textures are `ID3D11ShaderResourceView*` cast to `ImTextureID`. The device comes from `RE::BSGraphics::Renderer::GetDevice()`.
- All Papyrus calls happen on the game thread through SKSE tasks. Results come back to the UI through a queue drained at the start of each frame (`MtfBridge::PostToUi` / `DrainUi`). UI state is never touched from the game thread.

## 5. Textures

- Tattoo DDS files are 4096² and block-compressed. Loading the whole file for a thumbnail costs about 20 MB each. `DdsCache` reads only the header, seeks past the mips that are too big, and builds a small DDS in memory from the tail.
- Textures load from loose files and BSAs alike through `BSResourceNiBinaryStream`, on a worker thread. The lint only reports a missing texture after the loader has tried and failed, because BSA contents can't be checked with the filesystem.
- The header shrinking lives in `io/DdsHeader.cpp` with no DirectX dependency, so the offline tests cover it.
- The cache has a 256 MB budget, estimated from each texture's size and format. Over budget it evicts least recently used textures, never ones drawn in the last two frames, down to three quarters of the budget.
- A shader resource view may still be referenced by the frame's draw lists after the UI drops it, so `Clear` and eviction retire views and `EndFrame` releases them two frames later. At process exit the destructor only detaches the worker thread: joining or releasing then could hang or crash.

## 6. Code layout

- `App.cpp` holds state, targets, preview, lint triggers and the per-frame hooks; `AppEditor.cpp` the open preset (load, save, undo, drafts, restore, import); `AppLayout.cpp` the window, panels, status line and modals. `game/Camera.cpp` holds the idle camera setting.
- Logic that doesn't need the game or ImGui goes in `model/` or `io/`, which build into `mtfui_core` and are tested offline: tier moves and look copying (`Preset.cpp`), search, slider formats and unique names (`Text.cpp`).
