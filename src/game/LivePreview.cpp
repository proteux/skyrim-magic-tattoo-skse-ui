#include "game/LivePreview.h"

#include "game/MtfBridge.h"
#include "io/JsonStore.h"
#include "io/StyledJson.h"

namespace mtfui::game {
    namespace {
        constexpr auto kDebounce = 300ms;
        constexpr auto kBusyTimeout = 10s;
        constexpr std::size_t kMaxKnown = 32;
    }

    LivePreview& LivePreview::Get() {
        static LivePreview instance;
        return instance;
    }

    model::Preset MakePreviewPreset(const model::Preset& source, int slot, bool includeEffects) {
        auto p = model::NewPreset(kPreviewPreset, "Magic Tattoos UI preview");
        p.transitionDuration = 0.0;
        if (slot < 0 || slot >= static_cast<int>(source.slots.size())) {
            return p;
        }
        const auto& s = source.slots[slot];
        const auto vis = model::ResolveVisual(source, slot);
        auto& d = p.slots[0];
        d.packId = vis.none ? std::string{} : vis.packId;
        d.entryId = vis.none ? std::string{} : vis.entryId;
        d.layers = s.layers;
        d.pulse = s.pulse;
        if (includeEffects) {
            d.effects = s.effects;
        }
        return p;
    }

    void LivePreview::Init(const std::filesystem::path& builtinDir, std::vector<RE::FormID> known,
        std::function<void(const std::vector<RE::FormID>&)> recordActors) {
        file_ = builtinDir / (std::string(kPreviewPreset) + ".json");
        known_ = std::move(known);
        record_ = std::move(recordActors);
    }

    void LivePreview::Request(RE::FormID actor, const model::Preset& source, int slot, bool includeEffects) {
        if (actor == 0) {
            return;
        }
        Pending p;
        p.actor = actor;
        p.doc = model::PresetToJson(MakePreviewPreset(source, slot, includeEffects));
        if (pending_ && pending_->actor == actor && pending_->doc == p.doc) {
            return;
        }
        if (!pending_ && lastActor_ == actor && last_ == p.doc) {
            return;
        }
        p.due = std::chrono::steady_clock::now() + kDebounce;
        pending_ = std::move(p);
    }

    void LivePreview::Tick() {
        const auto now = std::chrono::steady_clock::now();
        if (busy_ && now - busySince_ > kBusyTimeout) {
            // Papyrus never answered (script lag, or a call MTF doesn't have): let the next edit try again.
            logger::warn("Live preview got no answer from MTF within {}s", kBusyTimeout.count());
            busy_ = false;
            lastActor_ = 0;
            ++generation_;
            status_ = "MTF did not respond to the preview";
        }
        if (busy_ || !pending_ || now < pending_->due) {
            return;
        }
        auto job = std::move(*pending_);
        pending_.reset();
        std::string error;
        if (!io::WriteJsonStyled(file_, job.doc, &error)) {
            status_ = "Could not write the preview preset: " + error;
            logger::error("{}", status_);
            return;
        }
        busy_ = true;
        busySince_ = std::chrono::steady_clock::now();
        status_ = "Updating preview...";
        last_ = job.doc;
        lastActor_ = job.actor;
        auto& bridge = MtfBridge::Get();
        const RE::FormID previous = applied_;
        const RE::FormID actor = job.actor;
        const int gen = generation_;
        if (std::find(touched_.begin(), touched_.end(), actor) == touched_.end()) {
            touched_.push_back(actor);
        }
        Remember(actor);
        logger::info("Live preview on {:08X}", actor);
        // Remove first: MTF only redraws on a tier change, so re-adding is what refreshes the look.
        auto apply = [this, actor, gen] {
            MtfBridge::Get().PresetChanged(kPreviewPreset, true, [this, actor, gen] {
                MtfBridge::Get().Apply(actor, kPreviewPreset, [this, actor, gen](int rc) {
                    if (gen != generation_) {
                        // Stopped (or timed out) while this was in flight: undo it rather than leave an untracked preview.
                        if (rc == 1 || rc == 0) {
                            logger::info("Removing a preview that finished after it was stopped, on {:08X}", actor);
                            MtfBridge::Get().Remove(actor, kPreviewPreset, {});
                        }
                        return;
                    }
                    busy_ = false;
                    if (rc == 1 || rc == 0) {
                        applied_ = actor;
                        status_ = "Previewing";
                    } else {
                        applied_ = 0;
                        lastActor_ = 0;
                        status_ = "Preview failed: " + ApplyResultText(rc);
                    }
                });
            });
        };
        if (previous != 0) {
            bridge.Remove(previous, kPreviewPreset, apply);
        } else {
            apply();
        }
    }

    void LivePreview::Stop() {
        pending_.reset();
        last_ = nlohmann::json();
        lastActor_ = 0;
        const auto actors = touched_;
        touched_.clear();
        applied_ = 0;
        status_.clear();
        if (busy_) {
            ++generation_;
            busy_ = false;
        }
        for (const auto id : actors) {
            MtfBridge::Get().Remove(id, kPreviewPreset, {});
        }
    }

    void LivePreview::Cleanup() {
        std::vector<RE::FormID> all = known_;
        all.push_back(0x14);
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        logger::info("Removing any leftover preview from {} actor(s)", all.size());
        for (const auto id : all) {
            MtfBridge::Get().Remove(id, kPreviewPreset, {});
        }
        ++generation_;
        touched_.clear();
        applied_ = 0;
        pending_.reset();
        last_ = nlohmann::json();
        lastActor_ = 0;
        busy_ = false;
        status_.clear();
    }

    void LivePreview::Remember(RE::FormID actor) {
        if (!known_.empty() && known_.front() == actor) {
            return;
        }
        std::erase(known_, actor);
        known_.insert(known_.begin(), actor);
        if (known_.size() > kMaxKnown) {
            known_.resize(kMaxKnown);
        }
        if (record_) {
            record_(known_);
        }
    }
}
