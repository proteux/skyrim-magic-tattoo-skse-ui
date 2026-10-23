# Magic Tattoos UI

A friendlier editor for Magic Tattoos Framework, with previews.

## Description

[Magic Tattoos Framework](https://github.com/levchimik/magic-tattoos-framework) (MTF) makes tattoos react to what is happening to the wearer: arousal, combat, magicka, the time of day, and anything else its plugins can detect. Setting it up through MTF's own menu means scrolling through long pages of sliders and remembering which tier is which.

Magic Tattoos UI replaces those pages with one window in the [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) menu. You see a whole preset at once as a ladder of tiers, each described in a plain sentence. You pick tattoos from a thumbnail browser and watch the result on a 2D preview, or live on yourself or an NPC, before you save.

It edits the same preset files MTF uses, so everything you make works with MTF's own menu, its mod events, and other mods that apply presets.

## Requirements

- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
- [Magic Tattoos Framework](https://github.com/levchimik/magic-tattoos-framework) and its requirements (RaceMenu, PapyrusUtil SE)

Built for Skyrim SE/AE 1.6.1170 with CommonLibSSE-NG, which also supports 1.5.97.

## Installation

### Install with Vortex

1. Install the requirements above.
2. Drag the downloaded archive onto Vortex's **Mods** page, enable **Magic Tattoos UI**, and click **Deploy**.

### Manual install

Extract the archive into `Skyrim Special Edition/Data`, so that `SKSE/Plugins/MagicTattoosUI.dll` ends up inside `Data`.

There is no plugin file, so it takes no load order slot.

## Opening it

Open the SKSE Menu Framework menu (F1 by default) and choose **Magic Tattoos**. Click **Open editor windows** to edit presets over the game. Presets, Tiers, and Tier editor open as separate panels you can drag around the target, and a strip at the top holds the target picker and live preview. Holding **Hold to peek** on the strip hides the panels for a moment. The layout is remembered. The tattoo browser, General, and Plugins stay as tabs in the menu. General > This menu has the panel opacity, a layout reset, and a switch back to one tabbed window. Esc closes the editor windows.

## Main features

### Presets

- **The whole preset on one screen.** Tiers are listed in priority order, top wins, with Default at the bottom. Each one has a sentence summary like "When the wearer is at least 50% aroused: Dragon Coil, pulsing at 1.2/s, 2 effects", plus a thumbnail of its tattoo.
- **Drag tiers to reorder them**, or select one and press Alt+Up / Alt+Down. Right-click a tier to move it, clear it, copy another tier's look, or copy it and paste it into another tier or preset, whole or just its look, conditions, effects, or timing.
- **Copy and paste layers and pulses.** Right-click a layer header to copy, paste, or use its colors on every layer, and use **Copy pulse** / **Paste pulse** on the Pulse tab. Color pickers have a **Palette** button with MTF's named colors.
- **An inspector for the selected tier** with Condition, Look, Pulse, Effects, and Timing tabs. Every condition and effect comes from the installed plugin catalogs, with its own sliders and menus, and is described in plain words as you edit it.
- **Built-in checks.** The editor warns about mistakes MTF won't tell you about:
  - a tier that can never activate because a higher tier always wins first (for example "at least 50%" above "at least 80%");
  - a tier without a condition;
  - plugins that are missing or turned off;
  - tattoos or textures that aren't installed;
  - layers that overflow your RaceMenu overlay slots;
  - a pulse that has nothing to animate.

  Click a check's tier button to jump to the tab where it can be fixed. Some checks have a one-click fix: **Move it up** for a tier hidden by a higher one, and **Enable it** for a plugin that's turned off.
- **Undo and redo** (Ctrl+Z, Ctrl+Y), **Save** (Ctrl+S), and **Revert**. Every save keeps a backup of the previous file.
- **Your work isn't lost.** Opening another preset, starting a new one, or reverting a new one asks whether to save, discard, or cancel first. Unsaved edits are also written to a draft every few seconds, and if the game closes before you save, the editor offers to restore them next time.
- **Shortcuts:** Ctrl+S save, Ctrl+Z / Ctrl+Y undo and redo, Ctrl+N new preset, Ctrl+D duplicate, Ctrl+F search presets, F5 apply to the target, Alt+Up / Alt+Down move the selected tier. Hover the version in the status line for the list.

### Tattoos

- **A thumbnail browser** for every installed visual pack, with search, filters for area, subject, style, and placement, favorites, and a recently used list. Right-click a tattoo to start a new preset with it as the Default look.
- Each tattoo shows a large preview in the selected tier's colors, its details, and which of your presets already use it.
- Double-click a tattoo, or use **Use for...**, to put it on a tier.

### Previews

- **2D preview** on the Look tab. It shows the tattoo over the target's skin texture with each layer's tint, opacity, and glow, and animates the pulse with the chosen waveform. Scroll to zoom, drag to pan, and double-click to reset.
- **Live preview** shows the tier you're editing on the selected target in the game while you edit it. It updates a moment after each change. Turn on **with effects** to also run the tier's effects. The preview is removed when you close the menu, and also on the next load if the game exits first.

### Targets

- Pick a target at the top of the window: yourself, the actor under your crosshair, the console selection, or anyone nearby.
- **Apply to target** puts the open preset on them for real, saving it first if needed. The **Applied** list shows what they're wearing and lets you open or remove each preset.
- Saving a preset that the target is wearing refreshes it on them right away.

### General and Plugins

- MTF's settings: on/off, debug messages, update interval, rescan visual packs, and the SkyrimNet option when SkyrimNet is installed.
- **An overlay slot map** for body, face, hands, and feet. It shows which RaceMenu overlay slots MTF uses, compared with how many your `skee64.ini` provides.
- **Plugins:** turn each MTF plugin on or off, see what conditions and effects it provides, and which of your presets use it.
- **Deleted presets and backups:** restore a deleted preset, or any of the last 10 backups of each preset, from the General tab. Restoring backs up the file it replaces first. **Copy as JSON** (under Preset options) and **Paste a preset from the clipboard** share presets as text.

## Notes

- Built-in presets that ship with MTF are read-only. Use **Duplicate** to make your own copy.
- Deleting a preset works like MTF's own delete: the file is kept but hidden, and General > Deleted presets brings it back. A backup also goes to `Data/SKSE/Plugins/MagicTattoosUI/backups`.
- The UI keeps its own settings in `Data/SKSE/Plugins/MagicTattoosUI/settings.json` and unsaved drafts in `drafts/` next to it. It never changes MTF's files except the presets you save, delete, or restore.
- Saving the game while an NPC is held in place releases them first, so a frozen NPC is never written into the save.

## Troubleshooting

The log is at `Documents/My Games/Skyrim Special Edition/SKSE/MagicTattoosUI.log`. Include it when reporting a problem.

- **"Magic Tattoos Framework is not loaded."** `MagicTattoosFramework.esp` isn't enabled. You can still edit preset files, but applying, previews, and settings need MTF.
- **"No free overlay slots" when applying.** Lower MTF's overlay start on the General tab, or raise `iNumOverlays` in `skee64.ini`.
- **A tattoo stays glowing after its tier ended.** Use **Clear stuck effects** on the General tab, or in the **Applied** list.

## Building from source

Needs Visual Studio 2022 or later (or the standalone Build Tools with the C++ workload) and [vcpkg](https://github.com/microsoft/vcpkg). Set `VCPKG_ROOT` if vcpkg isn't at the default path in `build.sh`.

```
make            # same as make live
make live       # build, run tests, copy straight into Skyrim's Data folder
make staging    # build, run tests, copy to the Vortex staging folder
make test       # build and run the offline tests only
make package    # dist/MagicTattoosUI-<version>.zip
```

`make live` stops before building if Skyrim is running, because Skyrim locks the DLL. Quit to desktop and rerun it. While using `live`, keep the mod disabled in Vortex, or a Vortex deploy overwrites the files. `SKYRIM_DIR` and `VORTEX_STAGING` override the default install paths. The offline tests in `tests/` check that saved presets match PapyrusUtil's JSON output byte for byte, using copies of real presets in `tests/fixtures`. They also cover the checks, tier copy and move, waveforms, the DDS header shrinking, settings, and backups.

See `docs/lessons.md` for how MTF applies preset changes and other notes for working on this.

## Credits

- levchimik, for Magic Tattoos Framework
- Thiago099, for SKSE Menu Framework
- The CommonLibSSE-NG contributors
