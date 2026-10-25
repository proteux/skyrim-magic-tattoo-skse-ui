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

    // Keeps at most one NPC in place by blocking its movement, and tells a follower to wait; 0 lets the held one
    // go. AI keeps running so live previews still redraw. Never touches the player, and releasing only undoes
    // what holding changed. Returns the actor now held, or 0.
    RE::FormID HoldStill(RE::FormID id);
    // After a load: lets an actor held in an earlier session move again.
    void ReleaseLeftover(RE::FormID id);
    // Right before a save is written, so the save never has the NPC held. Returns the actor that was released, or 0.
    RE::FormID ReleaseForSave();

    // Normalizes a texture path to the form BSResource expects ("textures\..." prefix, backslashes).
    std::string TexturePath(std::string path);
}
