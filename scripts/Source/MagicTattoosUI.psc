Scriptname MagicTattoosUI Hidden
{Papyrus functions from MagicTattoosUI.dll. Check SKSE.GetPluginVersion("MagicTattoosUI") before calling them:
 without the DLL, a call fails and logs an error.}

; Lands the spell's effects on akTarget in place, with no animation or projectile from akCaster. Aimed spells
; such as Firebolt hit on the target instead of flying from the caster. Magnitude follows the caster's Fortify
; (School) potions and Mod Spell Magnitude perks. The caster gets the blame for anger, kill credit and crime.
; Since 1.8.3; in-place delivery since 1.8.4.
Function CastAs(Actor akCaster, Spell akSpell, ObjectReference akTarget) global native

; Same as CastAs, then multiplies every effect's magnitude by afScale (0.5 for a half-strength Spell aura hit).
; Since 1.8.6.
Function CastAsScaled(Actor akCaster, Spell akSpell, ObjectReference akTarget, float afScale) global native
