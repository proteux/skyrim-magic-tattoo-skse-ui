#include "game/SpellbookBridge.h"

#include "game/MtfBridge.h"
#include "game/Papyrus.h"
#include "io/JsonStore.h"
#include "io/Translations.h"

namespace mtfui::game {
    namespace {
        constexpr const char* kJsonUtilFile = "MagicTattoosSpellbook/spells";

        // One-shot: copy the pre-rename spells.json if the Spellbook path is empty.
        void MaybeMigrateSpellsJson(const std::filesystem::path& dest) {
            if (std::filesystem::exists(dest)) {
                return;
            }
            const auto legacy = dest.parent_path().parent_path() / "MagicTattoosTactical" / dest.filename();
            if (!std::filesystem::exists(legacy)) {
                return;
            }
            std::error_code ec;
            std::filesystem::create_directories(dest.parent_path(), ec);
            std::filesystem::copy_file(legacy, dest, ec);
            if (ec) {
                logger::warn("Couldn't copy {} to {}: {}", io::PathText(legacy), io::PathText(dest), ec.message());
            } else {
                logger::info("Copied spell setup from MagicTattoosTactical to MagicTattoosSpellbook");
            }
        }

        // A callback from before a save loaded describes a player that is gone; drop it. Never empty.
        template <class T>
        std::function<void(T)> Guarded(std::function<void(T)> fn) {
            return [s = MtfBridge::Session(), fn = std::move(fn)](T value) {
                if (fn && s == MtfBridge::Session()) {
                    fn(std::move(value));
                }
            };
        }

        RE::ActorValue SkillOf(model::School school) {
            static constexpr RE::ActorValue skills[] = { RE::ActorValue::kAlteration, RE::ActorValue::kConjuration, RE::ActorValue::kDestruction,
                RE::ActorValue::kIllusion, RE::ActorValue::kRestoration };
            return skills[std::clamp(static_cast<int>(school), 0, model::kSchoolCount - 1)];
        }

        RE::BGSPerk* PerkOf(model::School school) {
            return RE::TESForm::LookupByID<RE::BGSPerk>(kNovicePerkBase + static_cast<RE::FormID>(school));
        }

        RE::EffectSetting* EffectOf(RE::SpellItem* spell) {
            return spell && !spell->effects.empty() && spell->effects[0] ? spell->effects[0]->baseEffect : nullptr;
        }

        void Name(RE::SpellItem* spell, const std::string& name) {
            if (!spell) {
                return;
            }
            spell->fullName = name.c_str();
            if (auto* effect = EffectOf(spell)) {
                effect->fullName = name.c_str();
            }
        }

        void SetCasting(RE::SpellItem* spell, int cost, model::School school) {
            if (!spell) {
                return;
            }
            spell->data.costOverride = cost;
            spell->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);
            spell->data.castingPerk = PerkOf(school);
            if (auto* effect = EffectOf(spell)) {
                effect->data.associatedSkill = SkillOf(school);
            }
        }

        // Hand glow, light, projectile, hit FX and cast sounds from a vanilla effect. Leaves cast type, delivery,
        // archetype and hostility alone.
        void CopyVisuals(RE::EffectSetting* dst, const RE::EffectSetting* src) {
            if (!dst || !src) {
                return;
            }
            dst->data.light = src->data.light;
            dst->data.effectShader = src->data.effectShader;
            dst->data.enchantShader = src->data.enchantShader;
            dst->data.projectileBase = src->data.projectileBase;
            dst->data.explosion = src->data.explosion;
            dst->data.castingArt = src->data.castingArt;
            dst->data.hitEffectArt = src->data.hitEffectArt;
            dst->data.impactDataSet = src->data.impactDataSet;
            dst->data.enchantEffectArt = src->data.enchantEffectArt;
            dst->data.hitVisuals = src->data.hitVisuals;
            dst->data.enchantVisuals = src->data.enchantVisuals;
            dst->data.imageSpaceMod = src->data.imageSpaceMod;
            dst->data.castingSoundLevel = src->data.castingSoundLevel;
            dst->effectSounds = src->effectSounds;
        }

