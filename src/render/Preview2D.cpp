#include "render/Preview2D.h"

#include "render/DdsCache.h"

namespace mtfui::render {
    using namespace ImGuiMCP;

    namespace {
        struct ViewState {
            float zoom = 1.0f;
            ImVec2 center{ 0.5f, 0.5f };
            std::string fittedFor;  // texture set the view was last fitted to
            float fitZoom = 1.0f;
            ImVec2 fitCenter{ 0.5f, 0.5f };
        };
        std::unordered_map<std::string, ViewState> g_views;

        constexpr std::uint32_t kSkinTone = 0xC89F86;
        constexpr float kPadding = 1.15f;
        constexpr float kMaxZoom = 32.0f;

        ImTextureID Tex(const Texture* t) { return reinterpret_cast<ImTextureID>(t->srv); }

        // Square around a UV box, padded and kept inside the texture.
        void Square(float u0, float v0, float u1, float v1, ImVec2& center, float& side) {
            side = std::clamp(std::max(u1 - u0, v1 - v0) * kPadding, 0.02f, 1.0f);
            const float half = side * 0.5f;
            center = { std::clamp((u0 + u1) * 0.5f, half, 1.0f - half), std::clamp((v0 + v1) * 0.5f, half, 1.0f - half) };
        }
    }

    void ContentUV(const Texture& tex, ImVec2& uv0, ImVec2& uv1) {
        ImVec2 c;
        float side = 1.0f;
        Square(tex.u0, tex.v0, tex.u1, tex.v1, c, side);
        uv0 = { c.x - side * 0.5f, c.y - side * 0.5f };
        uv1 = { c.x + side * 0.5f, c.y + side * 0.5f };
    }

    const Texture* RequestCropped(const std::string& path, int baseSize, float displayPx, int maxSize) {
        const auto* base = DdsCache::Get().Request(path, baseSize);
        if (!base) {
            return nullptr;
        }
        ImVec2 c;
        float side = 1.0f;
        Square(base->u0, base->v0, base->u1, base->v1, c, side);
        int want = baseSize;
        while (want < maxSize && want * side < displayPx) {
            want *= 2;
        }
        if (want > baseSize) {
            if (const auto* sharp = DdsCache::Get().Request(path, want)) {
                return sharp;
            }
        }
        return base;
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

        // Open zoomed onto the ink once every layer has loaded; a new tattoo refits.
        std::string setKey;
        float u0 = 1.0f, v0 = 1.0f, u1 = 0.0f, v1 = 0.0f;
        bool allLoaded = true;
        for (std::size_t i = 0; i < in.textures.size() && i < in.layers.size(); ++i) {
            setKey += in.textures[i] + "|";
            if (const auto* t = DdsCache::Get().Request(in.textures[i], textureSize)) {
                u0 = std::min(u0, t->u0), v0 = std::min(v0, t->v0);
                u1 = std::max(u1, t->u1), v1 = std::max(v1, t->v1);
            } else if (!DdsCache::Get().Failed(in.textures[i], textureSize)) {
                allLoaded = false;
            }
        }
        if (allLoaded && view.fittedFor != setKey) {
            view.fittedFor = setKey;
            if (u1 > u0 && v1 > v0) {
                float side = 1.0f;
                Square(u0, v0, u1, v1, view.fitCenter, side);
                view.fitZoom = std::clamp(1.0f / side, 1.0f, kMaxZoom);
            } else {
                view.fitZoom = 1.0f;
                view.fitCenter = { 0.5f, 0.5f };
            }
            view.zoom = view.fitZoom;
            view.center = view.fitCenter;
        }

        if (hovered && io->MouseWheel != 0.0f) {
            const ImVec2 m = GetMousePos();
            const float half = 0.5f / view.zoom;
            const ImVec2 uv{ view.center.x - half + (m.x - origin.x) / size / view.zoom, view.center.y - half + (m.y - origin.y) / size / view.zoom };
            view.zoom = std::clamp(view.zoom * (io->MouseWheel > 0 ? 1.25f : 0.8f), 1.0f, kMaxZoom);
            const float h2 = 0.5f / view.zoom;
            view.center = { uv.x - (m.x - origin.x) / size / view.zoom + h2, uv.y - (m.y - origin.y) / size / view.zoom + h2 };
        }
        if (IsItemActive() && (io->MouseDelta.x != 0.0f || io->MouseDelta.y != 0.0f)) {
            view.center.x -= io->MouseDelta.x / size / view.zoom;
            view.center.y -= io->MouseDelta.y / size / view.zoom;
        }
        if (hovered && IsMouseDoubleClicked(0)) {
            const bool atFit = std::abs(view.zoom - view.fitZoom) < 0.01f && std::abs(view.center.x - view.fitCenter.x) < 0.001f &&
                               std::abs(view.center.y - view.fitCenter.y) < 0.001f;
            view.zoom = atFit ? 1.0f : view.fitZoom;
            view.center = atFit ? ImVec2{ 0.5f, 0.5f } : view.fitCenter;
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

        // Zoomed in, the base copy is too coarse: fetch one whose visible part fills the preview.
        int sharpSize = textureSize;
        while (sharpSize < 4096 && sharpSize < size * view.zoom) {
            sharpSize *= 2;
        }
        bool loading = false;
        for (std::size_t i = 0; i < in.textures.size() && i < in.layers.size(); ++i) {
            const auto* tex = sharpSize > textureSize ? DdsCache::Get().Request(in.textures[i], sharpSize) : nullptr;
            if (!tex) {
                tex = DdsCache::Get().Request(in.textures[i], textureSize);
            }
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
            SetTooltip("Scroll to zoom, drag to pan, double-click to switch between the tattoo and the whole texture.\n"
                       "Colors approximate the in-game look.");
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
            if (const auto* tex = RequestCropped(t, texSize, size, 1024)) {
                ImVec2 uv0, uv1;
                ContentUV(*tex, uv0, uv1);
                ImDrawListManager::AddImage(dl, Tex(tex), origin, end, uv0, uv1, ToImColor(tint, 1.0f));
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
