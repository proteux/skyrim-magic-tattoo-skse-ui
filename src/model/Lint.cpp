#include "model/Lint.h"

#include "io/UiSettings.h"
#include "model/Describe.h"

#include <algorithm>
#include <set>

namespace mtfui::model {
    namespace {
        // "at least" thresholds: a lower bound on an earlier tier makes a later, higher bound unreachable.
        bool IsAtLeast(const CatalogEntry& e) {
            const auto d = io::ToLower(e.description);
            const auto l = io::ToLower(e.params[0].label);
            return d.find("at least") != std::string::npos || l.rfind("min", 0) == 0;
        }

        // True when every activation of `later` also satisfies `earlier`.
        bool Covers(const Catalogs& cat, const ConditionItem& earlier, const ConditionItem& later) {
            if (!IEquals(earlier.pluginId, later.pluginId)) {
                return false;
            }
            if (earlier == later) {
                return true;
            }
            const auto* entry = cat.FindCondition(earlier.pluginId);
            if (!entry || entry->params[0].kind != ParamKind::Slider || !IsAtLeast(*entry)) {
                return false;
            }
            if (ParamStr(earlier.param2) != ParamStr(later.param2) || ParamStr(earlier.param3) != ParamStr(later.param3)) {
                return false;
            }
            return ParamInt(earlier.param) <= ParamInt(later.param);
        }

        // Earlier tier is active whenever later is: earlier's requirement set is implied by later's.
        bool Shadows(const Catalogs& cat, const Slot& earlier, const Slot& later) {
            if (!earlier.Configured() || !later.Configured()) {
                return false;
            }
            auto implied = [&](const ConditionItem& need) {
                for (const auto& have : later.conditions) {
                    if (Covers(cat, need, have)) {
                        return true;
                    }
                }
                return false;
            };
            if (earlier.op == 0 || earlier.conditions.size() == 1) {
                // AND: every earlier item must be implied by later (later must be AND too, or a single item).
                if (later.op == 1 && later.conditions.size() > 1) {
                    return false;
                }
                for (const auto& need : earlier.conditions) {
                    if (!implied(need)) {
                        return false;
                    }
                }
                return true;
            }
            // Earlier is OR: shadowed if any later item alone implies one earlier item, for every later branch.
            if (later.op == 1 && later.conditions.size() > 1) {
                for (const auto& have : later.conditions) {
                    bool hit = false;
                    for (const auto& need : earlier.conditions) {
                        hit = hit || Covers(cat, need, have);
                    }
                    if (!hit) {
                        return false;
                    }
                }
                return true;
            }
            for (const auto& need : earlier.conditions) {
                if (implied(need)) {
                    return true;
                }
            }
            return false;
        }

        bool HasContent(const Slot& s) {
            return !s.packId.empty() || !s.effects.empty() || s.pulse.Enabled() || !s.name.empty();
        }

        void CheckParams(const CatalogEntry& entry, const std::array<ParamValue, 5>& values, int count, int slot, const std::string& what,
            std::vector<LintIssue>& out) {
            for (int i = 0; i < count; ++i) {
                const auto& spec = entry.params[i];
                if (spec.kind == ParamKind::Slider) {
                    const auto v = ParamInt(values[i]);
                    if (v < spec.min || v > spec.max) {
                        out.push_back({ Severity::Warning, slot,
                            what + ": " + (spec.label.empty() ? "value" : spec.label) + " is " + std::to_string(v) + ", outside " +
                                std::to_string(spec.min) + " to " + std::to_string(spec.max) });
                    }
                } else if (spec.kind == ParamKind::Menu) {
                    const auto v = ParamStr(values[i]);
                    bool found = false;
                    for (const auto& o : spec.menu) {
                        found = found || IEquals(o.id, v);
                    }
                    if (!found) {
                        out.push_back({ Severity::Warning, slot, what + ": '" + v + "' is not an option for " + spec.label });
                    }
                }
            }
        }
    }

