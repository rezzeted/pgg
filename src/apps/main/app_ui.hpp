#pragma once

#include "editor_layout.hpp"

namespace ls {

void draw_main_menu_bar(bool& request_quit, bool& menu_export_json, PanelVisibility& panels);

void draw_left_settings_panel(const PanelLayout& zone);

void draw_center_preview_panel(const PanelLayout& zone);

void draw_bottom_log_panel(const PanelLayout& zone, PanelVisibility& panels);

void draw_status_bar(float dpi_scale, float status_bar_height);

// Height reserved for layout; call after ImGui::NewFrame() (uses font metrics).
float compute_status_bar_height(float dpi_scale);

void handle_keyboard_shortcuts(bool& request_export);

} // namespace ls
