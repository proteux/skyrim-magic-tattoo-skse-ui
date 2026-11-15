Scriptname MagicTattoosUI Hidden
{Papyrus functions from MagicTattoosUI.dll. Check SKSE.GetPluginVersion("MagicTattoosUI") before calling them:
 without the DLL, a call fails and logs an error.}

; Lands the spell on akTarget as if akCaster cast it, with no animation or projectile. The caster's perks,
; Fortify (School) potions and the difficulty setting count, and the caster gets the blame for anger, kill
; credit and crime. Since 1.8.3.
Function CastAs(Actor akCaster, Spell akSpell, ObjectReference akTarget) global native
