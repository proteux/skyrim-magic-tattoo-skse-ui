#include "model/Waveform.h"

#include "io/JsonStore.h"
#include "io/UiSettings.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mtfui::model {
    double Waveform::Sample(double phase) const {
        phase = phase - std::floor(phase);
        switch (kind) {
            case Kind::Triangle:
                return phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
            case Kind::Square:
                return phase < shape ? 1.0 : 0.0;
            case Kind::Sawtooth:
                return phase;
            case Kind::Keyframes: {
                if (points.empty()) {
                    return 0.0;
                }
                if (phase <= points.front().first) {
                    return points.front().second;
                }
                for (std::size_t i = 1; i < points.size(); ++i) {
                    const auto& [t1, v1] = points[i];
                    if (phase <= t1) {
                        const auto& [t0, v0] = points[i - 1];
                        const double span = t1 - t0;
                        return span <= 0.0 ? v1 : v0 + (v1 - v0) * (phase - t0) / span;
                    }
                }
                return points.back().second;
            }
            case Kind::Cos:
            default:
                return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * phase);
        }
    }

    std::optional<Waveform> LoadWaveform(const std::filesystem::path& file) {
        auto j = io::ReadJson(file);
        if (!j || !j->is_object()) {
            return std::nullopt;
        }
        Waveform w;
        w.name = file.stem().string();
        const auto kind = io::ToLower(j->value("kind", std::string{ "cos" }));
        if (kind == "triangle") {
            w.kind = Waveform::Kind::Triangle;
        } else if (kind == "square") {
            w.kind = Waveform::Kind::Square;
            if (auto s = j->find("shape"); s != j->end() && s->is_number()) {
                w.shape = std::clamp(s->get<double>(), 0.0, 1.0);
            }
        } else if (kind == "sawtooth" || kind == "saw") {
            w.kind = Waveform::Kind::Sawtooth;
        } else if (kind == "keyframes") {
            w.kind = Waveform::Kind::Keyframes;
            if (auto p = j->find("points"); p != j->end() && p->is_array()) {
                for (const auto& pt : *p) {
                    if (pt.is_object() && w.points.size() < 16) {
                        w.points.emplace_back(std::clamp(pt.value("t", 0.0), 0.0, 1.0), std::clamp(pt.value("v", 0.0), 0.0, 1.0));
                    }
                }
                std::stable_sort(w.points.begin(), w.points.end(), [](auto& a, auto& b) { return a.first < b.first; });
            }
        }
        return w;
    }

    double PulseMultiplier(const Waveform& wave, double rate, double depthPct, double pause, double t) {
        if (rate <= 0.0 || depthPct <= 0.0) {
            return 1.0;
        }
        const double depth = std::clamp(depthPct / 100.0, 0.0, 1.0);
        const double period = 1.0 / rate;
        double value;
        if (pause > 0.0) {
            const double cycle = period + pause;
            const double local = std::fmod(t, cycle);
            value = local < period ? wave.Sample(local / period) : 0.0;
        } else {
            value = wave.Sample(t * rate);
        }
        return (1.0 - depth) + depth * value;
    }
}
