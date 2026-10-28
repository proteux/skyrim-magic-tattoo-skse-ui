#include "game/MtfBridge.h"
#include "ui/App.h"
#include "ui/Tabs.h"
#include "ui/Widgets.h"

namespace mtfui::ui {
    using namespace ImGuiMCP;

    namespace {
        std::string g_search;

        void EntryList(const char* id, const char* title, const std::vector<model::CatalogEntry>& list) {
            if (list.empty()) {
                return;
            }
            if (TreeNode(id, "%s (%d)", title, static_cast<int>(list.size()))) {
                for (const auto& e : list) {
                    BulletText("%s", e.label.c_str());
                    if (IsItemHovered(ImGuiHoveredFlags_DelayShort) && !e.description.empty()) {
                        SetTooltip("%s", e.description.c_str());
                    }
                }
                TreePop();
            }
        }
    }

    void DrawPluginsTab(App& app) {
        auto& bridge = game::MtfBridge::Get();
        const auto& snap = bridge.Snapshot();
        TextWrapped("%s", T("$MTFUI_Plugins_Intro"));
        SearchBox("##plugsearch", g_search, 280.0f, T("$MTFUI_Plugins_Search"));
        if (!bridge.Installed()) {
            SameLine();
            TextColored(kWarn, "%s", T("$MTFUI_Plugins_NoMtf"));
        }
        Spacing();

        if (!BeginTable("##plugins", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        TableSetupScrollFreeze(0, 1);
        TableSetupColumn(T("$MTFUI_Plugins_ColOn"), ImGuiTableColumnFlags_WidthFixed, 40.0f);
        TableSetupColumn(T("$MTFUI_Plugins_ColPlugin"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        TableSetupColumn(T("$MTFUI_Plugins_ColProvides"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        TableSetupColumn(T("$MTFUI_Common_UsedBy"), ImGuiTableColumnFlags_WidthStretch, 1.5f);
        TableHeadersRow();

        for (const auto& plugin : app.cat.plugins) {
            std::string hay = plugin.label + " " + plugin.pluginId;
            for (const auto& e : plugin.conditions) {
                hay += " " + e.label;
            }
            for (const auto& e : plugin.effects) {
                hay += " " + e.label;
            }
            if (!MatchesSearch(hay, g_search)) {
                continue;
            }
            const auto lower = io::ToLower(plugin.pluginId);
            const auto known = snap.pluginEnabled.find(lower);
            const bool registered = std::find(snap.registeredPlugins.begin(), snap.registeredPlugins.end(), lower) != snap.registeredPlugins.end();
            PushID(plugin.pluginId.c_str());
            TableNextRow();

            TableNextColumn();
            bool on = known == snap.pluginEnabled.end() || known->second;
            BeginDisabled(!bridge.Installed() || !snap.fresh);
            if (Checkbox("##on", &on)) {
                bridge.SetPluginEnabled(plugin.pluginId, on);
            }
            EndDisabled();

            TableNextColumn();
            TextUnformatted(plugin.label.c_str());
            TextDisabled("%s", plugin.pluginId.c_str());
            if (snap.fresh && !snap.registeredPlugins.empty() && !registered) {
                TextColored(kWarn, "%s", T("$MTFUI_Plugins_CatalogOnly"));
                if (IsItemHovered()) {
                    SetTooltip("%s", T("$MTFUI_Plugins_CatalogOnlyTip"));
                }
            }

            TableNextColumn();
            EntryList("##conditions", T("$MTFUI_Plugins_Conditions"), plugin.conditions);
            EntryList("##effects", T("$MTFUI_Plugins_Effects"), plugin.effects);

            TableNextColumn();
            if (const auto* users = app.usage.Plugin(plugin.pluginId)) {
                if (TreeNode("##users", "%s", TF("$MTFUI_Plugins_PresetCount", users->size()).c_str())) {
                    for (const auto& u : *users) {
                        BulletText("%s", u.c_str());
                    }
                    TreePop();
                }
                if (!on) {
                    TextColored(kWarn, "%s", T("$MTFUI_Plugins_DisabledInUse"));
                }
            } else {
                TextDisabled("%s", T("$MTFUI_Plugins_Unused"));
            }
            PopID();
        }
        EndTable();
        if (app.cat.plugins.empty()) {
            TextDisabled("%s", T("$MTFUI_Plugins_NoCatalogs"));
        }
    }
}
