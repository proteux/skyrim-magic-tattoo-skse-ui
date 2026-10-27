#pragma once

namespace mtfui::game {
    // RaceMenu hides an overlay slot that has no texture when the body's 3D is built, and doesn't show it again when a
    // texture is written there later. A tattoo MTF draws into such a slot stays invisible until the 3D is rebuilt, so
    // the player's hidden slots that now hold a texture are shown twice a second.
    void WatchOverlayVisibility();

    // Shows the actor's hidden overlay slots that hold a texture. Main thread only. Returns how many it showed.
    int ShowDrawnOverlays(RE::Actor* actor);
}
