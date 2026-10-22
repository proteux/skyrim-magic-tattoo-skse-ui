#include "render/Preview2D.h"

#include "render/DdsCache.h"

namespace mtfui::render {
    using namespace ImGuiMCP;

    namespace {
        struct ViewState {
            float zoom = 1.0f;
            ImVec2 center{ 0.5f, 0.5f };
        };
        std::unordered_map<std::string, ViewState> g_views;

        constexpr std::uint32_t kSkinTone = 0xC89F86;

        ImTextureID Tex(const Texture* t) { return reinterpret_cast<ImTextureID>(t->srv); }
    }

    std::uint32_t ToImColor(std::uint32_t rgb, float alpha) {
        const auto r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
        const auto a = static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return IM_COL32(r, g, b, a);
    }

    void DrawPreview(const char* id, const PreviewInput& in, float size, int textureSize) {
        auto& view = g_views[id];
        const ImVec2 origin = GetCursorScreenPos();
        const ImVec2 end{ origin.x + size, origin.y + size };
        InvisibleButton(id, ImVec2{ size, size });
        const bool hovered = IsItemHovered();
        auto* io = GetIO();

        if (hovered && io->MouseWheel != 0.0f) {
            const ImVec2 m = GetMousePos();
            const float half = 0.5f / view.zoom;
            const ImVec2 uv{ view.center.x - half + (m.x - origin.x) / size / view.zoom, view.center.y - half + (m.y - origin.y) / size / view.zoom };
            view.zoom = std::clamp(view.zoom * (io->MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, 8.0f);
            const float h2 = 0.5f / view.zoom;
            view.center = { uv.x - (m.x - origin.x) / size / view.zoom + h2, uv.y - (m.y - origin.y) / size / view.zoom + h2 };
        }
        if (IsItemActive() && (io->MouseDelta.x != 0.0f || io->MouseDelta.y != 0.0f)) {
            view.center.x -= io->MouseDelta.x / size / view.zoom;
            view.center.y -= io->MouseDelta.y / size / view.zoom;
        }
        if (hovered && IsMouseDoubleClicked(0)) {
            view = {};
        }
        const float half = 0.5f / view.zoom;
        view.center.x = std::clamp(view.center.x, half, 1.0f - half);
        view.center.y = std::clamp(view.center.y, half, 1.0f - half);
        const ImVec2 uv0{ view.center.x - half, view.center.y - half };
        const ImVec2 uv1{ view.center.x + half, view.center.y + half };

        auto* dl = GetWindowDrawList();
        ImDrawListManager::AddRectFilled(dl, origin, end, ToImColor(kSkinTone, 1.0f), 0.0f, 0);
        if (!in.backdrop.empty()) {
            if (const auto* skin = DdsCache::Get().Request(in.backdrop, textureSize)) {
                ImDrawListManager::AddImage(dl, Tex(skin), origin, end, uv0, uv1, IM_COL32_WHITE);
            }
        }

        double pulseMult = 1.0;
        if (in.animate && in.pulse.Enabled()) {
            static const model::Waveform kCosine;
            pulseMult = model::PulseMultiplier(in.waveform ? *in.waveform : kCosine, in.pulse.rate, static_cast<double>(in.pulse.depth), in.pulse.pause, GetTime());
        }

        bool loading = false;
        for (std::size_t i = 0; i < in.textures.size() && i < in.layers.size(); ++i) {
            const auto* tex = DdsCache::Get().Request(in.textures[i], textureSize);
            if (!tex) {
                loading = loading || !DdsCache::Get().Failed(in.textures[i], textureSize);
                continue;
            }
            const auto& layer = in.layers[i];
            const float alpha = static_cast<float>(layer.alpha) / 100.0f;
            ImDrawListManager::AddImage(dl, Tex(tex), origin, end, uv0, uv1, ToImColor(layer.tint, alpha));
            const float glow = static_cast<float>(layer.emissiveMult * pulseMult);
            if (glow > 0.0f) {
                // Emissive shows as light on top of the ink; saturate around a multiplier of 4.
                ImDrawListManager::AddImage(dl, Tex(tex), origin, end, uv0, uv1, ToImColor(layer.emissive, std::min(1.0f, glow / 4.0f) * alpha));
            }
        }

        ImDrawListManager::AddRect(dl, origin, end, GetColorU32(ImGuiCol_Border, 1.0f), 0.0f, 0, 1.0f);
        if (loading) {
            const ImVec2 p{ origin.x + 8, origin.y + 8 };
            ImDrawListManager::AddText(dl, p, IM_COL32(255, 255, 255, 220), "Loading texture...");
        }
        if (view.zoom > 1.0f) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1fx", view.zoom);
            ImDrawListManager::AddText(dl, ImVec2{ origin.x + 8, end.y - 22 }, IM_COL32(255, 255, 255, 200), buf);
        }
        if (hovered) {
            SetTooltip("Scroll to zoom, drag to pan, double-click to reset.\nColors approximate the in-game look.");
        }
    }

    void DrawThumb(const std::vector<std::string>& textures, float size, std::uint32_t tint) {
        const ImVec2 origin = GetCursorScreenPos();
        const ImVec2 end{ origin.x + size, origin.y + size };
        auto* dl = GetWindowDrawList();
        ImDrawListManager::AddRectFilled(dl, origin, end, ToImColor(kSkinTone, 1.0f), 4.0f, 0);
        bool any = false;
        bool failed = false;
        const int texSize = size > 160.0f ? 512 : 256;
        for (const auto& t : textures) {
            if (const auto* tex = DdsCache::Get().Request(t, texSize)) {
                ImDrawListManager::AddImage(dl, Tex(tex), origin, end, ImVec2{ 0, 0 }, ImVec2{ 1, 1 }, ToImColor(tint, 1.0f));
                any = true;
            } else {
                failed = failed || DdsCache::Get().Failed(t, texSize);
            }
        }
        if (!any) {
            const char* label = failed ? "missing" : "...";
            ImDrawListManager::AddText(dl, ImVec2{ origin.x + 6, origin.y + 6 }, IM_COL32(40, 30, 30, 255), label);
        }
        Dummy(ImVec2{ size, size });
    }

    void DrawPulseGraph(const model::Pulse& pulse, const model::Waveform* wave, float width, float height) {
        static const model::Waveform kCosine;
        const auto& w = wave ? *wave : kCosine;
        constexpr int kSamples = 120;
        float values[kSamples];
        const double period = pulse.rate > 0.0 ? 1.0 / pulse.rate + pulse.pause : 1.0;
        const double span = period * 2.0;
        for (int i = 0; i < kSamples; ++i) {
            const double t = span * i / (kSamples - 1);
            values[i] = static_cast<float>(model::PulseMultiplier(w, pulse.rate, static_cast<double>(pulse.depth), pulse.pause, t));
        }
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%.2fs per cycle", period);
        PlotLines("##pulse", values, kSamples, 0, pulse.rate > 0.0 ? overlay : "no pulse", 0.0f, 1.0f, ImVec2{ width, height });
    }
}
