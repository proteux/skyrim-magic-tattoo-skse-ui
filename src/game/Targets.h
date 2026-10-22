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

    // Keeps at most one NPC in place by switching its AI off; 0 lets the held one go. Never touches the
    // player, and only switches back on AI that it switched off. Returns the actor now held, or 0.
    RE::FormID HoldStill(RE::FormID id);
    // After a load: switches an actor held in an earlier session back on, if its AI is still off.
    void ReleaseLeftover(RE::FormID id);

    // Normalizes a texture path to the form BSResource expects ("textures\..." prefix, backslashes).
    std::string TexturePath(std::string path);
}
