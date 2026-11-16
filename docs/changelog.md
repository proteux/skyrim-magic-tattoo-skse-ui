# Changelog

Versions follow `<major>.<feature>.<patch>`, as described in `AGENTS.md`.

Version 1.8.14

New: `MagicTattoosUI.GetSlotPreset` and `IsSlotPermanent` for Magic Tattoos Tactical cast scripts. The Spells tab also checks `MTFT_LayoutVersion` from the ESP against the UI's layout fixture, and hides itself if they disagree.

Version 1.8.13

Change: the Hex and Blessing radio tooltips explain when a Hex counts as an attack, that a sneak mark can land quietly, and that Blessings stay friendly.

Version 1.8.12

Change: the spell picker defaults to "Known by player", so it opens on spells the character knows instead of the full load order.

Version 1.8.11

Change: Magic Tattoos Tactical Hex effects no longer use the engine Hostile flag. Tactical blames the caster in script when a Hex lands and the caster isn't hidden, so a sneak hex can mark without aggro; open casts and Blessings behave as before.

Version 1.8.10

Change: the Spells tab warns when two tattoo spells share a preset (the last cast owns the tattoo). Saving spells toasts when a cleared preset made the character forget a spell, or when a cast-type change swapped the known version so it needs re-equipping.

Version 1.8.9

Change: the spell picker's concentration tip matches MTF Plugin - Skyrim 0.3.5: each pulse is about ten seconds of the spell, the wearer always pays magicka each second, and empty magicka pauses the channel until it recovers.

Version 1.8.8

Change: Magic Tattoos Tactical spells take their hand glow, light, projectile and hit FX from a vanilla spell that matches the school and whether they're a Hex or a Blessing, so a Restoration blessing no longer looks like an Illusion hex in your hands.

Version 1.8.6

New: `MagicTattoosUI.CastAsScaled`, which lands a spell like `CastAs` then multiplies magnitude (used by MTF Plugin - Skyrim Spell aura for half-strength fire-and-forget hits). The spell picker hides awkward spells (cloaks, wards, summons, caster-only, corpse) by default, with a "Show awkward spells" toggle to bring them back.

Version 1.8.5

Fix: `CastAs` no longer crashes when reading Fortify (School) power on AE 1.6.1170 (ActorValueOwner must be reached through `AsActorValueOwner`).

Version 1.8.4

Change: `MagicTattoosUI.CastAs` puts the spell's effects on the target in place. Aimed spells such as Firebolt no longer fly from the caster; a hex detonates on the victim, still counting the caster's Fortify (School) potions and Mod Spell Magnitude perks, with kill credit and aggro going to the caster.

Version 1.8.3

New: a Papyrus function, `MagicTattoosUI.CastAs`, that lands a spell on a target as if an actor cast it, counting that actor's perks and Fortify (School) potions. MTF Plugin - Skyrim 0.3.1 uses it, so a Cast a spell or Spell aura on a Magic Tattoos Tactical hex hits as hard as the caster's own spell. The package now has `Scripts/MagicTattoosUI.pex`.

Version 1.8.2

Change: the spell picker leaves out Magic Tattoos Tactical's own spells. A tier that cast one would put a tattoo on again and again.

Version 1.8.1

Change: the spell picker is a table with each spell's school, level (Novice to Master), type (concentration or fire and forget, and how it's delivered), base magicka cost, and whether it's harmful or helpful. Click a column to sort by it. The search also matches those details, so "adept concentration" or "helpful" narrows the list. Hovering a spell, or the chosen one, shows its effects with magnitude, area, and duration, how it behaves in Cast a spell and Spell aura, and the plugin it comes from. Cloaks, summons, bound weapons, wards, spells for corpses, and spells that only work through their caster, such as Telekinesis and Candlelight, are marked in orange with the reason they work poorly from a tattoo. The plugin name no longer fills each row.

Version 1.8.0

New: a spell picker for plugin effects that take a spell, such as Cast a spell and Spell aura in MTF Plugin - Skyrim. It lists every spell in your load order with its school and plugin, with a search box, a school filter, and a "Known by player" filter. A spell whose plugin has been removed is flagged in the preset's checks.

Version 1.7.1

Change: the Spells tab is easier to read. Each spell gets its own box, and only the spells you've set up are listed; New spell adds the next one, with a count of how many of the 20 are in use. Remove clears a spell, and saving makes this character forget it.
Change: the Spells tab's preset menu is grouped like the library, with a submenu per group, then ungrouped presets, then built-ins. A * marks the submenu holding the chosen preset.

Version 1.7.0

New: a Spells tab for Magic Tattoos Tactical, shown when its plugin is loaded. Set up to 20 hex and blessing spells that put a preset on whoever they hit (self, touch or ranged), with their name, magicka cost, school, and how long the tattoo lasts, plus a Dispel spell. Learn and forget each spell for the current character from the tab.

Version 1.6.8

Fix: applied tattoos that were being previewed would get dropped when the menu closed
New: gradient effects blend color settings from a low-end color to a high-end color, such as RaceMenu's hair color turning from brown to white as health drops.


Version 1.6.6

New: Gradient tiers builds a ladder of tiers from health, magicka, stamina or any threshold condition, quickly building all of the tiers necessary.
New: gradients can ramp tattoo tint.
New: gradients can ramp glow strength, opacity, pulse speed, depth and pause, and switch pulse shape at the low end.
New: gradients can ramp effects too, such as RaceMenu's actor scale growing as health drops.
New: gradient settings are saved in the preset and reopen with it.
New: presets can have more than 7 tiers, up to MTF's iMaxConditions. Extra tiers work in game but only show in this editor. (Do not save them in the MTF MCM, they will drop all tiers after 7)
New: a check warns about tiers past MTF's iMaxConditions limit.
Fixed: text boxes that confirm on Enter losing what you typed when you click away.


Version 1.5.5

Added Translations: The UI now uses Skyrim's language setting, with English, French, German, Italian, Russian, and Spanish included. All text is translatable: menus, the tier sentence summaries, the checks panel, and messages.
Font warning: If the menu font can't show a language's characters (Russian, for example), the UI says so and explains which font setting to change.
License files: The download now includes the GPL-3.0 license and a readme with the copyright notice and source link, in `Data/SKSE/Plugins/MagicTattoosUI`.
For translators: Translation files are UTF-16 LE text files in `Interface/Translations`, named `MagicTattoosUI_<LANGUAGE>.txt`. To add a language, copy the English file and translate the values. The README has the details.
Typographic characters that the menu font can't draw, such as curly quotes and dashes, are swapped for plain ASCII when the text is displayed.
The translation work is _very_ rough, please report issues


Version 1.4.3

New: the General tab shows which overlay slots your tattoos use, and lets you move them off other mods' overlays.
New: support for more overlay slots, which now also works on existing saves.
New: the preset loaded in MTF's MCM now shows under Applied and can be cleared there. An * on the Applied button means one is active.
Fixed: tattoos disappearing after moving slots, after loading some saves, or when newly applied.


Version 1.2.12

Holding a follower was resulting in them waiting even after you left the zone. Hold now immobilizes them (still plays animations so that MTF updates), but doesn't tell them to Wait