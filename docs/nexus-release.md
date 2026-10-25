# Nexus release checklist

Everything to paste into the Nexus Mods upload form for Skyrim Special Edition. Vortex installs from Nexus, so publishing there is what makes the mod installable with **Mod Manager Download**.

## Build the archive

```
make package    # builds, runs the tests, writes dist/MagicTattoosUI-<version>.zip
```

The zip holds only `SKSE/Plugins/MagicTattoosUI.dll`, at the root where Vortex expects it. The `.pdb` is left out on purpose.

## Mod details

| Field | Value |
| --- | --- |
| Game | Skyrim Special Edition |
| Name | Magic Tattoos UI |
| Author | Jack Whitehouse |
| Version | Same as `VERSION` |
| Category | User Interface |
| Adult content | Yes, if MTF itself is only on adult sites, because the arousal conditions come from its plugins. Otherwise No. |
| Language | English |

**Brief overview** (the short summary, 350 characters max):

> A friendlier editor for Magic Tattoos Framework in the SKSE Menu Framework menu. See a whole preset as a ladder of tiers in plain sentences, pick tattoos from a thumbnail browser, preview them in 2D or live on yourself or an NPC, and catch mistakes MTF won't tell you about.

**Detailed description:** paste `docs/nexus-description.bbcode`. The Nexus editor has a BBCode toggle (the `[ ]` button) — switch to it before pasting.

**Tags:** User Interface, SKSE, Tattoos, Overlays, Body, Utilities.

## Requirements tab

Add as Nexus requirements:

| Mod | Notes |
| --- | --- |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | |
| [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) | Hosts the editor window |
| [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080) | Required by MTF |
| [PapyrusUtil SE](https://www.nexusmods.com/skyrimspecialedition/mods/13048) | Required by MTF |

Add as off-site requirements:

| Name | URL | Notes |
| --- | --- | --- |
| SKSE64 | https://skse.silverlock.org/ | |
| Magic Tattoos Framework | https://github.com/levchimik/magic-tattoos-framework | The framework this edits |

## Files tab

| Field | Value |
| --- | --- |
| File | `dist/MagicTattoosUI-<version>.zip` |
| File name | Magic Tattoos UI |
| Category | Main Files |
| Version | Same as `VERSION` |
| Remove the old file? | Yes, move previous versions to Old Files |

**File description:**

> SKSE plugin. Install with Vortex or extract into Data. Built for Skyrim SE/AE 1.6.1170 and 1.5.97.

## Permissions

The source is GPL-3.0 (see `LICENSE`), so the permissions should allow it:

- Upload to other sites: Yes, with credit
- Convert to other games: Yes
- Modify and release: Yes, under GPL-3.0
- Use assets in other mods: Yes
- Use in paid mods: No
- Source code: link the GitHub repo in the description or the mod page's source link

## Images

Nexus wants at least one image, and the first one is the thumbnail on listings. Take these in game at 1920x1080 or larger:

1. **Thumbnail:** the editor windows open over a character wearing a glowing tattoo, with the tier ladder visible.
2. The tier ladder with its sentence summaries and the checks panel showing a warning.
3. The tattoo browser with filters and a large preview.
4. The Look tab with the 2D preview, beside the live preview on the character.
5. The preset library with groups expanded.
6. The General tab overlay slot map.

## After publishing

- Put the Nexus mod ID into the Vortex `meta.ini` block in `build.sh` (`modid=0`) if you want staging builds to show as the Nexus mod.
- Tag the commit: `git tag v<version> && git push --tags`.
