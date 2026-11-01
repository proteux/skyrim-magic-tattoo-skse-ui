# Agent notes

## Versioning

- **Bump `VERSION` whenever you change the plugin.** Every build that may go into the game gets a new version, so the first line of `MagicTattoosUI.log` shows which build is running.
- The format is `<major>.<feature>.<patch>`:
  - **major** is only for breaking changes (for example, presets or saved data that older builds can't read). Never bump it without the user asking.
  - **feature** is for a genuinely new feature. Reset patch to 0.
  - **patch** is for any update to an existing feature: fixes, tweaks, extensions, and follow-up work on a feature that's already in. Most changes are patches.
- Each number can go past 9, so `1.56.3` or `1.6.14` is fine. Don't roll a long run of patches over into a feature bump.
- `VERSION` is the only place the version lives. CMake, the DLL's version info, the log, the status line, the Vortex `meta.ini`, and the package name all read it.
- Bump once per change you hand to the user, not once per rebuild while you're still working on it.

## Building

- **Always install with `make live`** (plain `make` does the same), which builds, runs the tests, and copies straight into the game's `Data` folder. Don't use `make staging` (the Vortex mod folder): the extra Vortex deploy step wastes the user's time.
- `make test` builds and runs the offline tests only, and `make package` writes the release zip. See `README.md` and `build.sh`.
- The tests must pass before installing. They check that presets round-trip byte for byte against copies of real MTF data in `tests/fixtures`.
- Skyrim locks the DLL while it runs. `make live` checks for that before building and exits with "Quit Skyrim to desktop, then rerun." Ask the user to quit, then retry.
- After `make live`, the user only has to restart Skyrim. Keep the mod disabled in Vortex, or a Vortex deploy overwrites the live files.

## More context

`docs/lessons.md` explains how MTF stores and applies presets, the SKSE Menu Framework quirks, texture loading, and the lessons carried over from New Day New Duds.
