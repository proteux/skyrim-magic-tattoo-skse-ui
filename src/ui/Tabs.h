#pragma once

namespace mtfui::ui {
    class App;

    void DrawTargetBar(App& app);
    void DrawPresetsTab(App& app);
    void DrawTattoosTab(App& app);
    void DrawGeneralTab(App& app);
    void DrawPluginsTab(App& app);

    // Pieces of the Presets tab that also stand alone as panels of the split layout.
    void DrawLibraryPanel(App& app);
    void DrawTiersPanel(App& app);
    void DrawInspectorPanel(App& app);
}
