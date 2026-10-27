// Offline checks against copies of real MTF data in tests/fixtures.
#include "io/DdsHeader.h"
#include "io/JsonStore.h"
#include "io/StyledJson.h"
#include "io/UiSettings.h"
#include "model/Catalogs.h"
#include "model/Describe.h"
#include "model/Lint.h"
#include "model/Preset.h"
#include "model/Slots.h"
#include "model/Text.h"
#include "model/Usage.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace mtfui;

namespace {
    int g_failures = 0;

    void Check(bool ok, const std::string& what) {
        if (!ok) {
            ++g_failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    void RoundTripPresets(const fs::path& fixtures) {
        int count = 0;
        for (const auto& e : fs::directory_iterator(fixtures / "presets")) {
            const auto original = io::ReadText(e.path());
            Check(original.has_value(), "read " + e.path().string());
            if (!original) {
                continue;
            }
            const auto j = nlohmann::json::parse(*original);
            Check(io::WriteStyled(j) == *original, "raw byte round-trip of " + e.path().filename().string());

            const auto preset = model::PresetFromJson(e.path().stem().string(), j);
            const auto rewritten = io::WriteStyled(model::PresetToJson(preset));
            Check(rewritten == *original, "typed model round-trip of " + e.path().filename().string());
            if (rewritten != *original) {
                std::size_t i = 0;
                while (i < rewritten.size() && i < original->size() && rewritten[i] == (*original)[i]) {
                    ++i;
                }
                const auto from = i > 80 ? i - 80 : 0;
                std::printf("  first difference at byte %zu\n  expected: %s\n  actual:   %s\n", i,
                    original->substr(from, 160).c_str(), rewritten.substr(from, 160).c_str());
            }

            const auto reparsed = model::PresetFromJson(preset.name, nlohmann::json::parse(rewritten));
            Check(reparsed == preset, "typed equality after reload of " + e.path().filename().string());
            ++count;
        }
        Check(count > 0, "fixtures contain presets");
    }

    void Groups(const fs::path& fixtures) {
        const auto j = *io::ReadJson(fixtures / "presets" / "riDDEN_WET.json");
        auto preset = model::PresetFromJson("riDDEN_WET", j);
        Check(preset.group.empty(), "fixture presets have no group");
        Check(!model::PresetToJson(preset).contains(model::kGroupKey), "an empty group writes no key");
        preset.group = "Combat";
        const auto grouped = model::PresetToJson(preset);
        Check(grouped.value(model::kGroupKey, std::string{}) == "Combat", "group is written");
        Check(model::PresetFromJson("riDDEN_WET", grouped) == preset, "group survives a reload");
        auto ungrouped = model::PresetFromJson("riDDEN_WET", grouped);
        ungrouped.group.clear();
        Check(!model::PresetToJson(ungrouped).contains(model::kGroupKey), "clearing the group removes the key");

        const auto dir = fs::temp_directory_path() / "mtfui_group_test";
        fs::remove_all(dir);
        fs::copy(fixtures, dir, fs::copy_options::recursive);
        io::WriteJsonStyled(dir / "presets" / "riDDEN_WET.json", grouped);
        model::Catalogs cat;
        cat.Load(dir);
        const auto list = cat.ListPresets(false);
        const auto it = std::find_if(list.begin(), list.end(), [](const model::PresetFileInfo& i) { return i.name == "riDDEN_WET"; });
        Check(it != list.end() && it->group == "Combat", "the library reads the group");
        fs::remove_all(dir);
    }

    void Formatting() {
        Check(io::FormatDouble(1.0) == "1.0", "1.0 formats as 1.0");
        Check(io::FormatDouble(static_cast<float>(0.6)) == "0.60000002384185791", "float 0.6 widens like JsonUtil");
        Check(io::WriteStyled(nlohmann::json::array({ 1, 2 })) == "[ 1, 2 ]", "short arrays stay on one line");
        Check(io::WriteStyled(nlohmann::json::object()) == "{}", "empty object");
        Check(model::SanitizePresetName("B&H - Dragon") == "B_H_-_Dragon", "sanitize matches MTF");
        Check(model::SanitizePresetName(std::string(40, 'a')).size() == 32, "sanitize caps at 32");
        Check(model::FormatHexColor(0xAD0073) == "#AD0073", "hex color format");
        Check(model::ParseHexColor(nlohmann::json("#cc0033"), 0) == 0xCC0033, "hex color parse");
    }

    void Catalogs(const fs::path& fixtures) {
        model::Catalogs cat;
        cat.Load(fixtures);
        Check(cat.plugins.size() >= 8, "plugin catalogs load");
        Check(cat.loadErrors.empty(), "fixture catalogs load cleanly" + (cat.loadErrors.empty() ? std::string{} : ": " + cat.loadErrors.front()));
        Check(!cat.packs.empty() && !cat.packs.front().entries.empty(), "visual pack loads");
        Check(cat.FindCondition("mtf.ostim:Excitement") != nullptr, "case-insensitive condition lookup");
        Check(cat.FindCondition("mtf.base:combat.in") != nullptr, "legacy condition key resolves through module map");
        Check(cat.FindEntry("mtf.lyru-1", "lyrudragonfront") != nullptr, "case-insensitive entry lookup");

        const auto* heartbeat = cat.FindWaveform("heartbeat");
        Check(heartbeat && std::abs(heartbeat->Sample(0.08) - 1.0) < 1e-9, "keyframe waveform peak");
        Check(heartbeat && std::abs(heartbeat->Sample(0.04) - 0.5) < 1e-9, "keyframe waveform lerp");
        model::Waveform cosine;
        Check(std::abs(cosine.Sample(0.5) - 1.0) < 1e-9 && std::abs(cosine.Sample(0.0)) < 1e-9, "built-in cosine");
        Check(std::abs(model::PulseMultiplier(cosine, 1.0, 70, 0.0, 0.0) - 0.3) < 1e-9, "pulse trough = 1 - depth");
        Check(std::abs(model::PulseMultiplier(cosine, 1.0, 70, 0.5, 1.2) - 0.3) < 1e-9, "pulse holds through pause");

        const auto dragon = model::PresetFromJson("B_H_-_DRAGON", *io::ReadJson(fixtures / "presets" / "B_H_-_DRAGON.json"));
        const auto summary = model::DescribeSlot(cat, dragon, 1);
        Check(summary.find("50") != std::string::npos, "tier summary includes threshold: " + summary);
        Check(model::DescribeVisual(cat, dragon, 1).find("(from Default)") != std::string::npos, "inherited visual is labelled");

        const auto issues = model::LintPreset(cat, dragon, {});
        bool shadow = false;
        for (const auto& i : issues) {
            shadow = shadow || (i.slot == 2 && i.message.find("never activate") != std::string::npos);
        }
        Check(shadow, "lint flags a higher threshold placed below a lower one");

        auto fixed = dragon;
        std::swap(fixed.slots[1], fixed.slots[2]);
        for (const auto& i : model::LintPreset(cat, fixed, {})) {
            Check(i.message.find("never activate") == std::string::npos, "no shadow once reordered: " + i.message);
        }

        auto rebased = model::NewPreset("Rebased", "Rebased");
        rebased.slots[0].packId = "other.pack";
        rebased.slots[0].entryId = "otherentry";
        rebased.slots[0].layers[0].tint = 0x123456;
        model::CopyTiers(rebased, dragon);
        Check(rebased.slots.size() == dragon.slots.size(), "copied tiers keep the source's tier count");
        Check(std::equal(dragon.slots.begin() + 1, dragon.slots.end(), rebased.slots.begin() + 1), "copied tiers match the source");
        Check(rebased.slots[0].packId == "other.pack" && rebased.slots[0].entryId == "otherentry" && rebased.slots[0].layers[0].tint == 0x123456,
            "copying tiers keeps the Default look");
        Check(rebased.slots[0].effects == dragon.slots[0].effects, "copying tiers takes Default's effects");
        Check(model::ResolveVisual(rebased, 1).packId == "other.pack", "inheriting tiers follow the new Default tattoo");

        const auto presets = cat.ListPresets(false);
        Check(presets.size() == 6, "lists the fixture presets");
        const auto usage = model::BuildUsage(cat, presets);
        Check(!usage.entries.empty(), "usage index finds tattoo entries");
        const auto* ostim = usage.Plugin("mtf.ostim");
        Check(ostim && !ostim->empty(), "usage index finds plugin users");
    }

    void AtomicWrite() {
        const auto dir = fs::temp_directory_path() / "mtfui_tests";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const auto file = dir / "atomic.json";
        Check(io::WriteTextAtomic(file, "one"), "atomic write creates");
        Check(io::WriteTextAtomic(file, "two"), "atomic write replaces");
        Check(io::ReadText(file).value_or("") == "two", "atomic write content");
        Check(!fs::exists(dir / "atomic.json.tmp"), "atomic write leaves no temp file");

        const auto backups = dir / "backups";
        for (int i = 0; i < 3; ++i) {
            Check(io::Backup(file, backups, 2), "backup");
        }
        Check(io::ListBackups(backups).size() == 1, "identical backups are skipped");
        for (const char* text : { "three", "four", "five" }) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            io::WriteTextAtomic(file, text);
            Check(io::Backup(file, backups, 2), std::string("backup of ") + text);
        }
        const auto list = io::ListBackups(backups);
        Check(list.size() == 2, "backups are pruned to keep=2, found " + std::to_string(list.size()));
        Check(!list.empty() && list.front().stem == "atomic", "backup stem is the file name");
        Check(!list.empty() && io::ReadText(list.front().path).value_or("") == "five", "newest backup first");
        Check(list.size() == 2 && list.front().stamp > list.back().stamp, "backups sorted newest first");
        Check(io::Backup(dir / "missing.json", backups, 2), "backing up a missing file is a no-op success");

        std::string error;
        Check(!io::ReadJson(dir / "nothing.json", &error) && !error.empty(), "missing file reports an error");
        io::WriteTextAtomic(dir / "bad.json", "{ \"a\": 1,, }");
        error.clear();
        Check(!io::ReadJson(dir / "bad.json", &error) && !error.empty(), "malformed JSON reports an error: " + error);
        fs::remove_all(dir);
    }

    model::ConditionItem Cond(const std::string& id, std::int64_t value = 0) {
        model::ConditionItem c;
        c.pluginId = id;
        c.param = value;
        return c;
    }

    bool HasIssue(const std::vector<model::LintIssue>& issues, int slot, const std::string& text) {
        return std::any_of(issues.begin(), issues.end(), [&](const model::LintIssue& i) { return i.slot == slot && i.message.find(text) != std::string::npos; });
    }

    void LintRules(const fs::path& fixtures) {
        model::Catalogs cat;
        cat.Load(fixtures);
        const auto excite = [](std::int64_t v) { return Cond("mtf.ostim:Excitement", v); };
        const auto combat = Cond("mtf.combat:combat.in");

        auto shadowed = [&](int earlierOp, std::vector<model::ConditionItem> earlier, int laterOp, std::vector<model::ConditionItem> later) {
            auto p = model::NewPreset("T", "T");
            p.slots[1].op = earlierOp;
            p.slots[1].conditions = std::move(earlier);
            p.slots[2].op = laterOp;
            p.slots[2].conditions = std::move(later);
            return HasIssue(model::LintPreset(cat, p, {}), 2, "never activate");
        };
        Check(shadowed(0, { excite(50) }, 0, { excite(80), combat }), "AND tier with a lower bound shadows a stricter AND tier");
        Check(!shadowed(0, { excite(50), combat }, 0, { excite(80) }), "AND tier with an extra condition does not shadow");
        Check(shadowed(1, { excite(50), combat }, 0, { combat }), "OR tier shadows a tier that needs one of its branches");
        Check(!shadowed(0, { excite(50) }, 1, { excite(80), combat }), "a later OR tier can still activate through another branch");
        Check(shadowed(1, { excite(50), combat }, 1, { excite(80), combat }), "OR tier covering every later branch shadows it");
        Check(!shadowed(0, { excite(80) }, 0, { excite(50) }), "a higher bound above a lower one is fine");

        const auto dragon = model::PresetFromJson("B_H_-_DRAGON", *io::ReadJson(fixtures / "presets" / "B_H_-_DRAGON.json"));
        const auto issues = model::LintPreset(cat, dragon, {});
        const auto shadow = std::find_if(issues.begin(), issues.end(), [](const model::LintIssue& i) { return i.message.find("never activate") != std::string::npos; });
        Check(shadow != issues.end() && shadow->fix.kind == model::LintFix::Kind::MoveAbove && shadow->fix.targetSlot == 1,
            "shadowed tier offers moving above the tier that hides it");
        Check(shadow != issues.end() && shadow->tab == model::LintTab::Condition, "shadow issue points at the Condition tab");
        auto fixed = dragon;
        Check(model::MoveTier(fixed, 2, 1), "MoveTier moves a tier up");
        Check(!HasIssue(model::LintPreset(cat, fixed, {}), 1, "never activate") && !HasIssue(model::LintPreset(cat, fixed, {}), 2, "never activate"),
            "applying the fix clears the shadow");
        Check(!model::MoveTier(fixed, 0, 1) && !model::MoveTier(fixed, 1, 0) && !model::MoveTier(fixed, 1, 99), "MoveTier never moves Default or out of range");

        auto noCond = model::NewPreset("T", "T");
        noCond.slots[1].effects.push_back({});
        Check(HasIssue(model::LintPreset(cat, noCond, {}), 1, "no condition"), "content without a condition is flagged");

        auto missing = model::NewPreset("T", "T");
        missing.slots[1].conditions = { Cond("mtf.nothing:here") };
        missing.slots[1].effects.push_back({ "mtf.nothing:fx" });
        missing.slots[0].packId = "mtf.nopack";
        missing.slots[0].entryId = "x";
        const auto missingIssues = model::LintPreset(cat, missing, {});
        Check(HasIssue(missingIssues, 1, "is not installed; this tier"), "missing condition is an error");
        Check(HasIssue(missingIssues, 1, "Effect 'mtf.nothing:fx' is not installed"), "missing effect is an error");
        Check(HasIssue(missingIssues, 0, "Tattoo pack 'mtf.nopack'"), "missing pack is an error");

        model::LintContext disabled;
        disabled.pluginEnabled = [](const std::string& pid) { return !model::IEquals(pid, "mtf.ostim"); };
        const auto off = model::LintPreset(cat, dragon, disabled);
        const auto offIssue = std::find_if(off.begin(), off.end(), [](const model::LintIssue& i) { return i.fix.kind == model::LintFix::Kind::EnablePlugin; });
        Check(offIssue != off.end() && model::IEquals(offIssue->fix.pluginId, "mtf.ostim"), "disabled plugin offers enabling it");

        auto pulse = model::NewPreset("T", "T");
        pulse.slots[0].pulse.depth = 50;
        pulse.slots[0].pulse.waveform = "nonexistent";
        const auto pulseIssues = model::LintPreset(cat, pulse, {});
        Check(HasIssue(pulseIssues, 0, "rate is 0"), "pulse depth without rate is flagged");
        Check(HasIssue(pulseIssues, 0, "Waveform 'nonexistent'"), "missing waveform is flagged");
        Check(std::all_of(pulseIssues.begin(), pulseIssues.end(), [](const model::LintIssue& i) { return i.slot != 0 || i.tab == model::LintTab::Pulse; }),
            "pulse issues point at the Pulse tab");

        // Only the layers the tattoo uses count: a one-texture tattoo with layer 1 hidden is invisible, whatever layer 2 says.
        auto clear = dragon;
        const auto* entry = cat.FindEntry(clear.slots[0].packId, clear.slots[0].entryId);
        if (entry && entry->textures.size() == 1) {
            clear.slots[0].layers[0].alpha = 0;
            clear.slots[0].layers[1].alpha = 100;
            Check(HasIssue(model::LintPreset(cat, clear, {}), 0, "layer is fully transparent"), "transparent check uses the tattoo's layers only");
            clear.slots[0].layers[0].alpha = 10;
            Check(!HasIssue(model::LintPreset(cat, clear, {}), 0, "transparent"), "a visible layer clears the transparent check");
        }

        auto badName = model::NewPreset("bad name!", "");
        const auto nameIssues = model::LintPreset(cat, badName, {});
        Check(HasIssue(nameIssues, -1, "File name"), "invalid file name is an error");
        Check(HasIssue(nameIssues, -1, "Display name is empty"), "empty display name is noted");
    }

    void LookAndDescribe(const fs::path& fixtures) {
        model::Catalogs cat;
        cat.Load(fixtures);
        auto p = model::PresetFromJson("B_H_-_DRAGON", *io::ReadJson(fixtures / "presets" / "B_H_-_DRAGON.json"));

        // Tier 1 inherits Default's tattoo; copying its look onto tier 2 keeps inheriting, onto Default it is explicit.
        p.slots[1].layers[0].tint = 0x123456;
        model::CopyLook(p, 1, 2);
        Check(p.slots[2].packId.empty() && p.slots[2].layers[0].tint == 0x123456, "look copied tier to tier keeps inheriting");
        auto withNone = p;
        withNone.slots[0].packId.clear();
        model::CopyLook(withNone, 0, 3);
        Check(withNone.slots[3].packId == model::kNoTexture, "copying an empty Default onto a tier hides the tattoo");
        p.slots[3].packId = "mtf.lyru-1";
        p.slots[3].entryId = "other";
        model::CopyLook(p, 3, 0);
        Check(p.slots[0].packId == "mtf.lyru-1" && p.slots[0].entryId == "other", "copying onto Default takes the tattoo");

        auto tinted = model::NewPreset("T", "T");
        tinted.slots[0].packId = "mtf.lyru-1";
        tinted.slots[0].entryId = "LyruDragonFront";
        tinted.slots[0].layers[0].tint = 0xFF0000;
        tinted.slots[0].layers[0].emissiveMult = 1.0;
        tinted.slots[0].layers[0].emissive = 0x00FF00;
        const auto summary = model::DescribeSlot(cat, tinted, 0);
        Check(summary.find("tinted") != std::string::npos && summary.find("glowing") != std::string::npos, "summary names tint and glow: " + summary);
        Check(model::DescribeSlot(cat, model::NewPreset("T", "T"), 1) == "Unused", "unconfigured tier is Unused");

        const auto* exc = cat.FindCondition("mtf.ostim:Excitement");
        if (exc) {
            model::ConditionItem c;
            model::ApplyConditionDefaults(*exc, c);
            Check(c.param == exc->params[0].def, "condition defaults come from the catalog");
        }
        const model::PluginCatalog* owner = nullptr;
        for (const auto& plugin : cat.plugins) {
            for (const auto& e : plugin.effects) {
                if (!owner && e.ParamCount() > 0) {
                    owner = &plugin;
                    model::Effect fx;
                    model::ApplyEffectDefaults(e, fx);
                    Check(fx.params[0] == e.params[0].def, "effect defaults come from the catalog");
                }
            }
        }
        Check(owner != nullptr, "fixtures have an effect with parameters");
    }

    void Waveforms() {
        model::Waveform w;
        w.kind = model::Waveform::Kind::Triangle;
        Check(std::abs(w.Sample(0.25) - 0.5) < 1e-9 && std::abs(w.Sample(0.5) - 1.0) < 1e-9 && std::abs(w.Sample(0.75) - 0.5) < 1e-9, "triangle");
        w.kind = model::Waveform::Kind::Square;
        w.shape = 0.3;
        Check(w.Sample(0.2) == 1.0 && w.Sample(0.5) == 0.0, "square duty cycle");
        w.kind = model::Waveform::Kind::Sawtooth;
        Check(std::abs(w.Sample(1.25) - 0.25) < 1e-9, "phase wraps above 1");
        w.kind = model::Waveform::Kind::Cos;
        Check(std::abs(w.Sample(-0.5) - 1.0) < 1e-9, "phase wraps below 0");
        w.kind = model::Waveform::Kind::Keyframes;
        Check(w.Sample(0.5) == 0.0, "empty keyframes are flat");
        w.points = { { 0.2, 0.4 }, { 0.2, 0.9 }, { 0.6, 0.1 } };
        Check(w.Sample(0.1) == 0.4 && w.Sample(0.9) == 0.1, "keyframes hold their ends");
        Check(std::isfinite(w.Sample(0.2)), "zero-length keyframe span does not divide by zero");
        Check(std::abs(model::PulseMultiplier(w, 0.0, 50, 0.0, 3.0) - 1.0) < 1e-9, "rate 0 does not pulse");
    }

    void Params() {
        model::ParamSpec slider;
        slider.kind = model::ParamKind::Slider;
        Check(model::ParamInt(slider.Coerce(model::ParamValue{ std::string("42") })) == 42, "slider coerces a stored string");
        Check(std::holds_alternative<std::int64_t>(slider.Coerce(model::ParamValue{ std::string("7") })), "slider writes an int");
        model::ParamSpec menu;
        menu.kind = model::ParamKind::Menu;
        Check(model::ParamStr(menu.Coerce(model::ParamValue{ std::int64_t{ 3 } })) == "3", "menu coerces a stored int");

        Check(model::SliderPrintfFormat("") == "%d", "empty slider format");
        Check(model::SliderPrintfFormat("{0}% aroused") == "%d%% aroused", "percent signs are escaped");
        Check(model::SliderPrintfFormat("%d m") == "%d m", "printf-style placeholder is kept");
        Check(model::SliderPrintfFormat("%s {0} %n") == "%%s %d %%n", "other conversions are neutralized");
        Check(model::SliderPrintfFormat("meters") == "%d meters", "format without a placeholder gets one");
        Check(model::SliderPrintfFormat("{0} {0}") == "%d {0}", "only one conversion");

        const auto taken = [](const std::string& n) { return n == "Dragon" || n == "Dragon_2"; };
        Check(model::NextFreeName("Dragon", taken) == "Dragon_3", "next free name");
        Check(model::NextFreeName("Free", taken) == "Free", "free name kept");
        const std::string longName(32, 'a');
        Check(model::NextFreeName(longName, [&](const std::string& n) { return n == longName; }).size() == 32, "suffix stays within 32 characters");

        Check(model::MatchesSearch("Lyru Dragon Front", "dragon lyru"), "search words in any order");
        Check(!model::MatchesSearch("Lyru Dragon Front", "dragon back"), "every search word must match");
        Check(model::MatchesSearch("anything", "  "), "blank search matches");

        std::string error;
        Check(!model::ParsePresetText("not json", &error) && !error.empty(), "clipboard import rejects non-JSON");
        Check(!model::ParsePresetText("{\"a\":1}", &error), "clipboard import rejects non-presets");
        Check(model::ParsePresetText("{\"slot\":[]}", &error).has_value(), "clipboard import accepts a preset");
    }

    std::vector<std::uint8_t> DdsHeader(std::uint32_t w, std::uint32_t h, std::uint32_t mips, const char* fourcc, std::uint32_t dxgi = 0) {
        std::vector<std::uint8_t> d(4 + 124 + (dxgi ? 20 : 0), 0);
        auto put = [&](std::size_t at, std::uint32_t v) { std::memcpy(d.data() + at, &v, 4); };
        std::memcpy(d.data(), "DDS ", 4);
        put(4, 124);
        put(4 + 4, 0x1007 | (mips > 1 ? 0x20000u : 0u));
        put(4 + 8, h);
        put(4 + 12, w);
        put(4 + 24, mips);
        put(4 + 76, 0x4);
        std::memcpy(d.data() + 4 + 80, dxgi ? "DX10" : fourcc, 4);
        if (dxgi) {
            put(4 + 124, dxgi);
            put(4 + 124 + 4, 3);  // 2D
            put(4 + 124 + 12, 1);  // array size
        }
        return d;
    }

    void Dds() {
        std::size_t skip = 0, keep = 0;
        auto bc1 = io::ShrinkDds(DdsHeader(256, 256, 9, "DXT1"), 64, &skip, &keep);
        Check(bc1.has_value(), "BC1 header shrinks");
        Check(skip == 32768 + 8192, "BC1 skips the two large mips, skipped " + std::to_string(skip));
        Check(keep == 2048 + 512 + 128 + 32 + 8 + 8 + 8, "BC1 keeps the rest, kept " + std::to_string(keep));
        if (bc1) {
            std::uint32_t w = 0, mips = 0;
            std::memcpy(&w, bc1->data() + 4 + 12, 4);
            std::memcpy(&mips, bc1->data() + 4 + 24, 4);
            Check(w == 64 && mips == 7, "BC1 header rewritten to 64 px and 7 mips");
        }

        auto bc7 = io::ShrinkDds(DdsHeader(128, 128, 2, nullptr, 98), 64, &skip, &keep);
        Check(bc7 && bc7->size() == 4 + 124 + 20, "BC7 keeps the DX10 header");
        Check(skip == 16384 && keep == 4096, "BC7 mip sizes");

        auto small = io::ShrinkDds(DdsHeader(32, 32, 1, "DXT5"), 64, &skip, &keep);
        Check(small && skip == 0 && keep == 1024, "small texture is kept whole");

        auto truncated = DdsHeader(128, 128, 2, nullptr, 98);
        truncated.resize(4 + 124);
        Check(!io::ShrinkDds(truncated, 64, &skip, &keep), "DX10 header cut short is rejected");
        Check(!io::ShrinkDds(std::vector<std::uint8_t>(10, 0), 64, &skip, &keep), "truncated header is rejected");
        auto cube = DdsHeader(64, 64, 1, "DXT1");
        const std::uint32_t caps2 = 0x200;
        std::memcpy(cube.data() + 4 + 108, &caps2, 4);
        Check(!io::ShrinkDds(cube, 64, &skip, &keep), "cubemap is rejected");
        Check(!io::ShrinkDds(DdsHeader(64, 64, 1, "ZZZZ"), 64, &skip, &keep), "unknown format is rejected");
    }

    void Settings() {
        const auto dir = fs::temp_directory_path() / "mtfui_settings_test";
        fs::remove_all(dir);
        io::UiSettings s;
        s.favorites = { "a/b" };
        s.recents = { "c/d", "e/f" };
        s.pauseGame = false;
        s.thumbSize = 150;
        s.panelOpacity = 40;
        s.panels["tiers"] = { 1, 2, 300, 400, true };
        s.lastPreset = "Dragon";
        s.previewActors = { 0x14, 0xABC };
        s.heldActors = { 0x1234, 0x5678 };
        Check(s.Save(dir / "settings.json"), "settings save");
        io::UiSettings r;
        r.Load(dir / "settings.json");
        Check(r.favorites == s.favorites && r.recents == s.recents && r.pauseGame == s.pauseGame && r.thumbSize == s.thumbSize &&
                  r.panelOpacity == s.panelOpacity && r.lastPreset == s.lastPreset && r.previewActors == s.previewActors && r.heldActors == s.heldActors,
            "settings round-trip");
        Check(r.panels.contains("tiers") && r.panels["tiers"].w == 300 && r.panels["tiers"].open, "panel layout round-trip");

        io::WriteTextAtomic(dir / "bad.json", R"({"thumbSize":"big","pauseGame":3,"favorites":{"x":1},"panelOpacity":1e99,"lastPreset":7})");
        io::UiSettings bad;
        bad.Load(dir / "bad.json");
        const io::UiSettings defaults;
        Check(bad.thumbSize == defaults.thumbSize && bad.pauseGame == defaults.pauseGame && bad.favorites.empty() && bad.panelOpacity == defaults.panelOpacity &&
                  bad.lastPreset.empty(),
            "wrong-typed settings fall back to defaults");
        io::WriteTextAtomic(dir / "broken.json", "{");
        io::UiSettings broken;
        broken.Load(dir / "broken.json");
        Check(broken.thumbSize == defaults.thumbSize, "unreadable settings load as defaults");
        fs::remove_all(dir);
    }

    void HiddenPresets(const fs::path& fixtures) {
        const auto dir = fs::temp_directory_path() / "mtfui_hidden_test";
        fs::remove_all(dir);
        fs::copy(fixtures, dir, fs::copy_options::recursive);
        auto j = *io::ReadJson(dir / "presets" / "riDDEN_WET.json");
        j["hidden"] = 1;
        io::WriteJsonStyled(dir / "presets" / "riDDEN_WET.json", j);
        model::Catalogs cat;
        cat.Load(dir);
        std::vector<model::PresetFileInfo> hidden;
        const auto shown = cat.ListPresets(false, &hidden);
        Check(shown.size() == 5, "hidden preset is left out of the list");
        Check(hidden.size() == 1 && hidden.front().name == "riDDEN_WET", "hidden preset is reported separately");
        Check(std::none_of(shown.begin(), shown.end(), [](const model::PresetFileInfo& i) { return i.json.is_null(); }), "listed presets keep their parsed JSON");

        io::WriteTextAtomic(dir / "plugins" / "broken.json", "{ not json");
        model::Catalogs withBroken;
        withBroken.Load(dir);
        Check(!withBroken.loadErrors.empty(), "a broken catalog file is reported");
        Check(withBroken.plugins.size() == cat.plugins.size(), "a broken catalog file doesn't stop the others loading");
        fs::remove_all(dir);
    }

    void OverlaySlots() {
        // 12 Body slots: the MCM preset wants 2 at start 0, Dragon has 3, Back has 1 but was left at an old base.
        model::AreaSlots a;
        a.total = 12;
        a.start = 0;
        a.mcmWant = 2;
        a.presets = { { "Dragon", 2, 3 }, { "Back", 8, 1 } };
        a.textures.assign(12, "");
        a.textures[0] = "mtf\\a.dds";
        a.textures[2] = "mtf\\dragon.dds";
        a.textures[8] = "mtf\\back.dds";
        a.textures[6] = "racemenu\\paint.dds";
        a.textures[11] = "racemenu\\top.dds";

        const auto now = model::CurrentSlots(a);
        Check(now.size() == 12, "one cell per RaceMenu slot");
        Check(now[0].use == model::SlotUse::Mcm && now[1].use == model::SlotUse::Mcm, "the MCM block sits at the start");
        Check(now[4].use == model::SlotUse::Preset && now[4].preset == "Dragon", "a preset fills its stored range");
        Check(now[8].use == model::SlotUse::Preset && now[8].preset == "Back", "a preset left at an old base shows where it is");
        Check(now[6].other && now[11].other && !now[2].other && !now[5].other, "only textures outside MTF's blocks are someone else's");

        auto plan = model::PlanStart(a, 0);
        Check(plan.fits && plan.needed == 6, "MCM block and both presets need 6 slots");
        Check(plan.cells[5].use == model::SlotUse::Preset && plan.cells[5].preset == "Back", "presets pack tight above the MCM block");
        Check(plan.covered.empty() && plan.exposed.size() == 2 && plan.exposed[0] == 6, "other overlays above MTF are exposed to the next preset");

        plan = model::PlanStart(a, 5);
        Check(plan.fits, "start 5 still fits in 12 slots");
        Check(plan.covered.size() == 1 && plan.covered[0] == 6, "moving onto another overlay reports it covered");
        Check(plan.cells[0].use == model::SlotUse::Free && plan.cells[0].texture.empty(), "MTF's old slots are freed in the plan");

        plan = model::PlanStart(a, 9);
        Check(!plan.fits && plan.needed == 6, "start 9 doesn't leave room for 6 slots");
        Check(model::McmLayersAt(a, 11) == 1, "the MCM block is capped to the slots left, like MTF does");

        plan = model::PlanStart(a, 7, "back");
        Check(plan.fits && plan.needed == 5, "a skipped preset (the live preview) is left out, ignoring case");

        Check(model::ListSlots({ 3 }) == "3" && model::ListSlots({ 3, 5 }) == "3 and 5" && model::ListSlots({ 3, 5, 7 }) == "3, 5 and 7", "slot lists read naturally");
    }
}

int main(int argc, char** argv) {
    const fs::path fixtures = argc > 1 ? argv[1] : "tests/fixtures";
    Formatting();
    RoundTripPresets(fixtures);
    Groups(fixtures);
    Catalogs(fixtures);
    AtomicWrite();
    LintRules(fixtures);
    LookAndDescribe(fixtures);
    Waveforms();
    Params();
    Dds();
    Settings();
    HiddenPresets(fixtures);
    OverlaySlots();
    if (g_failures == 0) {
        std::printf("All tests passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
