#pragma once

#include "model/Preset.h"
#include "model/Waveform.h"

namespace mtfui::render {
    struct PreviewInput {
        std::string backdrop;  // skin diffuse texture, empty = flat skin tone
        std::vector<std::string> textures;  // tattoo layers, in overlay order
        std::array<model::Layer, model::kLayerCount> layers;
        model::Pulse pulse;
        const model::Waveform* waveform = nullptr;  // nullptr = built-in cosine
        bool animate = true;
    };

    // Approximates MTF's overlay on a flat UV view: tint multiplies, alpha fades, emissive is
    // drawn as a second tinted pass scaled by the (pulsing) emissive multiplier.
    void DrawPreview(const char* id, const PreviewInput& in, float size, int textureSize = 1024);

    // Small square thumbnail of a tattoo entry's layers on a dark tile.
    void DrawThumb(const std::vector<std::string>& textures, float size, std::uint32_t tint = 0xFFFFFF);

    // Plots one or two cycles of the pulse multiplier.
    void DrawPulseGraph(const model::Pulse& pulse, const model::Waveform* wave, float width, float height);

    std::uint32_t ToImColor(std::uint32_t rgb, float alpha);

    struct Texture;
    // Square UV window around a texture's ink, so thumbnails show the design rather than empty canvas.
    void ContentUV(const Texture& tex, ImGuiMCP::ImVec2& uv0, ImGuiMCP::ImVec2& uv1);

    // Loads at baseSize to find the ink, then asks for a copy big enough that the cropped ink
    // covers displayPx on screen (up to maxSize). Returns the sharpest copy available so far.
    const Texture* RequestCropped(const std::string& path, int baseSize, float displayPx, int maxSize);
}
