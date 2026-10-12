#pragma once

#include <imgui.h>

namespace vv {

// Panels; zones are main-viewport rectangles computed in main.cpp.
void draw_main_menu_bar(bool& request_quit, bool& export_ir);
void draw_left_settings_panel(ImVec2 pos, ImVec2 size);
void draw_bottom_log_panel(ImVec2 pos, ImVec2 size);
void draw_status_bar(float status_h);

}  // namespace vv
