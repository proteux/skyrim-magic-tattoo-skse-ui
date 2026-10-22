#include "game/LivePreview.h"

#include "game/MtfBridge.h"
#include "io/JsonStore.h"
#include "io/StyledJson.h"

namespace mtfui::game {
    namespace {
        constexpr auto kDebounce = 300ms;
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

    void LivePreview::Init(const std::filesystem::path& builtinDir, std::function<void(const std::vector<RE::FormID>&)> recordActors) {
        file_ = builtinDir / (std::string(kPreviewPreset) + ".json");
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
        if (!pending_ && applied_ == actor && last_ == p.doc) {
            return;
        }
        p.due = std::chrono::steady_clock::now() + kDebounce;
        pending_ = std::move(p);
    }

    void LivePreview::Tick() {
        if (busy_ || !pending_ || std::chrono::steady_clock::now() < pending_->due) {
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
        status_ = "Updating preview...";
        last_ = job.doc;
        auto& bridge = MtfBridge::Get();
        const RE::FormID previous = applied_;
        const RE::FormID actor = job.actor;
        if (std::find(touched_.begin(), touched_.end(), actor) == touched_.end()) {
            touched_.push_back(actor);
            Record();
        }
        // Remove first: MTF only redraws on a tier change, so re-adding is what refreshes the look.
        auto apply = [this, actor] {
            MtfBridge::Get().PresetChanged(kPreviewPreset, true, [this, actor] {
                MtfBridge::Get().Apply(actor, kPreviewPreset, [this, actor](int rc) {
                    busy_ = false;
                    if (rc == 1 || rc == 0) {
                        applied_ = actor;
                        status_ = "Previewing";
                    } else {
                        applied_ = 0;
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
        const auto actors = touched_;
        touched_.clear();
        applied_ = 0;
        status_.clear();
        for (const auto id : actors) {
            MtfBridge::Get().Remove(id, kPreviewPreset, {});
        }
        Record();
    }

    void LivePreview::Cleanup(const std::vector<RE::FormID>& actors) {
        std::vector<RE::FormID> all = actors;
        all.push_back(0x14);
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        for (const auto id : all) {
            MtfBridge::Get().Remove(id, kPreviewPreset, {});
        }
        touched_.clear();
        applied_ = 0;
        pending_.reset();
        busy_ = false;
        Record();
    }

    void LivePreview::Record() {
        if (record_) {
            record_(touched_);
        }
    }
}
