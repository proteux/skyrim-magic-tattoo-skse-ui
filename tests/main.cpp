// Offline checks against copies of real MTF data in tests/fixtures.
#include "io/JsonStore.h"
#include "io/StyledJson.h"
#include "model/Catalogs.h"
#include "model/Describe.h"
#include "model/Lint.h"
#include "model/Preset.h"
#include "model/Usage.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

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

        const auto presets = cat.ListPresets(false);
        Check(presets.size() == 6, "lists the fixture presets");
        const auto usage = model::BuildUsage(cat, presets);
        Check(!usage.entries.empty(), "usage index finds tattoo entries");
        const auto* ostim = usage.Plugin("mtf.ostim");
        Check(ostim && !ostim->empty(), "usage index finds plugin users");
    }

    void AtomicWrite() {
        const auto dir = fs::temp_directory_path() / "mtfui_tests";
        fs::create_directories(dir);
        const auto file = dir / "atomic.json";
        Check(io::WriteTextAtomic(file, "one"), "atomic write creates");
        Check(io::WriteTextAtomic(file, "two"), "atomic write replaces");
        Check(io::ReadText(file).value_or("") == "two", "atomic write content");
        for (int i = 0; i < 3; ++i) {
            Check(io::Backup(file, dir / "backups", 2), "backup");
        }
        fs::remove_all(dir);
    }
}

int main(int argc, char** argv) {
    const fs::path fixtures = argc > 1 ? argv[1] : "tests/fixtures";
    Formatting();
    RoundTripPresets(fixtures);
    Catalogs(fixtures);
    AtomicWrite();
    if (g_failures == 0) {
        std::printf("All tests passed\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
