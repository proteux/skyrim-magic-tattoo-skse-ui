#include "game/LivePreview.h"

#include "game/MtfBridge.h"
#include "io/JsonStore.h"
#include "io/StyledJson.h"
#include "io/Translations.h"
#include "model/Catalogs.h"

namespace mtfui::game {
    namespace {
        constexpr auto kDebounce = 300ms;
        constexpr auto kBusyTimeout = 10s;
        constexpr std::size_t kMaxKnown = 32;

        bool Contains(const std::vector<std::string>& names, const std::string& name) {
            return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return model::IEquals(n, name); });
        }

        std::string JoinNames(const std::vector<std::string>& names) {
            std::string out;
            for (const auto& n : names) {
                out += (out.empty() ? "" : ", ") + n;
            }
            return out;
        }
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

    void LivePreview::SetListeners(std::function<void(const std::string&)> error, std::function<void()> appliedChanged) {
        onError_ = std::move(error);
        onAppliedChanged_ = std::move(appliedChanged);
    }

    void LivePreview::Request(RE::FormID actor, const model::Preset& source, int slot, bool includeEffects, std::vector<std::string> suspend) {
        if (actor == 0) {
            return;
        }
        Pending p;
        p.actor = actor;
        p.doc = model::PresetToJson(MakePreviewPreset(source, slot, includeEffects));
        p.suspend = std::move(suspend);
        if (pending_ && pending_->actor == actor && pending_->doc == p.doc && pending_->suspend == p.suspend) {
            return;
        }
        if (!pending_ && lastActor_ == actor && last_ == p.doc && lastSuspend_ == p.suspend) {
            return;
        }
        p.due = std::chrono::steady_clock::now() + kDebounce;
        pending_ = std::move(p);
    }

    void LivePreview::RunSteps(std::shared_ptr<std::vector<Step>> steps, std::size_t i, int gen) {
        // Stopped or timed out: whoever bumped the generation has taken over the cleanup.
        if (i >= steps->size() || gen != generation_) {
            return;
        }
        (*steps)[i]([this, steps, i, gen] { RunSteps(steps, i + 1, gen); });
    }

    void LivePreview::Tick() {
        const auto now = std::chrono::steady_clock::now();
        if (busy_ && now - busySince_ > kBusyTimeout) {
            // Papyrus never answered (script lag, or a call MTF doesn't have): let the next edit try again.
            logger::warn("Live preview got no answer from MTF within {}s", kBusyTimeout.count());
            busy_ = false;
            lastActor_ = 0;
            ++generation_;
            status_ = T("$MTFUI_Preview_NoResponse");
        }
        if (busy_ || !pending_ || now < pending_->due) {
            return;
        }
        auto job = std::move(*pending_);
        pending_.reset();
        std::string error;
        if (!io::WriteJsonStyled(file_, job.doc, &error)) {
            status_ = TF("$MTFUI_Preview_WriteFailed", error);
            logger::error("Could not write the preview preset: {}", error);
            return;
        }
        busy_ = true;
        busySince_ = std::chrono::steady_clock::now();
        status_ = T("$MTFUI_Preview_Updating");
        last_ = job.doc;
        lastActor_ = job.actor;
        lastSuspend_ = job.suspend;
        const RE::FormID previous = applied_;
        const RE::FormID actor = job.actor;
        const int gen = generation_;
        if (std::find(touched_.begin(), touched_.end(), actor) == touched_.end()) {
            touched_.push_back(actor);
        }
        Remember(actor);
        logger::info("Live preview on {:08X}", actor);

        auto steps = std::make_shared<std::vector<Step>>();
        if (previous != 0 && previous != actor) {
            steps->push_back([this, previous](std::function<void()> next) {
                auto owed = std::move(suspended_[previous]);
                suspended_.erase(previous);
                MtfBridge::Get().Remove(previous, kPreviewPreset, [this, previous, owed, next] { Restore(previous, owed, true, next); });
            });
        } else if (previous == actor) {
            // Remove first: MTF only redraws on a tier change, so re-adding is what refreshes the look.
            steps->push_back([actor](std::function<void()> next) { MtfBridge::Get().Remove(actor, kPreviewPreset, next); });
        }

        // Take off what would draw the tattoo a second time, and put back what no longer would. The list is written
        // to the actor before anything comes off, so a save made at any point after can be repaired on load.
        steps->push_back([this, actor, suspend = job.suspend](std::function<void()> next) {
            const auto held = SuspendedOn(actor);
            std::vector<std::string> takeOff;
            std::vector<std::string> putBack;
            std::copy_if(suspend.begin(), suspend.end(), std::back_inserter(takeOff), [&](const std::string& n) { return !Contains(held, n); });
            std::copy_if(held.begin(), held.end(), std::back_inserter(putBack), [&](const std::string& n) { return !Contains(suspend, n); });
            if (takeOff.empty() && putBack.empty()) {
                next();
                return;
            }
            if (suspend.empty()) {
                suspended_.erase(actor);
            } else {
                suspended_[actor] = suspend;
            }
            if (!takeOff.empty()) {
                logger::info("Hiding {} on {:08X} during the preview", JoinNames(takeOff), actor);
            }
            MtfBridge::Get().WriteSuspended(actor, suspend, [this, actor, takeOff, putBack, next] {
                auto remaining = std::make_shared<std::size_t>(takeOff.size() + 1);
                auto join = [remaining, next] {
                    if (--*remaining == 0) {
                        next();
                    }
                };
                for (const auto& n : takeOff) {
                    MtfBridge::Get().Remove(actor, n, join);
                }
                Restore(actor, putBack, false, join);
            });
        });

        steps->push_back([this, actor, gen](std::function<void()>) {
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
                        const auto hidden = SuspendedOn(actor).size();
                        status_ = hidden == 0 ? std::string(T("$MTFUI_Preview_Previewing"))
                                  : hidden == 1 ? TF("$MTFUI_Preview_HidingOne", hidden)
                                                : TF("$MTFUI_Preview_HidingMany", hidden);
                    } else {
                        // Keep last_: the same request again would fail the same way, so only an edit retries.
                        applied_ = 0;
                        status_ = TF("$MTFUI_Preview_Failed", ApplyResultText(rc, actor == 0x14));
                        auto owed = std::move(suspended_[actor]);
                        suspended_.erase(actor);
                        Restore(actor, owed, true);
                    }
                });
            });
        });
        RunSteps(steps, 0, gen);
    }

    void LivePreview::Restore(RE::FormID actor, std::vector<std::string> names, bool rewriteRecord, std::function<void()> done) {
        if (names.empty()) {
            if (done) {
                done();
            }
            return;
        }
        logger::info("Putting back {} on {:08X}", JoinNames(names), actor);
        auto retry = std::make_shared<std::vector<std::string>>();
        auto reasons = std::make_shared<std::vector<std::string>>();
        auto remaining = std::make_shared<std::size_t>(names.size());
        for (const auto& n : names) {
            MtfBridge::Get().Apply(actor, n, [this, actor, n, retry, reasons, remaining, rewriteRecord, done](int rc) {
                if (rc != 1 && rc != 0) {
                    reasons->push_back(TF("$MTFUI_Preview_PutBackReason", n, ApplyResultText(rc, actor == 0x14)));
                    // Only a missing actor can come right later; a deleted preset or full slots would fail every load.
                    if (rc == -2) {
                        retry->push_back(n);
                    }
                }
                if (--*remaining > 0) {
                    return;
                }
                if (!reasons->empty()) {
                    logger::warn("Could not put back {} on {:08X}", JoinNames(*reasons), actor);
                    if (onError_) {
                        onError_(TF("$MTFUI_Preview_PutBackFailed", JoinNames(*reasons)));
                    }
                }
                if (rewriteRecord) {
                    // What may still go back stays listed, so the next load of a save made now tries again.
                    auto keep = *retry;
                    for (const auto& s : SuspendedOn(actor)) {
                        if (!Contains(keep, s)) {
                            keep.push_back(s);
                        }
                    }
                    MtfBridge::Get().WriteSuspended(actor, keep);
                }
                if (onAppliedChanged_) {
                    onAppliedChanged_();
                }
                if (done) {
                    done();
                }
            });
        }
    }

    void LivePreview::Stop() {
        pending_.reset();
        last_ = nlohmann::json();
        lastActor_ = 0;
        lastSuspend_.clear();
        auto actors = touched_;
        touched_.clear();
        auto owed = std::move(suspended_);
        suspended_.clear();
        for (const auto& [id, names] : owed) {
            if (std::find(actors.begin(), actors.end(), id) == actors.end()) {
                actors.push_back(id);
            }
        }
        applied_ = 0;
        status_.clear();
        if (busy_) {
            ++generation_;
            busy_ = false;
        }
        for (const auto id : actors) {
            const auto it = owed.find(id);
            if (it == owed.end() || it->second.empty()) {
                MtfBridge::Get().Remove(id, kPreviewPreset, {});
                continue;
            }
            // The preview comes off first so the restored presets get its overlay slots back.
            MtfBridge::Get().Remove(id, kPreviewPreset, [this, id, names = it->second] { Restore(id, names, true); });
        }
    }

    void LivePreview::Cleanup() {
        std::vector<RE::FormID> all = known_;
        all.push_back(0x14);
        std::sort(all.begin(), all.end());
        all.erase(std::unique(all.begin(), all.end()), all.end());
        logger::info("Removing any leftover preview from {} actor(s)", all.size());
        for (const auto id : all) {
            MtfBridge::Get().Remove(id, kPreviewPreset, [this, id] {
                MtfBridge::Get().ReadSuspended(id, [this, id](std::vector<std::string> names) {
                    if (!names.empty()) {
                        logger::info("This save was made during a live preview; restoring what it hid on {:08X}", id);
                        Restore(id, names, true);
                    }
                });
            });
        }
        ++generation_;
        touched_.clear();
        suspended_.clear();
        applied_ = 0;
        pending_.reset();
        last_ = nlohmann::json();
        lastActor_ = 0;
        lastSuspend_.clear();
        busy_ = false;
        status_.clear();
    }

    const std::vector<std::string>& LivePreview::SuspendedOn(RE::FormID actor) const {
        static const std::vector<std::string> none;
        const auto it = suspended_.find(actor);
        return it == suspended_.end() ? none : it->second;
    }

    bool LivePreview::IsSuspended(RE::FormID actor, const std::string& name) const {
        return Contains(SuspendedOn(actor), name);
    }

    void LivePreview::Forget(RE::FormID actor, const std::string& name) {
        const auto it = suspended_.find(actor);
        if (it == suspended_.end()) {
            return;
        }
        const auto same = [&](const std::string& n) { return model::IEquals(n, name); };
        std::erase_if(it->second, same);
        auto rest = it->second;
        if (rest.empty()) {
            suspended_.erase(it);
        }
        // The next request leaves it out too; without this it would count as a change and redraw the preview.
        std::erase_if(lastSuspend_, same);
        if (pending_) {
            std::erase_if(pending_->suspend, same);
        }
        MtfBridge::Get().WriteSuspended(actor, rest);
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
