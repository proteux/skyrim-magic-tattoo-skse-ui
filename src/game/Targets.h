#pragma once

namespace mtfui::game {
    struct ActorInfo {
        RE::FormID id = 0;
        std::string name;
        float distance = 0.0f;
        bool player = false;
        bool female = false;
    };

    // Diffuse skin textures for the 2D preview backdrop, relative to Data/textures.
    struct SkinInfo {
        std::string body;
        std::string hands;
        std::string feet;
        bool female = false;
    };

    // Must be called on the game thread.
    ActorInfo PlayerInfo();
    std::optional<ActorInfo> CrosshairActor();
    std::optional<ActorInfo> ConsoleActor();
    std::vector<ActorInfo> NearbyActors(float radius = 2048.0f);
    std::optional<ActorInfo> Describe(RE::FormID id);
    SkinInfo SkinFor(RE::FormID id);

    // Normalizes a texture path to the form BSResource expects ("textures\..." prefix, backslashes).
    std::string TexturePath(std::string path);
}
