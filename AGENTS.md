# Agent notes

## Versioning

- **Bump `VERSION` whenever you change the plugin.** Every build that may go into the game gets a new version, so the first line of `MagicTattoosUI.log` shows which build is running. Bump the patch number (`0.1.0` to `0.1.1`) for fixes and small changes, and the minor number for new features.
- `VERSION` is the only place the version lives. CMake, the DLL's version info, the log, the status line, the Vortex `meta.ini`, and the package name all read it.
- Bump once per change you hand to the user, not once per rebuild while you're still working on it.

## Building

- `make test` builds and runs the offline tests, `make` stages to Vortex, `make live` copies into the game's `Data` folder, and `make package` writes the release zip. See `README.md` and `build.sh`.
- The tests must pass before staging. They check that presets round-trip byte for byte against copies of real MTF data in `tests/fixtures`.
- After staging, the user still has to deploy in Vortex and restart Skyrim.

## More context

`docs/lessons.md` explains how MTF stores and applies presets, the SKSE Menu Framework quirks, texture loading, and the lessons carried over from New Day New Duds.
