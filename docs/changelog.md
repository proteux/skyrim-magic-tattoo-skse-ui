# Changelog

Versions follow `<major>.<feature>.<patch>`, as described in `AGENTS.md`.

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