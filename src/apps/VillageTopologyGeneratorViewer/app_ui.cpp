#include "app_ui.hpp"

#include "viewer_state.hpp"

#include "drui/drui.h"
#include "drui/icons.h"

#include <algorithm>

namespace vv {

namespace {
constexpr ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;
}

void draw_main_menu_bar(bool& request_quit, bool& export_ir) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu(ICON_FA_FILE " File")) {
        if (ImGui::MenuItem(ICON_FA_SAVE " Export IR JSON", nullptr, false, g_generated)) {
            export_ir = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TIMES " Exit", "Alt+F4")) {
            request_quit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ICON_FA_BARS " View")) {
        ImGui::MenuItem("Terrain", nullptr, &g_show_terrain);
        ImGui::MenuItem("Attractiveness", nullptr, &g_show_attractiveness);
        ImGui::MenuItem("Slope", nullptr, &g_show_slope);
        ImGui::Separator();
        ImGui::MenuItem("Roads", nullptr, &g_show_roads);
        ImGui::MenuItem("Parcels", nullptr, &g_show_parcels);
        ImGui::MenuItem("Buildings", nullptr, &g_show_buildings);
        ImGui::MenuItem("Anchors", nullptr, &g_show_anchors);
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_INFO_CIRCLE " Theme")) {
            for (int i = 0; i < static_cast<int>(DrUI::ThemeId::COUNT); ++i) {
                const auto tid = static_cast<DrUI::ThemeId>(i);
                if (ImGui::MenuItem(DrUI::ThemeName(tid), nullptr, DrUI::g_current_theme == tid)) {
                    DrUI::ApplyTheme(tid, 1.0f);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ICON_FA_INFO_CIRCLE " Help")) {
        if (ImGui::MenuItem(ICON_FA_INFO_CIRCLE " About")) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "VillageTopologyGeneratorViewer - ImGui %s", IMGUI_VERSION);
            DrUI::ShowToast(buf, DrUI::ToastType::Info, 4.0f);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void draw_left_settings_panel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##LeftSettings", nullptr, kPanelFlags);

    ImGui::SeparatorText("Terrain");
    {
        int seed = static_cast<int>(g_terrain_params.seed);
        if (ImGui::InputInt("Seed", &seed)) {
            g_terrain_params.seed = static_cast<uint64_t>(std::max(0, seed));
        }
    }
    ImGui::InputInt("Width", &g_terrain_params.width);
    ImGui::InputInt("Height", &g_terrain_params.height);
    g_terrain_params.width = std::clamp(g_terrain_params.width, 32, 512);
    g_terrain_params.height = std::clamp(g_terrain_params.height, 32, 512);
    {
        float v = static_cast<float>(g_terrain_params.water_level);
        if (ImGui::InputFloat("Water level", &v)) g_terrain_params.water_level = v;
        v = static_cast<float>(g_terrain_params.amplitude);
        if (ImGui::InputFloat("Amplitude", &v)) g_terrain_params.amplitude = std::max(1.0f, v);
        v = static_cast<float>(g_terrain_params.forest_threshold);
        if (ImGui::InputFloat("Forest threshold", &v)) {
            g_terrain_params.forest_threshold = std::clamp(v, 0.0f, 1.0f);
        }
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Skeleton");
    ImGui::Combo("Style", &g_style, "radial\0trunk\0");
    g_skeleton_params.style =
        g_style == 1 ? village_topology_generator::GrowthStyle::trunk
                     : village_topology_generator::GrowthStyle::radial;
    ImGui::Checkbox("Merge roads (Y-junctions)", &g_skeleton_params.merge_roads);
    ImGui::Spacing();

    ImGui::SeparatorText("Parcels");
    {
        float v = static_cast<float>(g_parcel_params.front_width);
        if (ImGui::InputFloat("Front width (m)", &v)) g_parcel_params.front_width = std::max(2.0f, v);
        v = static_cast<float>(g_parcel_params.max_depth);
        if (ImGui::InputFloat("Max depth (m)", &v)) g_parcel_params.max_depth = std::max(2.0f, v);
        v = static_cast<float>(g_parcel_params.min_depth);
        if (ImGui::InputFloat("Min depth (m)", &v)) g_parcel_params.min_depth = std::max(1.0f, v);
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Roster");
    ImGui::InputInt("Houses", &g_house_count);
    g_house_count = std::clamp(g_house_count, 0, 40);
    ImGui::TextDisabled("+ mill, bakery, tavern, well");
    ImGui::Spacing();

    if (ImGui::Button("Generate", ImVec2(-1.0f, 0.0f))) {
        try {
            run_generation();
        } catch (const std::exception& e) {
            app_log_push_fmt("Error: %s", e.what());
        }
    }

    if (g_generated) {
        ImGui::Spacing();
        ImGui::SeparatorText("Result");
        ImGui::Text("roads=%zu  parcels=%zu  buildings=%zu", g_network.roads.size(),
                    g_parcels.size(), g_ir["buildings"].size());
        if (!g_placement.unplaced.empty()) {
            ImGui::TextColored(DrUI::Colors::Warning, "unplaced: %zu", g_placement.unplaced.size());
        }
        ImGui::Text("fingerprint: %s", g_fingerprint.c_str());
        ImGui::Text("%.1f ms", g_last_ms);
    }

    ImGui::End();
}

void draw_bottom_log_panel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##BottomLog", nullptr, kPanelFlags);

    if (DrUI::Button(ICON_FA_TRASH " Clear")) {
        app_log_clear();
    }
    ImGui::BeginChild("##log_scroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (const auto& line : g_app_log) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
}

void draw_status_bar(float status_h) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - status_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, status_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, DrUI::Colors::BackgroundPrimary);
    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextColored(DrUI::Colors::TextSecondary, ICON_FA_BOLT " VillageTopologyGenerator");
    ImGui::SameLine(0, 8);
    ImGui::TextColored(DrUI::Colors::TextDisabled, "|");
    ImGui::SameLine(0, 8);
    const char* tail = g_app_log.empty() ? "Ready" : g_app_log.back().c_str();
    ImGui::TextColored(DrUI::Colors::TextSecondary, "%s", tail);
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    const ImVec4 fps_col =
        (ImGui::GetIO().Framerate >= 55.0f) ? DrUI::Colors::Success : DrUI::Colors::Warning;
    ImGui::TextColored(fps_col, "%.0f FPS", ImGui::GetIO().Framerate);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

}  // namespace vv
