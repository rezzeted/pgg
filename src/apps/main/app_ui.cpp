#include "app_ui.hpp"

#include "app_state.hpp"
#include "layout_preview.hpp"
#include "level_synth_globals.hpp"

#include "drui/drui.h"
#include "drui/icons.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <random>

namespace ls {

void handle_keyboard_shortcuts(bool& request_export) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) {
        request_export = true;
    }
}

void draw_main_menu_bar(bool& request_quit, bool& menu_export_json, PanelVisibility& panels) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu(ICON_FA_FILE " File")) {
        if (ImGui::MenuItem(ICON_FA_SAVE " Export JSON...", "Ctrl+E", false, !g_layouts.empty())) {
            menu_export_json = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_TIMES " Exit", "Alt+F4")) {
            request_quit = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_BARS " View")) {
        ImGui::MenuItem(ICON_FA_COG " Settings panel", nullptr, &panels.show_left_panel);
        ImGui::MenuItem(ICON_FA_BARS " Log panel", nullptr, &panels.show_log_panel);
        ImGui::MenuItem(ICON_FA_BOLT " Status bar", nullptr, &panels.status_bar);
        ImGui::Separator();
        if (ImGui::BeginMenu(ICON_FA_INFO_CIRCLE " Theme")) {
            for (int i = 0; i < static_cast<int>(DrUI::ThemeId::COUNT); ++i) {
                const auto tid = static_cast<DrUI::ThemeId>(i);
                const bool selected = (DrUI::g_current_theme == tid);
                if (ImGui::MenuItem(DrUI::ThemeName(tid), nullptr, selected)) {
                    DrUI::ApplyTheme(tid, panels.dpi_scale);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(ICON_FA_INFO_CIRCLE " Help")) {
        if (ImGui::MenuItem(ICON_FA_INFO_CIRCLE " About")) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "LevelSynth  ·  ImGui %s", IMGUI_VERSION);
            DrUI::ShowToast(buf, DrUI::ToastType::Info, 4.0f);
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void draw_left_settings_panel(const PanelLayout& zone) {
    ImGui::SetNextWindowPos(zone.pos);
    ImGui::SetNextWindowSize(zone.size);
    ImGui::Begin("##LeftSettings", nullptr, kPanelFlags);

    ImGui::SeparatorText("Map");
    if (g_catalog_loaded) {
        const int n_maps = static_cast<int>(g_catalog.maps.size());
        g_selected_preset = std::clamp(g_selected_preset, 0, n_maps - 1);
        const auto& cur = g_catalog.maps[static_cast<std::size_t>(g_selected_preset)];

        if (ImGui::BeginCombo("##map_combo", cur.display_name.c_str())) {
            for (int i = 0; i < n_maps; ++i) {
                const bool selected = (i == g_selected_preset);
                if (ImGui::Selectable(g_catalog.maps[static_cast<std::size_t>(i)].display_name.c_str(),
                                      selected)) {
                    g_selected_preset = i;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("Maps: <repo>/resources/dungeon_topology_generator_gui/Maps/  (%d files)", n_maps);
        ImGui::TextDisabled("%s", g_resources_path);
        ImGui::Text("Rooms: %d-%d | Passages: %d", cur.room_from, cur.room_to,
                    static_cast<int>(cur.passages.size()));
        ImGui::Text("Corridors: %s", cur.corridors_enabled ? "Yes" : "No");
    } else {
        ImGui::TextUnformatted("No maps under resources/dungeon_topology_generator_gui/Maps/");
        ImGui::TextDisabled("%s", g_resources_path);
    }
    if (ImGui::Button("Reload catalog", ImVec2(-1.0f, 0.0f))) {
        try {
            reload_catalog_from_default_resources();
        } catch (const std::exception& e) {
            app_log_push_fmt("Error: %s", e.what());
        }
    }
    ImGui::Spacing();

    ImGui::SeparatorText("Generation");
    ImGui::Checkbox("Random seed", &g_use_random_seed);
    if (!g_use_random_seed) {
        ImGui::InputInt("Seed", &g_seed);
    }
    ImGui::InputInt("Layouts", &g_num_layouts);
    g_num_layouts = std::clamp(g_num_layouts, 1, 64);
    ImGui::Checkbox("Compute doors", &g_compute_doors);
    ImGui::Spacing();

    auto& sa = g_gen_config.simulated_annealing;
    ImGui::SeparatorText("SA Configuration");
    ImGui::InputInt("Cycles", &sa.cycles);
    sa.cycles = std::max(sa.cycles, 1);
    ImGui::InputInt("Trials/cycle", &sa.trials_per_cycle);
    sa.trials_per_cycle = std::max(sa.trials_per_cycle, 1);
    ImGui::InputInt("Max iters w/o success", &sa.max_iterations_without_success);
    sa.max_iterations_without_success = std::max(sa.max_iterations_without_success, 1);
    ImGui::InputInt("Max stage2 failures", &sa.max_stage_two_failures);
    sa.max_stage_two_failures = std::max(sa.max_stage_two_failures, 1);
    ImGui::InputInt("Max perturb radius", &sa.max_perturbation_radius);
    sa.max_perturbation_radius = std::max(sa.max_perturbation_radius, 1);
    ImGui::Checkbox("Handle trees greedily", &sa.handle_trees_greedily);
    ImGui::InputInt("Time budget/layout (ms, 0=off)", &g_time_budget_ms);
    g_time_budget_ms = std::max(g_time_budget_ms, 0);

    if (ImGui::Button("Generate", ImVec2(-1.0f, 0.0f))) {
        try {
            unsigned seed = 0;
            if (g_use_random_seed) {
                std::random_device rd;
                seed = rd();
            } else {
                seed = static_cast<unsigned>(g_seed);
            }

            if (g_catalog_loaded) {
                generate_from_preset(g_selected_preset, seed);
            } else {
                generate_hardcoded(seed);
            }
        } catch (const std::exception& e) {
            app_log_push_fmt("Error: %s", e.what());
            g_layouts.clear();
            g_layout_index = 0;
            g_last_rooms = -1;
        }
    }

    ImGui::Spacing();
    if (!g_layouts.empty()) {
        if (ImGui::Button("Export JSON", ImVec2(-1.0f, 0.0f))) {
            g_export_pending = true;
        }
    }

    ImGui::End();
}

void draw_center_preview_panel(const PanelLayout& zone) {
    ImGui::SetNextWindowPos(zone.pos);
    ImGui::SetNextWindowSize(zone.size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##CenterPreview", nullptr, kPanelFlags);
    ImGui::PopStyleVar();

    ImGui::SeparatorText("Layout preview");

    if (g_layouts.size() > 1) {
        const int max_i = static_cast<int>(g_layouts.size()) - 1;
        g_layout_index = std::clamp(g_layout_index, 0, max_i);
        ImGui::SliderInt("Shown layout", &g_layout_index, 0, max_i);
    }

    if (!g_layouts.empty() && g_layout_index >= 0
        && g_layout_index < static_cast<int>(g_layouts.size())
        && !g_layouts[static_cast<size_t>(g_layout_index)].rooms.empty()) {

        const auto& shown = g_layouts[static_cast<size_t>(g_layout_index)];

        int total_doors = 0;
        for (const auto& room : shown.rooms) {
            total_doors += static_cast<int>(room.doors.size());
        }
        ImGui::Text("Layout %d/%d  |  rooms=%d  doors=%d", g_layout_index + 1,
                    static_cast<int>(g_layouts.size()), static_cast<int>(shown.rooms.size()), total_doors);

        ImGui::Checkbox("Preview grid lines", &g_preview_grid_lines);
        ImGui::Checkbox("Preview wall shading", &g_preview_shading);
        ImGui::Spacing();

        ImGui::BeginChild("##preview_canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar);
        draw_layout_preview_imgui(shown, g_layout_index);
        ImGui::EndChild();
    } else {
        ImGui::TextDisabled("Generate a layout to see the preview.");
    }

    ImGui::End();
}

void draw_bottom_log_panel(const PanelLayout& zone, PanelVisibility& panels) {
    if (!panels.show_log_panel || zone.size.y < 1.0f) {
        return;
    }

    ImGui::SetNextWindowPos(zone.pos);
    ImGui::SetNextWindowSize(zone.size);
    if (panels.bottom_collapsed) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
    }
    ImGui::Begin("##BottomLog", nullptr, kPanelFlags);
    if (panels.bottom_collapsed) {
        ImGui::PopStyleVar();
    }

    static int active_tab = 0;
    int current_tab = active_tab;

    if (ImGui::BeginTabBar("##BottomTabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
        if (panels.show_log_panel
            && ImGui::BeginTabItem(ICON_FA_BARS " Log", &panels.show_log_panel)) {
            current_tab = 0;
            if (ImGui::IsItemClicked() && active_tab == 0) {
                panels.bottom_collapsed = !panels.bottom_collapsed;
            }
            if (!panels.bottom_collapsed) {
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
            }
            ImGui::EndTabItem();
        }
        if (current_tab != active_tab) {
            panels.bottom_collapsed = false;
        }
        active_tab = current_tab;
        ImGui::EndTabBar();
    }

    ImGui::End();
}

float compute_status_bar_height(float dpi_scale) {
    const float vpad = 6.0f * dpi_scale;
    const float line = ImGui::GetTextLineHeight();
    // Vertical padding (top+bottom) + text line + slack so FPS / long tail text never clip.
    return std::max(line + vpad * 2.0f + 6.0f, 34.0f * dpi_scale);
}

void draw_status_bar(float dpi_scale, float status_bar_height) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = status_bar_height;
    const float vpad = 6.0f * dpi_scale;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * std::min(dpi_scale, 1.25f), vpad));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, DrUI::Colors::BackgroundPrimary);
    ImGui::Begin("##StatusBar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 cpos = ImGui::GetCursorScreenPos();
    const float line_h = ImGui::GetTextLineHeight();
    float dot_r = 4.0f * dpi_scale;
    float dot_cx = cpos.x + dot_r;
    float dot_cy = cpos.y + line_h * 0.5f;
    dl->AddCircleFilled(ImVec2(dot_cx, dot_cy), dot_r,
                          ImGui::ColorConvertFloat4ToU32(DrUI::Colors::Success));
    ImGui::Dummy(ImVec2(dot_r * 2 + 6, line_h));
    ImGui::SameLine();

    ImGui::TextColored(DrUI::Colors::TextSecondary, ICON_FA_BOLT " LevelSynth");

    ImGui::SameLine(0, 8);
    ImGui::TextColored(DrUI::Colors::TextDisabled, "|");
    ImGui::SameLine(0, 8);

    if (!g_layouts.empty()) {
        ImGui::TextColored(DrUI::Colors::TextSecondary, "%d layout(s)", static_cast<int>(g_layouts.size()));
        ImGui::SameLine(0, 8);
        ImGui::TextColored(DrUI::Colors::TextDisabled, "|");
        ImGui::SameLine(0, 8);
    }

    const char* tail = g_app_log.empty() ? "Ready" : g_app_log.back().c_str();
    ImGui::TextColored(DrUI::Colors::TextSecondary, "%s", tail);

    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    ImVec4 fps_col = (io.Framerate >= 55.0f) ? DrUI::Colors::Success : DrUI::Colors::Warning;
    ImGui::TextColored(fps_col, "%.0f FPS", io.Framerate);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace ls
