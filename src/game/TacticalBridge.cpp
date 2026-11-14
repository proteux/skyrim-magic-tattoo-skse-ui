#include "game/TacticalBridge.h"

#include "game/MtfBridge.h"
#include "game/Papyrus.h"
#include "io/JsonStore.h"
#include "io/Translations.h"

namespace mtfui::game {
    namespace {
        constexpr const char* kJsonUtilFile = "MagicTattoosTactical/spells";

        // A callback from before a save loaded describes a player that is gone; drop it. Never empty.
        std::function<void(KnownSpells)> Guarded(std::function<void(KnownSpells)> fn) {
            return [s = MtfBridge::Session(), fn = std::move(fn)](KnownSpells known) {
                if (fn && s == MtfBridge::Session()) {
                    fn(known);
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
    }

    TacticalBridge& TacticalBridge::Get() {
        static TacticalBridge instance;
        return instance;
    }

    std::string TacticalBridge::SpellName(const model::TacticalConfig& config, int index) {
        if (index == kTacticalDispel) {
            return config.dispel.name.empty() ? std::string(T("$MTFUI_Spells_DispelDefaultName")) : config.dispel.name;
        }
        const auto& name = config.slots[index].name;
        return name.empty() ? TF("$MTFUI_Spells_DefaultName", index + 1) : name;
    }

    void TacticalBridge::OnDataLoaded(const std::filesystem::path& configFile) {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data || !data->LookupModByName(kTacticalPlugin)) {
            logger::info("{}: not loaded", kTacticalPlugin);
            return;
        }
        int missing = 0;
        auto spell = [&](RE::FormID id) {
            auto* s = data->LookupForm<RE::SpellItem>(id, kTacticalPlugin);
            if (!s || !EffectOf(s)) {
                logger::error("{}: spell {:03X} is missing", kTacticalPlugin, id);
                ++missing;
            }
            return s;
        };
        for (int i = 0; i < model::kTacticalSlots; ++i) {
            for (int v = 0; v < model::kCastTypeCount; ++v) {
                cast_[i][v] = spell(TacticalCastSpell(v, i));
            }
            decay_[i] = spell(TacticalDecaySpell(i));
        }
        for (int d = 0; d < kDispelVersions; ++d) {
            dispel_[d] = spell(TacticalDispelSpell(d));
        }
        if (missing > 0) {
            logger::error("{}: {} spells are missing, so the Spells tab is off. Is the ESP from an older or newer build?", kTacticalPlugin, missing);
            return;
        }
        file_ = configFile;
        std::string error;
        if (auto j = io::ReadJson(file_, &error)) {
            config_ = model::TacticalFromJson(*j);
        } else if (std::filesystem::exists(file_)) {
            logger::warn("Couldn't read {}: {}. The spells start from their defaults", io::PathText(file_), error);
        }
        installed_ = true;
        ApplyRecords(config_);
        logger::info("{}: loaded, {} spells set up", kTacticalPlugin,
            std::ranges::count_if(config_.slots, [](const auto& s) { return !s.preset.empty(); }));
    }

    RE::SpellItem* TacticalBridge::Version(int index, int cast) const {
        if (cast < 0 || cast >= model::kCastTypeCount) {
            return nullptr;
        }
        if (index == kTacticalDispel) {
            const auto c = static_cast<model::CastType>(cast);
            return c == model::CastType::SelfChannel ? nullptr : dispel_[model::DispelVersion(c)];
        }
        return index >= 0 && index < model::kTacticalSlots ? cast_[index][cast] : nullptr;
    }

    void TacticalBridge::ApplyRecords(const model::TacticalConfig& config) {
        for (int i = 0; i < model::kTacticalSlots; ++i) {
            const auto& slot = config.slots[i];
            const auto name = SpellName(config, i);
            for (auto* spell : cast_[i]) {
                Name(spell, name);
                SetCasting(spell, slot.cost, slot.school);
                if (auto* effect = EffectOf(spell)) {
                    if (slot.hostile) {
                        effect->data.flags.set(RE::EffectSetting::EffectSettingData::Flag::kHostile, RE::EffectSetting::EffectSettingData::Flag::kDetrimental);
                    } else {
                        effect->data.flags.reset(RE::EffectSetting::EffectSettingData::Flag::kHostile, RE::EffectSetting::EffectSettingData::Flag::kDetrimental);
                    }
                }
            }
            Name(decay_[i], name);
            if (decay_[i]) {
                decay_[i]->effects[0]->effectItem.duration = static_cast<std::uint32_t>(std::lround(slot.decay));
            }
        }
        const auto dispelName = SpellName(config, kTacticalDispel);
        for (auto* spell : dispel_) {
            Name(spell, dispelName);
            SetCasting(spell, config.dispel.cost, config.dispel.school);
        }
    }

    KnownSpells TacticalBridge::ReadKnownNow() const {
        KnownSpells known;
        known.fill(-1);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return known;
        }
        for (int i = 0; i < kTacticalSpells; ++i) {
            for (int v = 0; v < model::kCastTypeCount; ++v) {
                if (auto* spell = Version(i, v); spell && player->HasSpell(spell)) {
                    known[i] = v;
                    break;
                }
            }
        }
        return known;
    }

