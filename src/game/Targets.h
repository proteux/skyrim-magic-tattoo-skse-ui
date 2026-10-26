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

    // Keeps at most one NPC in place with Papyrus SetDontMove; 0 lets the held one go. AI keeps running so live
    // previews still redraw. Never touches the player. Returns the actor now held, or 0.
    RE::FormID HoldStill(RE::FormID id);
    // While holding: logs the actor moving anyway, with its current package.
    void CheckHold();
    // After a load: lets any of these actors that this plugin held when the save was made move again.
    void ReleaseLeftover(const std::vector<RE::FormID>& ids);
    // Right before a save is written, so the save never has the NPC held. Returns the actor that was released, or 0.
    RE::FormID ReleaseForSave();

    // Normalizes a texture path to the form BSResource expects ("textures\..." prefix, backslashes).
    std::string TexturePath(std::string path);
}