    std::vector<LintIssue> LintPreset(const Catalogs& cat, const Preset& preset, const LintContext& ctx) {
        std::vector<LintIssue> out;
        if (SanitizePresetName(preset.name) != preset.name || preset.name.empty()) {
            out.push_back({ Severity::Error, -1, "File name must be 1-32 characters of letters, digits, '_' or '-'" });
        }
        if (preset.displayName.empty()) {
            out.push_back({ Severity::Info, -1, "Display name is empty; MTF will show the file name" });
        }

        const int slotCount = std::min<int>(kSlotCount, static_cast<int>(preset.slots.size()));
        std::set<std::string> overflowReported;
        for (int i = 0; i < slotCount; ++i) {
            const Slot& s = preset.slots[i];
            const std::string label = SlotLabel(preset, i);

            if (i > 0 && !s.Configured()) {
                if (HasContent(s)) {
                    out.push_back({ Severity::Warning, i, label + " has a look or effects but no condition, so it never activates", LintTab::Condition });
                }
                continue;
            }

            for (int j = 1; j < i; ++j) {
                if (Shadows(cat, preset.slots[j], s)) {
                    out.push_back({ Severity::Warning, i,
                        label + " can never activate: " + SlotLabel(preset, j) + " is higher in the list and always matches first. Move it up.",
                        LintTab::Condition, { LintFix::Kind::MoveAbove, j, {} } });
                    break;
                }
            }

            for (const auto& c : s.conditions) {
                const PluginCatalog* owner = nullptr;
                const auto* entry = cat.FindCondition(c.pluginId, &owner);
                if (!entry) {
                    out.push_back({ Severity::Error, i, "Condition '" + c.pluginId + "' is not installed; this tier will not activate", LintTab::Condition });
                    continue;
                }
                if (ctx.pluginEnabled && !ctx.pluginEnabled(owner->pluginId)) {
                    out.push_back({ Severity::Warning, i, owner->label + " is disabled in MTF; '" + entry->label + "' will never be true", LintTab::Condition,
                        { LintFix::Kind::EnablePlugin, -1, owner->pluginId } });
                }
                const auto before = out.size();
                CheckParams(*entry, ConditionParams(c), 3, i, entry->label, out);
                if (entry->params[0].kind == ParamKind::Text && ParamStr(c.param).empty()) {
                    out.push_back({ Severity::Info, i, entry->label + ": text is empty, which usually matches anything" });
                }
                for (auto k = before; k < out.size(); ++k) {
                    out[k].tab = LintTab::Condition;
                }
            }

            const auto lookStart = out.size();
            const auto vis = ResolveVisual(preset, i);
            if (!vis.inherited && !s.packId.empty() && s.packId != kNoTexture) {
                const auto* pack = cat.FindPack(s.packId);
                if (!pack) {
                    out.push_back({ Severity::Error, i, "Tattoo pack '" + s.packId + "' is not installed" });
                } else if (s.entryId.empty()) {
                    out.push_back({ Severity::Warning, i, "A pack is chosen but no tattoo; nothing will show" });
                } else if (const auto* e = pack->Find(s.entryId); !e) {
                    out.push_back({ Severity::Error, i, "Tattoo '" + s.entryId + "' is missing from " + pack->label });
                } else {
                    if (ctx.textureExists) {
                        for (const auto& t : e->textures) {
                            if (!t.empty() && !ctx.textureExists(t)) {
                                out.push_back({ Severity::Warning, i, "Texture not found: " + t + " (it may be in a BSA)" });
                            }
                        }
                    }
                    const auto start = ctx.overlayStart.find(pack->area);
                    const auto cap = ctx.overlayCapacity.find(pack->area);
                    if (start != ctx.overlayStart.end() && cap != ctx.overlayCapacity.end() && cap->second > 0 &&
                        start->second + static_cast<int>(e->textures.size()) > cap->second && !overflowReported.contains(pack->area)) {
                        overflowReported.insert(pack->area);
                        out.push_back({ Severity::Warning, i,
                            e->label + " needs " + std::to_string(e->textures.size()) + " " + pack->area + " overlay slot(s) starting at " +
                                std::to_string(start->second) + ", but RaceMenu only has " + std::to_string(cap->second) +
                                ". Raise the overlay count in skee64.ini or lower MTF's start slot." });
                    }
                }
            }

            // Only the layers the tattoo has textures for are drawn; the others' opacity doesn't matter.
            if (const auto* shown = vis.none ? nullptr : cat.FindEntry(vis.packId, vis.entryId); shown && !shown->textures.empty()) {
                const int used = std::min<int>(kLayerCount, static_cast<int>(shown->textures.size()));
                bool anyVisible = false;
                for (int l = 0; l < used; ++l) {
                    anyVisible = anyVisible || s.layers[l].alpha > 0;
                }
                if (!anyVisible) {
                    out.push_back({ Severity::Info, i, used == 1 ? "The tattoo's layer is fully transparent" : "Every layer of the tattoo is fully transparent" });
                }
            }
            for (auto k = lookStart; k < out.size(); ++k) {
                out[k].tab = LintTab::Look;
            }

            if (s.pulse.depth > 0 && s.pulse.rate <= 0.0) {
                out.push_back({ Severity::Info, i, "Pulse depth is set but rate is 0, so it will not pulse", LintTab::Pulse });
            }
            if (!s.pulse.waveform.empty() && !cat.FindWaveform(s.pulse.waveform)) {
                out.push_back({ Severity::Warning, i, "Waveform '" + s.pulse.waveform + "' is not installed; MTF falls back to a smooth pulse", LintTab::Pulse });
            }

            const auto fxStart = out.size();
            for (const auto& fx : s.effects) {
                const PluginCatalog* owner = nullptr;
                const auto* entry = cat.FindEffect(fx.key, &owner);
                if (!entry) {
                    out.push_back({ Severity::Error, i, "Effect '" + fx.key + "' is not installed" });
                    continue;
                }
                if (ctx.pluginEnabled && !ctx.pluginEnabled(owner->pluginId)) {
                    out.push_back({ Severity::Warning, i, owner->label + " is disabled in MTF; '" + entry->label + "' will not run", LintTab::Effects,
                        { LintFix::Kind::EnablePlugin, -1, owner->pluginId } });
                }
                CheckParams(*entry, fx.params, entry->ParamCount(), i, entry->label, out);
            }
            if (static_cast<int>(s.effects.size()) > kEffectCap) {
                out.push_back({ Severity::Info, i, "More than " + std::to_string(kEffectCap) + " effects; MTF's MCM will only show the first " +
                                                        std::to_string(kEffectCap) });
            }
            for (auto k = fxStart; k < out.size(); ++k) {
                out[k].tab = LintTab::Effects;
            }
        }
        return out;
    }
}