    void TacticalBridge::SetKnown(int index, int cast) {
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

    void TacticalBridge::SyncKnown(const model::TacticalConfig& config) {
        const auto known = ReadKnownNow();
        for (int i = 0; i < kTacticalSpells; ++i) {
            if (known[i] < 0) {
                continue;
            }
            if (i != kTacticalDispel && config.slots[i].preset.empty()) {
                logger::info("{}: spell {} has no preset now, forgetting it", kTacticalPlugin, i + 1);
                SetKnown(i, -1);
                continue;
            }
            const int want = static_cast<int>(i == kTacticalDispel ? config.dispel.cast : config.slots[i].cast);
            if (known[i] != want) {
                logger::info("{}: swapping the known version of spell {} to cast type {}", kTacticalPlugin, i + 1, want);
                SetKnown(i, want);
            }
        }
    }

    bool TacticalBridge::Save(const model::TacticalConfig& config, std::string* error, std::function<void(KnownSpells)> done) {
        if (!installed_) {
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(file_.parent_path(), ec);
        if (!io::WriteJsonStyled(file_, model::TacticalToJson(config), error)) {
            return false;
        }
        config_ = config;
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, config, done = std::move(done)] {
            CallStatic("JsonUtil", "Unload", {}, RE::BSFixedString(kJsonUtilFile), false, false);
            ApplyRecords(config);
            SyncKnown(config);
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
        return true;
    }

    void TacticalBridge::OnGameLoaded() {
        if (!installed_) {
            return;
        }
        MtfBridge::RunOnGame([this, config = config_] {
            ApplyRecords(config);
            SyncKnown(config);
        });
    }

    void TacticalBridge::ReadKnown(std::function<void(KnownSpells)> done) {
        if (!installed_) {
            return;
        }
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, done = std::move(done)] {
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }

    void TacticalBridge::Learn(int index, std::function<void(KnownSpells)> done) {
        if (!installed_ || index < 0 || index >= kTacticalSpells) {
            return;
        }
        const int cast = static_cast<int>(index == kTacticalDispel ? config_.dispel.cast : config_.slots[index].cast);
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, index, cast, done = std::move(done)] {
            SetKnown(index, cast);
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }

    void TacticalBridge::Forget(int index, std::function<void(KnownSpells)> done) {
        if (!installed_ || index < 0 || index >= kTacticalSpells) {
            return;
        }
        done = Guarded(std::move(done));
        MtfBridge::RunOnGame([this, index, done = std::move(done)] {
            SetKnown(index, -1);
            MtfBridge::PostToUi([done, known = ReadKnownNow()] { done(known); });
        });
    }
}
