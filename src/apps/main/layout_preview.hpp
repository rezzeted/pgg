#pragma once

#include "edgar/edgar.hpp"
#include "imgui.h"

extern bool g_preview_grid_lines;
extern bool g_preview_shading;

/// @param layout_view_id Changes to this value reset pan/zoom (e.g. selected layout index).
void draw_layout_preview_imgui(const edgar::generator::grid2d::LayoutGrid2D<int>& layout,
                               int layout_view_id);
