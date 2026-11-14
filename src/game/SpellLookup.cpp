#include "game/SpellLookup.h"

#include "game/MtfBridge.h"
#include "io/UiSettings.h"
#include "model/PluginCatalog.h"

namespace mtfui::game {
    namespace {
        int SchoolOf(RE::ActorValue skill) {
            switch (skill) {
                case RE::ActorValue::kAlteration: return 0;
                case RE::ActorValue::kConjuration: return 1;
                case RE::ActorValue::kDestruction: return 2;
                case RE::ActorValue::kIllusion: return 3;
                case RE::ActorValue::kRestoration: return 4;
                default: return -1;
            }
        }

        // "skyrim.esm|4045" for "Skyrim.esm|0x012FCD", so case and leading zeros don't matter. Empty when the value
        // isn't a plugin and a hex FormID.
        std::string Normalize(std::string_view value) {
            const auto bar = value.find('|');
            if (bar == std::string_view::npos || bar == 0) {
                return {};
            }
            auto trim = [](std::string_view s) {
                while (!s.empty() && s.front() == ' ') {
                    s.remove_prefix(1);
                }
                while (!s.empty() && s.back() == ' ') {
                    s.remove_suffix(1);
                }
                return s;
            };
            const auto plugin = trim(value.substr(0, bar));
            auto id = trim(value.substr(bar + 1));
            if (id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
                id.remove_prefix(2);
            }
            if (plugin.empty() || id.empty() || id.size() > 8) {
                return {};
            }
            std::uint32_t n = 0;
            for (const char c : id) {
                int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (d < 0) {
                    return {};
                }
                n = n * 16 + static_cast<std::uint32_t>(d);
            }
            return io::ToLower(std::string(plugin)) + "|" + std::to_string(n);
        }
    }

    SpellLookup& SpellLookup::Get() {
        static SpellLookup instance;
        return instance;
    }

    void SpellLookup::OnDataLoaded() {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) {
            return;
        }
        spells_.clear();
        byValue_.clear();
        for (auto* spell : data->GetFormArray<RE::SpellItem>()) {
            if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell || spell->effects.empty()) {
                continue;
            }
            const char* name = spell->GetFullName();
            auto* file = spell->GetFile(0);
            if (!name || !*name || !file) {
                continue;
            }
            SpellEntry e;
            e.name = name;
            e.plugin = std::string(file->GetFilename());
            e.formId = spell->GetFormID();
            const auto local = file->IsLight() ? e.formId & 0xFFF : e.formId & 0xFFFFFF;
            e.value = std::format("{}|0x{:06X}", e.plugin, local);
            if (auto* costliest = spell->GetCostliestEffectItem(); costliest && costliest->baseEffect) {
                e.school = SchoolOf(costliest->baseEffect->data.associatedSkill);
            }
            spells_.push_back(std::move(e));
        }
        std::ranges::stable_sort(spells_, [](const SpellEntry& a, const SpellEntry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
        for (std::size_t i = 0; i < spells_.size(); ++i) {
            byValue_.emplace(Normalize(spells_[i].value), i);
        }
        ready_ = true;
        logger::info("Spell picker: {} spells", spells_.size());

        model::SetLookupResolver([this](std::string_view kind, std::string_view value) {
            model::LookupResult r;
            if (kind != "spell" || !ready_) {
                return r;
            }
            if (const auto* e = Find(value)) {
                r.state = model::LookupState::Found;
                r.name = e->name;
            } else if (!Normalize(value).empty()) {
                r.state = model::LookupState::Missing;
            }
            return r;
        });
    }

    // A typed name, without a plugin, is matched as Papyrus matches it: the first spell with that name.
    const SpellEntry* SpellLookup::Find(std::string_view value) const {
        if (const auto key = Normalize(value); !key.empty()) {
            const auto it = byValue_.find(key);
            return it == byValue_.end() ? nullptr : &spells_[it->second];
        }
        for (const auto& e : spells_) {
            if (e.name.size() == value.size() && _strnicmp(e.name.c_str(), value.data(), value.size()) == 0) {
                return &e;
            }
        }
        return nullptr;
    }

    void SpellLookup::ReadKnown(std::function<void(std::unordered_set<RE::FormID>)> done) const {
        if (!done) {
            return;
        }
        MtfBridge::RunOnGame([this, session = MtfBridge::Session(), done = std::move(done)] {
            std::unordered_set<RE::FormID> known;
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                for (const auto& e : spells_) {
                    if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(e.formId); spell && player->HasSpell(spell)) {
                        known.insert(e.formId);
                    }
                }
            }
            MtfBridge::PostToUi([session, done, known = std::move(known)] {
                if (session == MtfBridge::Session()) {
                    done(known);
                }
            });
        });
    }
}
