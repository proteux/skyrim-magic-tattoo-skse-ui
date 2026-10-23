#pragma once

namespace mtfui::game {
    // Game thread only. Stops the idle orbit camera from circling the target behind the editor, and puts the
    // player's own delay back when released.
    void SetVanityCameraHeld(bool hold);
}