        void SetVisuals(RE::SpellItem* spell, model::School school, bool hostile, int cast) {
            auto* effect = EffectOf(spell);
            if (!effect || cast < 0 || cast >= model::kCastTypeCount) {
                return;
            }
            const int s = std::clamp(static_cast<int>(school), 0, model::kSchoolCount - 1);
            const auto id = kSpellbookVisuals[s][hostile ? 1 : 0][cast];
            if (auto* src = RE::TESForm::LookupByID<RE::EffectSetting>(id)) {
                CopyVisuals(effect, src);
            } else {
                logger::warn("{}: visual template {:06X} is missing", kSpellbookPlugin, id);
            }
        }
    }

    SpellbookBridge& SpellbookBridge::Get() {
        static SpellbookBridge instance;
        return instance;
    }

    std::string SpellbookBridge::SpellName(const model::SpellbookConfig& config, int index) {
        if (index == kSpellbookDispel) {
            return config.dispel.name.empty() ? std::string(T("$MTFUI_Spells_DispelDefaultName")) : config.dispel.name;
        }
        const auto& name = config.slots[index].name;
        return name.empty() ? TF("$MTFUI_Spells_DefaultName", index + 1) : name;
    }

    void SpellbookBridge::OnDataLoaded(const std::filesystem::path& configFile) {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName(kSpellbookPlugin)) {
            logger::info("{}: not loaded", kSpellbookPlugin);
            return;
        }
        int missing = 0;
        auto spell = [&](RE::FormID id) {
            auto* s = data->LookupForm<RE::SpellItem>(id, kSpellbookPlugin);
            if (!s || !EffectOf(s)) {
                logger::error("{}: spell {:03X} is missing", kSpellbookPlugin, id);
                ++missing;
            }
            return s;
        };
        for (int i = 0; i < model::kSpellbookSlots; ++i) {
            for (int v = 0; v < model::kCastTypeCount; ++v) {
                cast_[i][v] = spell(SpellbookCastSpell(v, i));
            }
            decay_[i] = spell(SpellbookDecaySpell(i));
        }
        for (int d = 0; d < kDispelVersions; ++d) {
            dispel_[d] = spell(SpellbookDispelSpell(d));
        }
        if (missing > 0) {
            logger::error("{}: {} spells are missing, so the Spells tab is off. Is the ESP from an older or newer build?", kSpellbookPlugin, missing);
            return;
        }
        auto* layoutGlobal = data->LookupForm<RE::TESGlobal>(SpellbookLayoutVersionGlobal(), kSpellbookPlugin);
        const int layoutVersion = layoutGlobal ? static_cast<int>(layoutGlobal->value) : -1;
        if (layoutVersion != kSpellbookLayoutVersion) {
            logger::error("{}: layout version is {}, expected {}. Copy tools/EspGen/layout.json into tests/fixtures and rebuild both mods.",
                kSpellbookPlugin, layoutVersion, kSpellbookLayoutVersion);
            return;
        }
        file_ = configFile;
        MaybeMigrateSpellsJson(file_);
        std::string error;
        if (auto j = io::ReadJson(file_, &error)) {
            config_ = model::SpellbookFromJson(*j);
        } else if (std::filesystem::exists(file_)) {
            logger::warn("Couldn't read {}: {}. The spells start from their defaults", io::PathText(file_), error);
        }
        installed_ = true;
        ApplyRecords(config_);
        logger::info("{}: loaded, {} spells set up", kSpellbookPlugin,
            std::ranges::count_if(config_.slots, [](const auto& s) { return !s.preset.empty(); }));
    }

    RE::SpellItem* SpellbookBridge::Version(int index, int cast) const {
        if (cast < 0 || cast >= model::kCastTypeCount) {
            return nullptr;
        }
        if (index == kSpellbookDispel) {
            const auto c = static_cast<model::CastType>(cast);
            return c == model::CastType::SelfChannel ? nullptr : dispel_[model::DispelVersion(c)];
        }
        return index >= 0 && index < model::kSpellbookSlots ? cast_[index][cast] : nullptr;
    }

    void SpellbookBridge::ApplyRecords(const model::SpellbookConfig& config) {
        for (int i = 0; i < model::kSpellbookSlots; ++i) {
            const auto& slot = config.slots[i];
            const auto name = SpellName(config, i);
            for (int v = 0; v < model::kCastTypeCount; ++v) {
                auto* spell = cast_[i][v];
                Name(spell, name);
                SetCasting(spell, slot.cost, slot.school);
                SetVisuals(spell, slot.school, slot.hostile, v);
                // Never mark the script effect Hostile/Detrimental. Hex vs Blessing still drives visuals and
                // spells.json; Magic Tattoos Spellbook blames the caster in Papyrus when a Hex lands and the
                // caster isn't hidden, so a sneak hex can mark without the engine treating the land as an attack.
                if (auto* effect = EffectOf(spell)) {
                    effect->data.flags.reset(RE::EffectSetting::EffectSettingData::Flag::kHostile, RE::EffectSetting::EffectSettingData::Flag::kDetrimental);
                }
            }
            Name(decay_[i], name);
            if (decay_[i]) {
                decay_[i]->effects[0]->effectItem.duration = static_cast<std::uint32_t>(std::lround(slot.decay));
            }
        }
        // Dispel versions are self, touch, ranged (no self-channeled).
        static constexpr int kDispelCast[] = {static_cast<int>(model::CastType::SelfInstant), static_cast<int>(model::CastType::Touch),
            static_cast<int>(model::CastType::Ranged)};
        const auto dispelName = SpellName(config, kSpellbookDispel);
        for (int d = 0; d < kDispelVersions; ++d) {
            Name(dispel_[d], dispelName);
            SetCasting(dispel_[d], config.dispel.cost, config.dispel.school);
            SetVisuals(dispel_[d], config.dispel.school, false, kDispelCast[d]);
        }
    }

    KnownSpells SpellbookBridge::ReadKnownNow() const {
        KnownSpells known;
        known.fill(-1);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return known;
        }
        for (int i = 0; i < kSpellbookSpells; ++i) {
            for (int v = 0; v < model::kCastTypeCount; ++v) {
                if (auto* spell = Version(i, v); spell && player->HasSpell(spell)) {
                    known[i] = v;
                    break;
                }
            }
        }
        return known;
    }

    void SpellbookBridge::SetKnown(int index, int cast) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }
        RE::SpellItem* keep = Version(index, cast);
        for (int v = 0; v < model::kCastTypeCount; ++v) {
            auto* spell = Version(index, v);
            if (spell && spell != keep && player->HasSpell(spell)) {
                player->RemoveSpell(spell);
            }
        }
        if (keep && !player->HasSpell(keep)) {
            player->AddSpell(keep);
        }
    }

    KnownSync SpellbookBridge::SyncKnown(const model::SpellbookConfig& config) {
        KnownSync sync;
        sync.known = ReadKnownNow();
        for (int i = 0; i < kSpellbookSpells; ++i) {
            if (sync.known[i] < 0) {
                continue;
            }
            if (i != kSpellbookDispel && config.slots[i].preset.empty()) {
                logger::info("{}: spell {} has no preset now, forgetting it", kSpellbookPlugin, i + 1);
                SetKnown(i, -1);
                ++sync.forgotten;
                continue;
            }
            const int want = static_cast<int>(i == kSpellbookDispel ? config.dispel.cast : config.slots[i].cast);
            if (sync.known[i] != want) {
                logger::info("{}: swapping the known version of spell {} to cast type {}", kSpellbookPlugin, i + 1, want);
                SetKnown(i, want);
                ++sync.swapped;
            }
        }
        sync.known = ReadKnownNow();
        return sync;
    }

    bool SpellbookBridge::Save(const model::SpellbookConfig& config, std::string* error, std::function<void(KnownSync)> done) {
        if (!installed_) {
            if (error) {
                *error = "MagicTattoosSpellbook.esp isn't loaded";
            }
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        if (!io::WriteJsonStyled(file_, model::SpellbookToJson(config), error)) {
            return false;
        }
        config_ = config;
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, config, done = std::move(done)] {
            CallStatic("JsonUtil", "Unload", {}, RE::BSFixedString(kJsonUtilFile), false, false);
            ApplyRecords(config);
            auto sync = SyncKnown(config);
            MtfBridge::PostToUi([done, sync = std::move(sync)] { done(sync); });
        });
        return true;
    }

    void SpellbookBridge::OnGameLoaded() {
        if (!installed_) {
            return;
        }
        MtfBridge::RunOnGame([this, config = config_] {
            ApplyRecords(config);
            SyncKnown(config);
        });
    }

    void SpellbookBridge::ReadKnown(std::function<void(KnownSpells)> done) {
        if (!installed_) {
            return;
        }
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, done = std::move(done)] {
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }

    void SpellbookBridge::Learn(int index, std::function<void(KnownSpells)> done) {
        if (!installed_ || index < 0 || index >= kSpellbookSpells) {
            return;
        }
        const int cast = static_cast<int>(index == kSpellbookDispel ? config_.dispel.cast : config_.slots[index].cast);
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, index, cast, done = std::move(done)] {
            SetKnown(index, cast);
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }

    void SpellbookBridge::Forget(int index, std::function<void(KnownSpells)> done) {
        if (!installed_ || index < 0 || index >= kSpellbookSpells) {
            return;
        }
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, index, done = std::move(done)] {
            SetKnown(index, -1);
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }
}
