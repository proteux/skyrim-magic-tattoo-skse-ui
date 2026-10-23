#include "io/JsonStore.h"
#include "model/Describe.h"
#include "render/DdsCache.h"
#include "render/Preview2D.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

#include <set>

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
            std::string area;  // "" = any
            std::string subject;
            std::string style;
            std::string placement;
            std::string selected;  // key
            std::string newFrom;   // key of the tattoo a new preset is being named for
            std::string newName;
        };
        TattoosTabState g;

        std::string Key(const model::VisualPack& p, const model::VisualEntry& e) {
            return io::ToLower(p.packId + "/" + e.id);
        }

        bool FilterCombo(const char* label, std::string& value, const std::set<std::string>& options) {
            bool changed = false;
            SetNextItemWidth(-1);
            if (BeginCombo(label, value.empty() ? "Any" : value.c_str())) {
                if (Selectable("Any", value.empty())) {
                    value.clear();
                    changed = true;
                }
                for (const auto& o : options) {
                    if (Selectable(o.c_str(), o == value)) {
                        value = o;
                        changed = true;
                    }
                }
                EndCombo();
            }
            return changed;
        }

        // Everything the visible list depends on. The list is rebuilt only when this changes, not every frame.
        struct CollectKey {
            int catalog = -1;
            Source source = Source::All;
            std::string pack, search, area, subject, style, placement;
            std::vector<std::string> keys;  // favorites or recents, when those are the source
            bool operator==(const CollectKey&) const = default;
        };
        CollectKey g_collectKey;
        std::vector<Item> g_items;

        struct FilterOptions {
            int catalog = -1;
            std::set<std::string> areas, subjects, styles, placements;
        };
        FilterOptions g_filters;

        std::vector<Item> Collect(App& app);

        const std::vector<Item>& Items(App& app) {
            CollectKey key{ app.cat.generation, g.source, g.pack, g.search, g.area, g.subject, g.style, g.placement, {} };
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

        const FilterOptions& Filters(App& app) {
            if (g_filters.catalog != app.cat.generation) {
                g_filters = {};
                g_filters.catalog = app.cat.generation;
                for (const auto& p : app.cat.packs) {
                    g_filters.areas.insert(p.area);
                    for (const auto& e : p.entries) {
                        if (!e.subject.empty()) {
                            g_filters.subjects.insert(e.subject);
                        }
                        if (!e.style.empty()) {
                            g_filters.styles.insert(e.style);
                        }
                        if (!e.placement.empty()) {
                            g_filters.placements.insert(e.placement);
                        }
                    }
                }
            }
            return g_filters;
        }

        // The tier the tattoo goes on: the one being picked for, or the selected one.
        int TargetSlot(App& app) {
            const int slot = app.picking ? app.pickSlot : app.ed.slot;
            return app.HasPreset() ? std::clamp(slot, 0, static_cast<int>(app.Preset().slots.size()) - 1) : 0;
        }

        std::vector<Item> Collect(App& app) {
            std::vector<Item> items;
            auto push = [&](const model::VisualPack& p, const model::VisualEntry& e) {
                if (!g.area.empty() && p.area != g.area) {
                    return;
                }
                if (!g.subject.empty() && e.subject != g.subject) {
                    return;
                }
                if (!g.style.empty() && e.style != g.style) {
                    return;
                }
                if (!g.placement.empty() && e.placement != g.placement) {
                    return;
                }
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

        void DrawSidebar(App& app, float width) {
            if (!BeginChild("##tsidebar", ImVec2{ width, 0 }, ImGuiChildFlags_Border)) {
                EndChild();
                return;
            }
            SearchBox("##tsearch", g.search, -1.0f, "Search tattoos");
            Spacing();
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
                TextWrapped("No visual packs found in MagicTattoosFramework/visuals.");
            }

            const auto& filters = Filters(app);
            const auto& areas = filters.areas;
            const auto& subjects = filters.subjects;
            const auto& styles = filters.styles;
            const auto& placements = filters.placements;
            SeparatorText("Filters");
            TextUnformatted("Area");
            FilterCombo("##area", g.area, areas);
            if (!subjects.empty()) {
                TextUnformatted("Subject");
                FilterCombo("##subject", g.subject, subjects);
            }
            if (!styles.empty()) {
                TextUnformatted("Style");
                FilterCombo("##style", g.style, styles);
            }
            if (!placements.empty()) {
                TextUnformatted("Placement");
                FilterCombo("##placement", g.placement, placements);
            }
            if (!g.area.empty() || !g.subject.empty() || !g.style.empty() || !g.placement.empty()) {
                if (SmallButton("Clear filters")) {
                    g.area.clear();
                    g.subject.clear();
                    g.style.clear();
                    g.placement.clear();
                }
            }
            Spacing();
            SetNextItemWidth(-1);
            SliderInt("##thumbsize", &app.settings.thumbSize, 64, 192, "Thumbnails %d px");
            EndChild();
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

        void DrawGrid(App& app, const std::vector<Item>& items, float width) {
            if (!BeginChild("##tgrid", ImVec2{ width, 0 }, ImGuiChildFlags_Border)) {
                EndChild();
                return;
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
            const float labelH = GetTextLineHeight();
            const int rows = (static_cast<int>(items.size()) + columns - 1) / columns;
            const float rowH = thumb + labelH + 10.0f;

            // Only rows on screen are drawn, so only their textures get requested.
            const float scrollY = GetScrollY();
            const float viewH = GetWindowHeight();
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
                    if (Selectable("##cell", g.selected == it.key, ImGuiSelectableFlags_AllowDoubleClick, ImVec2{ thumb, thumb + labelH + 4.0f })) {
                        g.selected = it.key;
                        if (IsMouseDoubleClicked(0)) {
                            UseSelected(app, it);
                        }
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        SetTooltip("%s\n%s", it.entry->label.c_str(), it.pack->label.c_str());
                    }
                    if (BeginPopupContextItem("##tctx")) {
                        g.selected = it.key;
                        if (MenuItem("New preset from this tattoo...")) {
                            g.newFrom = it.key;
                            g.newName = it.entry->label;
                        }
                        EndPopup();
                    }
                    render::DrawThumbAt(dl, pos, thumb, it.entry->textures.empty() ? std::string{} : it.entry->textures.front());
                    if (app.settings.IsFavorite(it.key)) {
                        ImDrawListManager::AddText(dl, ImVec2{ pos.x + thumb - 14, pos.y + 2 }, GetColorU32(kWarn), "*");
                    }
                    ImDrawListManager::PushClipRect(dl, ImVec2{ pos.x, pos.y + thumb }, ImVec2{ pos.x + thumb, pos.y + thumb + labelH + 4 }, true);
                    ImDrawListManager::AddText(dl, ImVec2{ pos.x + 2, pos.y + thumb + 2 }, GetColorU32(ImGuiCol_Text, 1.0f), it.entry->label.c_str());
                    ImDrawListManager::PopClipRect(dl);
                    PopID();
                }
            }
            SetCursorScreenPos(ImVec2{ origin.x, origin.y + rows * rowH });
            Dummy(ImVec2{ 1, 1 });
            EndChild();
        }

        void DrawDetails(App& app, const std::vector<Item>& items) {
            if (!BeginChild("##tdetail", ImVec2{ 0, 0 }, ImGuiChildFlags_Border)) {
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
        const auto& items = Items(app);
        const float sidebar = 210.0f;
        const float details = std::max(300.0f, GetContentRegionAvail().x * 0.3f);
        DrawSidebar(app, sidebar);
        SameLine();
        DrawGrid(app, items, GetContentRegionAvail().x - details - GetStyle()->ItemSpacing.x);
        SameLine();
        DrawDetails(app, items);
        DrawNewPresetPopup(app);
    }
}
