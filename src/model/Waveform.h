#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mtfui::model {
    struct Waveform {
        enum class Kind { Cos, Triangle, Square, Sawtooth, Keyframes };
        std::string name;  // file stem; empty = built-in cosine
        Kind kind = Kind::Cos;
        double shape = 0.5;  // square duty cycle
        std::vector<std::pair<double, double>> points;  // keyframes (t, v), t ascending in [0, 1]

        // Wave value in [0, 1] at phase in [0, 1).
        double Sample(double phase) const;
    };

    std::optional<Waveform> LoadWaveform(const std::filesystem::path& file);

    // MTF's pulse multiplier at time t (seconds): (1 - depth) + depth * wave, with the optional
    // pause holding the wave at 0 between cycles.
    double PulseMultiplier(const Waveform& wave, double rate, double depthPct, double pause, double t);
}
