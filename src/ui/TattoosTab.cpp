#include "io/JsonStore.h"
#include "model/Describe.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        enum class Source { All, Favorites, Recent, Pack };

        struct Item {
            const model::VisualPack* pack;
            const model::VisualEntry* entry;
            std::string key;  // lowercase "pack/entry"
        };

        struct TattoosTabState {
            Source source = Source::All;
            std::string pack;
            std::string search;
            std::string selected;  // key
            std::string newFrom;   // key of the tattoo a new preset is being named for
            std::string newName;
        };
        TattoosTabState g;

        std::string Key(const model::VisualPack& p, const model::VisualEntry& e) {
            return io::ToLower(p.packId + "/" + e.id);
        }

        // Everything the visible list depends on. The list is rebuilt only when this changes, not every frame.
        struct CollectKey {
            int catalog = -1;
            Source source = Source::All;
            std::string pack, search;
            std::vector<std::string> keys;  // favorites or recents, when those are the source
            bool operator==(const CollectKey&) const = default;
        };
        CollectKey g_collectKey;
        std::vector<Item> g_items;

        std::vector<Item> Collect(App& app);

        const std::vector<Item>& Items(App& app) {
            CollectKey key{ app.cat.generation, g.source, g.pack, g.search, {} };
            if (g.source == Source::Favorites) {
                key.keys = app.settings.favorites;
            } else if (g.source == Source::Recent) {
                key.keys = app.settings.recents;
            }
            if (!(key == g_collectKey)) {
                g_collectKey = std::move(key);
                g_items = Collect(app);
            }
            return g_items;
        }

        // The tier the tattoo goes on: the one being picked for, or the selected one.
        int TargetSlot(App& app) {
            const int slot = app.picking ? app.pickSlot : app.ed.slot;
            return app.HasPreset() ? std::clamp(slot, 0, static_cast<int>(app.Preset().slots.size()) - 1) : 0;
        }

        std::vector<Item> Collect(App& app) {
            std::vector<Item> items;
            auto push = [&](const model::VisualPack& p, const model::VisualEntry& e) {
                if (!MatchesSearch(e.label + " " + e.id + " " + p.label + " " + p.area + " " + e.subject + " " + e.style + " " + e.placement + " " + e.description, g.search)) {
                    return;
                }
                items.push_back({ &p, &e, Key(p, e) });
            };
            if (g.source == Source::Recent || g.source == Source::Favorites) {
                const auto& keys = g.source == Source::Recent ? app.settings.recents : app.settings.favorites;
                for (const auto& k : keys) {
                    const auto slash = k.find('/');
                    if (slash == std::string::npos) {
                        continue;
                    }
                    const auto* p = app.cat.FindPack(k.substr(0, slash));
                    const auto* e = p ? p->Find(k.substr(slash + 1)) : nullptr;
                    if (e) {
                        push(*p, *e);
                    }
                }
                return items;
            }
            for (const auto& p : app.cat.packs) {
                if (g.source == Source::Pack && !model::IEquals(p.packId, g.pack)) {
                    continue;
                }
                for (const auto& e : p.entries) {
                    push(p, e);
                }
            }
            return items;
        }

        // Lays items out left to right and starts a new row when the next one would not fit.
        struct Flow {
            float right = GetCursorScreenPos().x + GetContentRegionAvail().x;
            bool first = true;
            void Next(float width) {
                if (!first && GetItemRectMax().x + GetStyle()->ItemSpacing.x + width <= right) {
                    SameLine();
                }
                first = false;
            }
        };

        float ButtonWidth(const char* label) {
            return CalcTextSize(label).x + GetStyle()->FramePadding.x * 2.0f;
        }

        std::string SourceLabel(App& app) {
            switch (g.source) {
                case Source::Favorites:
                    return "Favorites";
                case Source::Recent:
                    return "Recently used";
                case Source::Pack:
                    if (const auto* p = app.cat.FindPack(g.pack)) {
                        return p->label;
                    }
                    return g.pack;
                default:
                    return "All";
            }
        }

        void DrawSourceCombo(App& app, float width) {
            SetNextItemWidth(width);
            const std::string preview = (g.source == Source::Pack ? "Pack: " : "Show: ") + SourceLabel(app);
            if (!BeginCombo("##tsource", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
                return;
            }
            std::size_t total = 0;
            for (const auto& p : app.cat.packs) {
                total += p.entries.size();
            }
            if (Selectable(("All (" + std::to_string(total) + ")").c_str(), g.source == Source::All)) {
                g.source = Source::All;
            }
            if (Selectable(("Favorites (" + std::to_string(app.settings.favorites.size()) + ")").c_str(), g.source == Source::Favorites)) {
                g.source = Source::Favorites;
            }
            if (Selectable("Recently used", g.source == Source::Recent)) {
                g.source = Source::Recent;
            }
            SeparatorText("Packs");
            for (const auto& p : app.cat.packs) {
                const bool sel = g.source == Source::Pack && model::IEquals(p.packId, g.pack);
                if (Selectable((p.label + " (" + std::to_string(p.entries.size()) + ")##" + p.packId).c_str(), sel)) {
                    g.source = Source::Pack;
                    g.pack = p.packId;
                }
                if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    SetTooltip("%s\nArea: %s\n%s", p.packId.c_str(), p.area.c_str(), io::PathText(p.file.filename()).c_str());
                }
            }
            if (app.cat.packs.empty()) {
                TextDisabled("No visual packs found in MagicTattoosFramework/visuals.");
            }
            EndCombo();
        }

        void DrawToolbar(App& app) {
            Flow flow;
            flow.Next(220.0f);
            SearchBox("##tsearch", g.search, 220.0f, "Search tattoos");
            flow.Next(230.0f);
            DrawSourceCombo(app, 230.0f);
        }

        float FooterHeight() {
            return GetFrameHeight() + GetStyle()->ItemSpacing.y;
        }

        void DrawFooter(App& app) {
            SetNextItemWidth(180.0f);
            SliderInt("##thumbsize", &app.settings.thumbSize, 64, 256, "Thumbnails %d px");
            if (IsItemDeactivatedAfterEdit()) {
                app.SaveSettings();
            }
            if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                SetTooltip("Ctrl+mouse wheel over the grid also zooms.");
            }
            const char* detailsLabel = app.settings.showDetails ? "Hide details" : "Show details";
            const float right = GetWindowContentRegionMax().x - ButtonWidth(detailsLabel);
            SameLine();
            SetCursorPosX(std::max(GetCursorPosX(), right));
            if (Button(detailsLabel)) {
                app.settings.showDetails = !app.settings.showDetails;
                app.SaveSettings();
            }
        }

        bool AnyFilter() {
            return !g.search.empty() || g.source != Source::All;
        }

        void DrawChips(App& app, std::size_t count) {
            if (!AnyFilter()) {
                return;
            }
            Flow flow;
            auto chip = [&](const char* id, const std::string& text, auto&& clear) {
                const std::string label = text + "  x##" + id;
                flow.Next(ButtonWidth(label.substr(0, label.find("##")).c_str()));
                if (SmallButton(label.c_str())) {
                    clear();
                }
                if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    SetTooltip("Remove this filter");
                }
            };
            if (!g.search.empty()) {
                chip("search", "Search: " + g.search, [] { g.search.clear(); });
            }
            if (g.source != Source::All) {
                chip("source", (g.source == Source::Pack ? "Pack: " : "Show: ") + SourceLabel(app), [] { g.source = Source::All; });
            }
            flow.Next(ButtonWidth("Clear all"));
            if (SmallButton("Clear all")) {
                g.search.clear();
                g.source = Source::All;
            }
            const std::string countText = std::to_string(count) + (count == 1 ? " tattoo" : " tattoos");
            flow.Next(CalcTextSize(countText.c_str()).x);
            TextDisabled("%s", countText.c_str());
        }

        void UseSelected(App& app, const Item& it) {
            if (!app.HasPreset()) {
                return;
            }
            const int slot = TargetSlot(app);
            app.UseEntry(slot, it.pack->packId, it.entry->id);
            app.Toast(it.entry->label + " set on " + model::SlotLabel(app.Preset(), slot));
            if (app.picking) {
                app.picking = false;
                app.RequestTab(Tab::Presets);
            }
        }

        bool g_zoomUnsaved = false;

        void DrawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                const float a = -1.5707963f + i * 0.6283185f;
                const float rr = (i % 2) ? r * 0.45f : r;
                pts[i] = ImVec2{ c.x + std::cos(a) * rr, c.y + std::sin(a) * rr };
            }
            for (int i = 0; i < 10; ++i) {
                ImDrawListManager::AddTriangleFilled(dl, c, pts[i], pts[(i + 1) % 10], col);
            }
        }

        void DrawHoverPreview(App& app, const Item& it, float thumb) {
            BeginTooltip();
            const float zoom = std::clamp(thumb * 2.5f, 256.0f, 512.0f);
            const ImVec2 p = GetCursorScreenPos();
            Dummy(ImVec2{ zoom, zoom });
            render::DrawThumbAt(GetWindowDrawList(), p, zoom, it.entry->textures.empty() ? std::string{} : it.entry->textures.front());
            PushTextWrapPos(GetCursorPosX() + zoom);
            TextUnformatted(it.entry->label.c_str());
            TextDisabled("%s  (%s)", it.pack->label.c_str(), it.pack->area.c_str());
            std::string tags;
            for (const auto* t : { &it.entry->subject, &it.entry->style, &it.entry->placement }) {
                if (!t->empty()) {
                    tags += (tags.empty() ? "" : ", ") + *t;
                }
            }
            if (!tags.empty()) {
                TextDisabled("%s", tags.c_str());
            }
            if (const auto* users = app.usage.Entry(it.pack->packId, it.entry->id)) {
                TextColored(kGood, "Used by %d preset%s", static_cast<int>(users->size()), users->size() == 1 ? "" : "s");
            }
            PopTextWrapPos();
            EndTooltip();
        }

        void DrawGrid(App& app, const std::vector<Item>& items, float width) {
            const auto* input = GetIO();
            if (!BeginChild("##tgrid", ImVec2{ width, -FooterHeight() }, ImGuiChildFlags_Border, input->KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0)) {
                EndChild();
                return;
            }
            bool zoomed = false;
            if (input->KeyCtrl && input->MouseWheel != 0.0f && IsWindowHovered()) {
                const int before = app.settings.thumbSize;
                app.settings.thumbSize = std::clamp(app.settings.thumbSize + (input->MouseWheel > 0.0f ? 16 : -16), 64, 256);
                zoomed = app.settings.thumbSize != before;
                g_zoomUnsaved = g_zoomUnsaved || zoomed;
            }
            if (app.picking && app.HasPreset()) {
                TextColored(kAccent, "Choosing a tattoo for %s: double-click one, or", model::SlotLabel(app.Preset(), TargetSlot(app)).c_str());
                SameLine();
                if (SmallButton("cancel")) {
                    app.picking = false;
                    app.RequestTab(Tab::Presets);
                }
            }
            if (items.empty()) {
                TextDisabled(g.source == Source::Favorites ? "No favorites yet. Select a tattoo and click the star." : "Nothing matches.");
                EndChild();
                return;
            }
            const float thumb = static_cast<float>(app.settings.thumbSize);
            const float cell = thumb + 8.0f;
            const int columns = std::max(1, static_cast<int>(GetContentRegionAvail().x / cell));
            const float labelH = GetTextLineHeight() * (thumb >= 128.0f ? 2.0f : 1.0f);
            const int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
            const float rowH = thumb + labelH + 10.0f;
            const float viewH = GetWindowHeight();

            if (zoomed) {
                const auto sel = std::find_if(items.begin(), items.end(), [](const Item& i) { return i.key == g.selected; });
                if (sel != items.end()) {
                    const float row = static_cast<float>((sel - items.begin()) / columns);
                    SetScrollY(std::max(0.0f, row * rowH - (viewH - rowH) * 0.5f));
                }
            }

            std::string current;
            if (app.HasPreset()) {
                const auto v = model::ResolveVisual(app.Preset(), TargetSlot(app));
                if (!v.none && !v.packId.empty()) {
                    current = io::ToLower(v.packId + "/" + v.entryId);
                }
            }

            // Only rows on screen are drawn, so only their textures get requested.
            const float scrollY = GetScrollY();
            const ImVec2 origin = GetCursorScreenPos();
            const int firstRow = std::max(0, static_cast<int>(scrollY / rowH) - 1);
            const int lastRow = std::min(rows, static_cast<int>((scrollY + viewH) / rowH) + 2);
            auto* dl = GetWindowDrawList();
            for (int r = firstRow; r < lastRow; ++r) {
                for (int c = 0; c < columns; ++c) {
                    const int idx = r * columns + c;
                    if (idx >= static_cast<int>(items.size())) {
                        break;
                    }
                    const auto& it = items[idx];
                    const ImVec2 pos{ origin.x + c * cell, origin.y + r * rowH };
                    SetCursorScreenPos(pos);
                    // By tattoo, not position: an open context menu must stay with its tattoo when filters change.
                    PushID(it.key.c_str());
                    const ImVec2 cellEnd{ pos.x + thumb, pos.y + thumb + labelH + 4.0f };
                    if (Selectable("##cell", g.selected == it.key, ImGuiSelectableFlags_AllowDoubleClick, ImVec2{ thumb, thumb + labelH + 4.0f })) {
                        g.selected = it.key;
                        if (IsMouseDoubleClicked(0)) {
                            UseSelected(app, it);
                        }
                    }
                    const bool hovered = IsItemHovered(ImGuiHoveredFlags_DelayShort);
                    if (BeginPopupContextItem("##tctx")) {
                        g.selected = it.key;
                        if (MenuItem("New preset from this tattoo...")) {
                            g.newFrom = it.key;
                            g.newName = it.entry->label;
                        }
                        const bool fav = app.settings.IsFavorite(it.key);
                        if (MenuItem(fav ? "Unfavorite" : "Favorite")) {
                            app.settings.ToggleFavorite(it.key);
                            app.SaveSettings();
                        }
                        EndPopup();
                    }
                    render::DrawThumbAt(dl, pos, thumb, it.entry->textures.empty() ? std::string{} : it.entry->textures.front());
                    if (app.settings.IsFavorite(it.key)) {
                        const ImVec2 c{ pos.x + thumb - 11.0f, pos.y + 11.0f };
                        ImDrawListManager::AddCircleFilled(dl, c, 10.0f, 0xB0000000, 20);
                        DrawStar(dl, c, 7.5f, GetColorU32(kWarn));
                    }
                    if (app.usage.Entry(it.pack->packId, it.entry->id)) {
                        const ImVec2 c{ pos.x + 9.0f, pos.y + 9.0f };
                        ImDrawListManager::AddCircleFilled(dl, c, 6.5f, 0xB0000000, 16);
                        ImDrawListManager::AddCircleFilled(dl, c, 4.5f, GetColorU32(kGood), 16);
                    }
                    if (!current.empty() && it.key == current) {
                        ImDrawListManager::AddRect(dl, ImVec2{ pos.x - 2.0f, pos.y - 2.0f }, ImVec2{ cellEnd.x + 2.0f, cellEnd.y + 2.0f }, GetColorU32(kAccent), 4.0f, 0, 2.0f);
                    }
                    ImDrawListManager::PushClipRect(dl, ImVec2{ pos.x, pos.y + thumb }, cellEnd, true);
                    ImDrawListManager::AddText(dl, GetFont(), GetFontSize(), ImVec2{ pos.x + 2, pos.y + thumb + 2 }, GetColorU32(ImGuiCol_Text, 1.0f),
                        it.entry->label.c_str(), nullptr, thumb >= 128.0f ? thumb - 4.0f : 0.0f);
                    ImDrawListManager::PopClipRect(dl);
                    if (hovered && !IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                        DrawHoverPreview(app, it, thumb);
                    }
                    PopID();
                }
            }
            SetCursorScreenPos(ImVec2{ origin.x, origin.y + rows * rowH });
            Dummy(ImVec2{ 1, 1 });
            EndChild();
        }

        void DrawDetails(App& app, const std::vector<Item>& items) {
            if (!BeginChild("##tdetail", ImVec2{ 0, -FooterHeight() }, ImGuiChildFlags_Border)) {
                EndChild();
                return;
            }
            const auto found = std::find_if(items.begin(), items.end(), [](const Item& i) { return i.key == g.selected; });
            if (found == items.end()) {
                TextWrapped("Select a tattoo to see it larger, check which presets use it, or put it on a tier.");
                EndChild();
                return;
            }
            const auto& it = *found;
            Text("%s", it.entry->label.c_str());
            SameLine();
            const bool fav = app.settings.IsFavorite(it.key);
            if (SmallButton(fav ? "Unfavorite" : "Favorite")) {
                app.settings.ToggleFavorite(it.key);
                app.SaveSettings();
            }
            TextDisabled("%s  /  %s  (%s)", it.pack->label.c_str(), it.entry->id.c_str(), it.pack->area.c_str());

            render::PreviewInput in;
            in.textures = it.entry->textures;
            const int slot = TargetSlot(app);
            if (app.HasPreset()) {
                in.layers = app.Preset().slots[slot].layers;
                in.pulse = app.Preset().slots[slot].pulse;
                in.waveform = app.cat.FindWaveform(in.pulse.waveform);
            }
            render::DrawPreview("##tpreview", in, std::min(GetContentRegionAvail().x, 360.0f));
            if (app.HasPreset()) {
                TextDisabled("Shown with the colors of %s.", model::SlotLabel(app.Preset(), slot).c_str());
            }

            if (app.HasPreset()) {
                if (Button(("Use for " + model::SlotLabel(app.Preset(), slot)).c_str())) {
                    UseSelected(app, it);
                }
            } else {
                TextDisabled("Open a preset to use this tattoo.");
            }

            Spacing();
            if (BeginTable("##tinfo", 2)) {
                TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, CalcTextSize("Placement").x);
                TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
                auto row = [](const char* k, const std::string& v) {
                    if (v.empty()) {
                        return;
                    }
                    TableNextRow();
                    TableNextColumn();
                    TextDisabled("%s", k);
                    TableNextColumn();
                    TextWrapped("%s", v.c_str());
                };
                row("Subject", it.entry->subject);
                row("Style", it.entry->style);
                row("Placement", it.entry->placement);
                row("Layers", std::to_string(it.entry->textures.size()));
                EndTable();
            }
            if (!it.entry->description.empty()) {
                TextWrapped("%s", it.entry->description.c_str());
            }
            SeparatorText("Used by");
            if (const auto* users = app.usage.Entry(it.pack->packId, it.entry->id)) {
                for (const auto& u : *users) {
                    BulletText("%s", u.c_str());
                }
            } else {
                TextDisabled("None of your presets");
            }
            if (TreeNode("Texture files")) {
                for (const auto& t : it.entry->textures) {
                    TextWrapped("%s", t.c_str());
                }
                TreePop();
            }
            EndChild();
        }

        void DrawNewPresetPopup(App& app) {
            if (!g.newFrom.empty() && !IsPopupOpen("New preset from tattoo")) {
                OpenPopup("New preset from tattoo");
            }
            if (!BeginPopupModal("New preset from tattoo", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }
            const auto slash = g.newFrom.find('/');
            const auto* pack = slash == std::string::npos ? nullptr : app.cat.FindPack(g.newFrom.substr(0, slash));
            const auto* entry = pack ? pack->Find(g.newFrom.substr(slash + 1)) : nullptr;
            if (entry) {
                TextDisabled("Default look: %s (%s)", entry->label.c_str(), pack->label.c_str());
            }
            if (app.Dirty()) {
                TextColored(kWarn, "You'll be asked what to do with the unsaved changes to the open preset.");
            }
            switch (NamePrompt(g.newName, app.UniqueName(g.newName), entry != nullptr)) {
                case NamePromptResult::Confirmed:
                    app.picking = false;
                    app.CreateNew(g.newName, pack->packId, entry->id);
                    app.RequestTab(Tab::Presets);
                    g.newFrom.clear();
                    CloseCurrentPopup();
                    break;
                case NamePromptResult::Cancelled:
                    g.newFrom.clear();
                    CloseCurrentPopup();
                    break;
                default:
                    break;
            }
            EndPopup();
        }
    }

    void DrawTattoosTab(App& app) {
        if (g_zoomUnsaved && !GetIO()->KeyCtrl) {
            g_zoomUnsaved = false;
            app.SaveSettings();
        }
        DrawToolbar(app);
        const auto& items = Items(app);
        DrawChips(app, items.size());
        // The chips may have just cleared a filter.
        const auto& shown = Items(app);
        if (app.settings.showDetails) {
            const float details = std::max(300.0f, GetContentRegionAvail().x * 0.3f);
            DrawGrid(app, shown, GetContentRegionAvail().x - details - GetStyle()->ItemSpacing.x);
            SameLine();
            DrawDetails(app, shown);
        } else {
            DrawGrid(app, shown, 0.0f);
        }
        DrawFooter(app);
        DrawNewPresetPopup(app);
    }
}
