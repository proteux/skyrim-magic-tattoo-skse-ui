#pragma once

namespace mtfui::game {
    // RaceMenu hides an overlay slot that has no texture when the body's 3D is built, and doesn't show it again when a
    // texture is written there later. A tattoo MTF draws into such a slot stays invisible until the 3D is rebuilt, so
    // after anything that makes MTF draw on the player, their hidden slots that now hold a texture are shown. MTF
    // draws on its own tick, so the slots are checked twice a second for a while; a new call extends the window.
    // Nothing re-hides a shown slot until the next rebuild.
    void WatchOverlaysFor(std::chrono::milliseconds window);

    // Shows the actor's hidden overlay slots that hold a texture. Main thread only. Returns how many it showed.
    int ShowDrawnOverlays(RE::Actor* actor);
}
