#include "layout_preview.hpp"

#include "drui/drui.h"

#include "edgar/geometry/orthogonal_line_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/io/layout_grid_cells.hpp"
#include "edgar/io/layout_outline_with_door_gaps.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

void dash_line_screen(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float dash_px,
                      float gap_px) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        return;
    }
    const float ux = dx / len;
    const float uy = dy / len;
    float t = 0.f;
    while (t < len - 1e-4f) {
        const float dash = std::min(dash_px, len - t);
        const ImVec2 s(a.x + ux * t, a.y + uy * t);
        const ImVec2 e(a.x + ux * (t + dash), a.y + uy * (t + dash));
        dl->AddLine(s, e, col, thickness);
        t += dash + gap_px;
    }
}

} // namespace

bool g_preview_grid_lines = true;
bool g_preview_shading = false;

void draw_layout_preview_imgui(const edgar::generator::grid2d::LayoutGrid2D<int>& layout,
                               int layout_view_id) {
    using namespace edgar::generator::grid2d;
    using namespace edgar::geometry;
    if (layout.rooms.empty()) {
        return;
    }
    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();
    for (const auto& room : layout.rooms) {
        for (const auto& p : room.outline.points()) {
            const int wx = p.x + room.position.x;
            const int wy = p.y + room.position.y;
            min_x = std::min(min_x, wx);
            min_y = std::min(min_y, wy);
            max_x = std::max(max_x, wx);
            max_y = std::max(max_y, wy);
        }
    }
    const float dx = static_cast<float>(std::max(1, max_x - min_x));
    const float dy = static_cast<float>(std::max(1, max_y - min_y));

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float canvas_w = std::max(100.0f, avail.x);
    const float canvas_h = std::max(200.0f, avail.y);
    const ImVec2 canvas_sz(canvas_w, canvas_h);

    ImGui::InvisibleButton("##layout_canvas", canvas_sz);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_p0 = ImGui::GetItemRectMin();
    const ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    // Preview view: pan (screen px) and zoom multiplier on top of fit-to-window scale.
    static int s_last_layout_view_id = std::numeric_limits<int>::min();
    static float s_zoom = 1.0f;
    static ImVec2 s_pan_px(0.0f, 0.0f);
    static bool s_middle_pan = false;

    if (layout_view_id != s_last_layout_view_id) {
        s_zoom = 1.0f;
        s_pan_px = ImVec2(0.0f, 0.0f);
        s_middle_pan = false;
        s_last_layout_view_id = layout_view_id;
    }

    ImGuiIO& io = ImGui::GetIO();
    constexpr float pad = 16.0f;
    const float scale_fit =
        std::min((canvas_sz.x - 2.0f * pad) / dx, (canvas_sz.y - 2.0f * pad) / dy);
    float scale = scale_fit * s_zoom;

    // Middle mouse: pan (screen-space). Drag only while middle is held; started over canvas.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        s_middle_pan = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        s_middle_pan = false;
    }
    if (s_middle_pan) {
        s_pan_px.x += io.MouseDelta.x;
        s_pan_px.y += io.MouseDelta.y;
    }

    // Mouse wheel: zoom toward cursor (keep world point under cursor fixed on screen).
    constexpr float k_zoom_wheel = 0.12f;
    constexpr float k_zoom_min = 0.25f;
    constexpr float k_zoom_max = 4.0f;
    if (hovered && io.MouseWheel != 0.0f) {
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        const float wx_old = static_cast<float>(min_x) + (mx - canvas_p0.x - pad - s_pan_px.x) / scale;
        const float wy_old = static_cast<float>(min_y) + (my - canvas_p0.y - pad - s_pan_px.y) / scale;
        const float zoom_factor = std::exp(io.MouseWheel * k_zoom_wheel);
        s_zoom = std::clamp(s_zoom * zoom_factor, k_zoom_min, k_zoom_max);
        const float scale_new = scale_fit * s_zoom;
        s_pan_px.x = mx - canvas_p0.x - pad - (wx_old - static_cast<float>(min_x)) * scale_new;
        s_pan_px.y = my - canvas_p0.y - pad - (wy_old - static_cast<float>(min_y)) * scale_new;
        scale = scale_new;
    }

    // Double-click: reset view to fit (same as default pan=0, zoom=1).
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        s_zoom = 1.0f;
        s_pan_px = ImVec2(0.0f, 0.0f);
        scale = scale_fit * s_zoom;
    }

    auto to_screen = [&](int wx, int wy) {
        return ImVec2(canvas_p0.x + pad + (static_cast<float>(wx - min_x)) * scale + s_pan_px.x,
                      canvas_p0.y + pad + (static_cast<float>(wy - min_y)) * scale + s_pan_px.y);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Mid gray canvas (lighter than before; black wall lines still read on empty area).
    constexpr ImU32 k_canvas_bg = IM_COL32(72, 74, 80, 255);
    dl->AddRectFilled(canvas_p0, canvas_p1, k_canvas_bg);
    dl->PushClipRect(canvas_p0, canvas_p1, true);

    // C# DungeonDrawer-style fills; wall outline + interior grid + shading: black on room fill.
    constexpr ImU32 k_fill_room = IM_COL32(248, 248, 244, 255);
    constexpr ImU32 k_fill_corridor = IM_COL32(230, 240, 248, 255);
    constexpr ImU32 k_wall = IM_COL32(0, 0, 0, 255);
    constexpr ImU32 k_grid = IM_COL32(0, 0, 0, 150);
    constexpr ImU32 k_shade = IM_COL32(0, 0, 0, 255);
    constexpr float k_wall_thickness = 5.0f;
    constexpr float k_shade_thickness = 4.0f;
    constexpr float k_grid_line_thickness = 1.0f;
    constexpr float k_grid_dash_px = 4.0f;
    constexpr float k_grid_gap_px = 3.0f;

    for (const auto& room : layout.rooms) {
        std::vector<Vector2Int> world_poly;
        world_poly.reserve(room.outline.points().size());
        for (const auto& p : room.outline.points()) {
            world_poly.push_back(p + room.position);
        }

        int rmin_x = std::numeric_limits<int>::max();
        int rmin_y = std::numeric_limits<int>::max();
        int rmax_x = std::numeric_limits<int>::min();
        int rmax_y = std::numeric_limits<int>::min();
        for (const auto& p : world_poly) {
            rmin_x = std::min(rmin_x, p.x);
            rmin_y = std::min(rmin_y, p.y);
            rmax_x = std::max(rmax_x, p.x);
            rmax_y = std::max(rmax_y, p.y);
        }

        std::vector<OrthogonalLineGrid2D> door_lines;
        door_lines.reserve(room.doors.size());
        for (const auto& d : room.doors) {
            door_lines.push_back(d.door_line);
        }
        auto outline_local = edgar::io::layout_outline_with_door_gaps(room.outline, std::move(door_lines));

        if (g_preview_shading && !outline_local.empty()) {
            ImVec2 last = to_screen(outline_local.back().first.x + room.position.x,
                                    outline_local.back().first.y + room.position.y);
            for (const auto& pr : outline_local) {
                const ImVec2 pt = to_screen(pr.first.x + room.position.x, pr.first.y + room.position.y);
                if (pr.second) {
                    dl->AddLine(last, pt, k_shade, k_shade_thickness);
                }
                last = pt;
            }
        }

        const ImU32 fill_col = room.is_corridor ? k_fill_corridor : k_fill_room;
        for (int y = rmin_y; y <= rmax_y; ++y) {
            for (int x = rmin_x; x <= rmax_x; ++x) {
                if (edgar::io::point_in_polygon_xy({x, y}, world_poly)) {
                    const ImVec2 p0 = to_screen(x, y);
                    const ImVec2 p1 = to_screen(x + 1, y);
                    const ImVec2 p2 = to_screen(x + 1, y + 1);
                    const ImVec2 p3 = to_screen(x, y + 1);
                    const ImVec2 q[4] = {p0, p1, p2, p3};
                    dl->AddConvexPolyFilled(q, 4, fill_col);
                }
            }
        }

        if (g_preview_grid_lines) {
            const PolygonGrid2D poly_world = room.outline + room.position;
            const std::vector<Vector2Int> lattice = edgar::io::grid_cell_lattice_points(poly_world);
            const auto lattice_set = edgar::io::grid_cell_lattice_point_set(lattice);
            for (const auto& p : lattice) {
                const Vector2Int right{p.x + 1, p.y};
                const Vector2Int bottom{p.x, p.y - 1};
                if (edgar::io::lattice_set_contains(lattice_set, right)) {
                    dash_line_screen(dl, to_screen(p.x, p.y), to_screen(right.x, right.y), k_grid,
                                     k_grid_line_thickness, k_grid_dash_px, k_grid_gap_px);
                }
                if (edgar::io::lattice_set_contains(lattice_set, bottom)) {
                    dash_line_screen(dl, to_screen(p.x, p.y), to_screen(bottom.x, bottom.y), k_grid,
                                     k_grid_line_thickness, k_grid_dash_px, k_grid_gap_px);
                }
            }
        }

        door_lines.clear();
        door_lines.reserve(room.doors.size());
        for (const auto& d : room.doors) {
            door_lines.push_back(d.door_line);
        }
        outline_local = edgar::io::layout_outline_with_door_gaps(room.outline, std::move(door_lines));
        if (outline_local.empty()) {
            continue;
        }
        ImVec2 last = to_screen(outline_local.back().first.x + room.position.x,
                                outline_local.back().first.y + room.position.y);
        for (const auto& pr : outline_local) {
            const ImVec2 pt = to_screen(pr.first.x + room.position.x, pr.first.y + room.position.y);
            if (pr.second) {
                dl->AddLine(last, pt, k_wall, k_wall_thickness);
            }
            last = pt;
        }
    }

    dl->PopClipRect();
    dl->AddRect(canvas_p0, canvas_p1, ImGui::ColorConvertFloat4ToU32(DrUI::Colors::Border));
}
