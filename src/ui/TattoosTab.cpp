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

        Source g_source = Source::All;
        std::string g_pack;
        std::string g_search;
        std::string g_area;  // "" = any
        std::string g_subject;
        std::string g_style;
        std::string g_placement;
        std::string g_selected;  // key

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

        std::vector<Item> Collect(App& app) {
            std::vector<Item> items;
            auto push = [&](const model::VisualPack& p, const model::VisualEntry& e) {
                if (!g_area.empty() && p.area != g_area) {
                    return;
                }
                if (!g_subject.empty() && e.subject != g_subject) {
                    return;
                }
                if (!g_style.empty() && e.style != g_style) {
                    return;
                }
                if (!g_placement.empty() && e.placement != g_placement) {
                    return;
                }
                if (!MatchesSearch(e.label + " " + e.id + " " + p.label + " " + e.subject + " " + e.style + " " + e.placement + " " + e.description, g_search)) {
                    return;
                }
                items.push_back({ &p, &e, Key(p, e) });
            };
            if (g_source == Source::Recent || g_source == Source::Favorites) {
                const auto& keys = g_source == Source::Recent ? app.settings.recents : app.settings.favorites;
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
                if (g_source == Source::Pack && !model::IEquals(p.packId, g_pack)) {
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
            SearchBox("##tsearch", g_search, -1.0f, "Search tattoos");
            Spacing();
            std::size_t total = 0;
            for (const auto& p : app.cat.packs) {
                total += p.entries.size();
            }
            if (Selectable(("All (" + std::to_string(total) + ")").c_str(), g_source == Source::All)) {
                g_source = Source::All;
            }
            if (Selectable(("Favorites (" + std::to_string(app.settings.favorites.size()) + ")").c_str(), g_source == Source::Favorites)) {
                g_source = Source::Favorites;
            }
            if (Selectable("Recently used", g_source == Source::Recent)) {
                g_source = Source::Recent;
            }
            SeparatorText("Packs");
            for (const auto& p : app.cat.packs) {
                const bool sel = g_source == Source::Pack && model::IEquals(p.packId, g_pack);
                if (Selectable((p.label + " (" + std::to_string(p.entries.size()) + ")##" + p.packId).c_str(), sel)) {
                    g_source = Source::Pack;
                    g_pack = p.packId;
                }
                if (IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    SetTooltip("%s\nArea: %s\n%s", p.packId.c_str(), p.area.c_str(), p.file.filename().string().c_str());
                }
            }
            if (app.cat.packs.empty()) {
                TextWrapped("No visual packs found in MagicTattoosFramework/visuals.");
            }

            std::set<std::string> areas, subjects, styles, placements;
            for (const auto& p : app.cat.packs) {
                areas.insert(p.area);
                for (const auto& e : p.entries) {
                    if (!e.subject.empty()) {
                        subjects.insert(e.subject);
                    }
                    if (!e.style.empty()) {
                        styles.insert(e.style);
                    }
                    if (!e.placement.empty()) {
                        placements.insert(e.placement);
                    }
                }
            }
            SeparatorText("Filters");
            TextUnformatted("Area");
            FilterCombo("##area", g_area, areas);
            if (!subjects.empty()) {
                TextUnformatted("Subject");
                FilterCombo("##subject", g_subject, subjects);
            }
            if (!styles.empty()) {
                TextUnformatted("Style");
                FilterCombo("##style", g_style, styles);
            }
            if (!placements.empty()) {
                TextUnformatted("Placement");
                FilterCombo("##placement", g_placement, placements);
            }
            if (!g_area.empty() || !g_subject.empty() || !g_style.empty() || !g_placement.empty()) {
                if (SmallButton("Clear filters")) {
                    g_area.clear();
                    g_subject.clear();
                    g_style.clear();
                    g_placement.clear();
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
            const int slot = app.picking ? app.pickSlot : app.ed.slot;
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
                TextColored(kAccent, "Choosing a tattoo for %s: double-click one, or", model::SlotLabel(app.Preset(), app.pickSlot).c_str());
                SameLine();
                if (SmallButton("cancel")) {
                    app.picking = false;
                    app.RequestTab(Tab::Presets);
                }
            }
            if (items.empty()) {
                TextDisabled(g_source == Source::Favorites ? "No favorites yet. Select a tattoo and click the star." : "Nothing matches.");
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
                    PushID(idx);
                    if (Selectable("##cell", g_selected == it.key, ImGuiSelectableFlags_AllowDoubleClick, ImVec2{ thumb, thumb + labelH + 4.0f })) {
                        g_selected = it.key;
                        if (IsMouseDoubleClicked(0)) {
                            UseSelected(app, it);
                        }
                    }
                    if (IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        SetTooltip("%s\n%s", it.entry->label.c_str(), it.pack->label.c_str());
                    }
                    ImDrawListManager::AddRectFilled(dl, pos, ImVec2{ pos.x + thumb, pos.y + thumb }, render::ToImColor(0xC89F86, 1.0f), 4.0f, 0);
                    if (!it.entry->textures.empty()) {
                        const int texSize = 256;
                        if (const auto* tex = render::RequestCropped(it.entry->textures.front(), texSize, thumb, 1024)) {
                            ImVec2 uv0, uv1;
                            render::ContentUV(*tex, uv0, uv1);
                            ImDrawListManager::AddImage(dl, reinterpret_cast<ImTextureID>(tex->srv), pos, ImVec2{ pos.x + thumb, pos.y + thumb }, uv0, uv1,
                                0xFFFFFFFF);
                        } else if (render::DdsCache::Get().Failed(it.entry->textures.front(), texSize)) {
                            ImDrawListManager::AddText(dl, ImVec2{ pos.x + 6, pos.y + 6 }, GetColorU32(kError), "missing");
                        }
                    }
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
            const auto found = std::find_if(items.begin(), items.end(), [](const Item& i) { return i.key == g_selected; });
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
            if (app.HasPreset()) {
                const int slot = app.picking ? app.pickSlot : app.ed.slot;
                in.layers = app.Preset().slots[slot].layers;
                in.pulse = app.Preset().slots[slot].pulse;
                in.waveform = app.cat.FindWaveform(in.pulse.waveform);
            }
            render::DrawPreview("##tpreview", in, std::min(GetContentRegionAvail().x, 360.0f));
            if (app.HasPreset()) {
                TextDisabled("Shown with the colors of %s.", model::SlotLabel(app.Preset(), app.picking ? app.pickSlot : app.ed.slot).c_str());
            }

            if (app.HasPreset()) {
                const int slot = app.picking ? app.pickSlot : app.ed.slot;
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
    }

    void DrawTattoosTab(App& app) {
        const auto items = Collect(app);
        const float sidebar = 210.0f;
        const float details = std::max(300.0f, GetContentRegionAvail().x * 0.3f);
        DrawSidebar(app, sidebar);
        SameLine();
        DrawGrid(app, items, GetContentRegionAvail().x - details - GetStyle()->ItemSpacing.x);
        SameLine();
        DrawDetails(app, items);
    }
}
