# Lessons and working notes

Notes for whoever works on Magic Tattoos UI next. Section 1 is carried over from New Day New Duds (NDND) and still applies. The rest is specific to this plugin and to Magic Tattoos Framework (MTF).

---

## 1. Carried over from New Day New Duds

- **Bump `VERSION` on every build that goes into the game, and check the log before testing.** The first line of `Documents/My Games/Skyrim Special Edition/SKSE/MagicTattoosUI.log` names the version. In NDND, "still broken" was often "still the old build".
- **Always fully restart Skyrim after a deploy.** Loading a save does not reload an SKSE DLL. Skyrim also locks the DLL while it runs, so `make live` fails until you quit to desktop.
- **Targets:** `make staging` copies into the Vortex mod folder (then deploy in Vortex), and `make live` copies straight into `Data`. While using `live`, keep the mod disabled in Vortex, or a deploy overwrites the files.
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
- Deleting uses MTF's soft delete: the file stays with `"hidden" : 1`. A backup goes to `SKSE/Plugins/MagicTattoosUI/backups` first.
- `AddAppliedPreset` return codes: 1 applied, 0 already applied, -2 no target, -3 too many presets, -4 empty name, -5 invalid preset, -6 no free overlay slots, -7 truncated.
- Live preview writes `presets_builtin/_mtfui_preview.json`, so MTF's own preset pickers never list it. The list of actors that wore it is saved, and the preview is removed from them on the next game load.
- Tier priority: the lowest tier number wins, and Default applies when no tier matches. On tiers, an empty pack inherits Default's tattoo, and `<none>` hides it.
- Layer *i* of a tier uses texture *i* of the chosen tattoo entry. Only as many layers as the entry has textures are drawn, so the other layer settings do nothing for that tattoo.
- The pulse only animates emission: `mult = (1 - depth) + depth * wave`.

## 4. SKSE Menu Framework

- The header needs `RE/Skyrim.h` included before it (see `src/pch.h`).
- Register in `kPostLoad`, not in `SKSEPluginLoad`. SKSE loads plugins alphabetically, and `MagicTattoosUI.dll` loads before `SKSEMenuFramework.dll`.
- The window's "pause game" flag is fixed when the window is registered, so changing it takes a restart.
- Textures are `ID3D11ShaderResourceView*` cast to `ImTextureID`. The device comes from `RE::BSGraphics::Renderer::GetDevice()`.
- All Papyrus calls happen on the game thread through SKSE tasks. Results come back to the UI through a queue drained at the start of each frame (`MtfBridge::PostToUi` / `DrainUi`). UI state is never touched from the game thread.

## 5. Textures

- Tattoo DDS files are 4096² and block-compressed. Loading the whole file for a thumbnail costs about 20 MB each. `DdsCache` reads only the header, seeks past the mips that are too big, and builds a small DDS in memory from the tail.
- Textures load from loose files and BSAs alike through `BSResourceNiBinaryStream`, on a worker thread. The lint only reports a missing texture after the loader has tried and failed, because BSA contents can't be checked with the filesystem.
