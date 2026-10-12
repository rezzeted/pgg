#include "map_preview.hpp"

#include "viewer_state.hpp"

#include "drui/drui.h"

#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vv {
namespace {

using namespace village_topology_generator;

GLuint s_tex = 0;
uint64_t s_tex_version = 0;

ImU32 lerp_u32(ImU32 ca, ImU32 cb, float t) {
    const int a0 = ca & 0xff, g0 = (ca >> 8) & 0xff, b0 = (ca >> 16) & 0xff;
    const int a1 = cb & 0xff, g1 = (cb >> 8) & 0xff, b1 = (cb >> 16) & 0xff;
    return IM_COL32(static_cast<int>(a0 + (a1 - a0) * t), static_cast<int>(g0 + (g1 - g0) * t),
                    static_cast<int>(b0 + (b1 - b0) * t), 255);
}

void rebuild_texture() {
    const Terrain& t = g_terrain;
    const int w = t.width;
    const int h = t.height;
    if (w <= 0 || h <= 0) return;

    double max_h = t.water_level + 1.0;
    for (const double v : t.heights) max_h = std::max(max_h, v);

    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            ImU32 col;
            if (t.is_water(x, y)) {
                const float depth =
                    static_cast<float>(std::clamp((t.water_level - t.heights[i]) / 6.0, 0.0, 1.0));
                col = lerp_u32(IM_COL32(64, 120, 200, 255), IM_COL32(18, 40, 105, 255), depth);
            } else {
                const float t01 =
                    static_cast<float>(std::clamp((t.heights[i] - t.water_level) / (max_h - t.water_level),
                                                  0.0, 1.0));
                col = t01 < 0.5f
                          ? lerp_u32(IM_COL32(168, 150, 96, 255), IM_COL32(96, 146, 72, 255), t01 * 2.0f)
                          : lerp_u32(IM_COL32(96, 146, 72, 255), IM_COL32(178, 178, 170, 255),
                                     (t01 - 0.5f) * 2.0f);
                if (t.is_forest(x, y)) {
                    col = lerp_u32(col, IM_COL32(28, 80, 40, 255), 0.65f);
                }
            }
            if (g_show_attractiveness && !g_analysis.attractiveness.empty()) {
                const float a = static_cast<float>(std::clamp(g_analysis.attractiveness[i], 0.0, 1.0));
                col = lerp_u32(col, IM_COL32(255, 176, 32, 255), a * 0.55f);
            }
            if (g_show_slope && !g_analysis.slope.empty()) {
                const float s = static_cast<float>(std::clamp(g_analysis.slope[i] / 0.5, 0.0, 1.0));
                col = lerp_u32(col, IM_COL32(220, 40, 32, 255), s * 0.6f);
            }
            px[i * 4 + 0] = static_cast<uint8_t>(col & 0xff);
            px[i * 4 + 1] = static_cast<uint8_t>((col >> 8) & 0xff);
            px[i * 4 + 2] = static_cast<uint8_t>((col >> 16) & 0xff);
            px[i * 4 + 3] = 255;
        }
    }

    if (s_tex == 0) {
        glGenTextures(1, &s_tex);
    }
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    s_tex_version = g_generation_version;
}

ImU32 building_color(const std::string& type) {
    if (type == "mill") return IM_COL32(232, 200, 60, 255);
    if (type == "bakery") return IM_COL32(230, 140, 50, 255);
    if (type == "tavern") return IM_COL32(170, 90, 200, 255);
    if (type == "well") return IM_COL32(90, 210, 220, 255);
    if (type == "church") return IM_COL32(235, 235, 235, 255);
    if (type == "smithy") return IM_COL32(150, 150, 160, 255);
    if (type == "farm") return IM_COL32(120, 190, 90, 255);
    return IM_COL32(240, 240, 235, 255);  // house & unknown
}

}  // namespace

void draw_map_preview(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::Begin("##CenterMap", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    if (!g_generated) {
        ImGui::TextDisabled("Set parameters on the left and click Generate.");
        ImGui::End();
        return;
    }

    if (s_tex_version != g_generation_version) {
        rebuild_texture();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float canvas_w = std::max(100.0f, avail.x);
    const float canvas_h = std::max(200.0f, avail.y);
    const ImVec2 canvas_sz(canvas_w, canvas_h);
    ImGui::InvisibleButton("##map_canvas", canvas_sz);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 canvas_p0 = ImGui::GetItemRectMin();
    const ImVec2 canvas_p1(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);

    static float s_zoom = 1.0f;
    static ImVec2 s_pan_px(0.0f, 0.0f);
    static bool s_middle_pan = false;
    static uint64_t s_view_version = 0;
    if (s_view_version != g_generation_version) {
        s_view_version = g_generation_version;
        s_zoom = 1.0f;
        s_pan_px = ImVec2(0.0f, 0.0f);
    }

    const double cs = g_terrain.cell_size;
    const float world_w = static_cast<float>(g_terrain.width * cs);
    const float world_h = static_cast<float>(g_terrain.height * cs);
    constexpr float pad = 16.0f;
    const float scale_fit = std::min((canvas_sz.x - 2.0f * pad) / world_w,
                                     (canvas_sz.y - 2.0f * pad) / world_h);
    float scale = scale_fit * s_zoom;

    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) s_middle_pan = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) s_middle_pan = false;
    if (s_middle_pan) {
        s_pan_px.x += io.MouseDelta.x;
        s_pan_px.y += io.MouseDelta.y;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        const float wx_old = (mx - canvas_p0.x - pad - s_pan_px.x) / scale;
        const float wy_old = (my - canvas_p0.y - pad - s_pan_px.y) / scale;
        const float zoom_factor = std::exp(io.MouseWheel * 0.12f);
        s_zoom = std::clamp(s_zoom * zoom_factor, 0.25f, 8.0f);
        const float scale_new = scale_fit * s_zoom;
        s_pan_px.x = mx - canvas_p0.x - pad - wx_old * scale_new;
        s_pan_px.y = my - canvas_p0.y - pad - wy_old * scale_new;
        scale = scale_new;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        s_zoom = 1.0f;
        s_pan_px = ImVec2(0.0f, 0.0f);
        scale = scale_fit;
    }

    auto to_screen = [&](double mx, double my) {
        return ImVec2(canvas_p0.x + pad + static_cast<float>(mx) * scale + s_pan_px.x,
                      canvas_p0.y + pad + static_cast<float>(my) * scale + s_pan_px.y);
    };
    auto cell_center = [&](std::pair<int, int> c) {
        return to_screen((c.first + 0.5) * cs, (c.second + 0.5) * cs);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_p0, canvas_p1, IM_COL32(58, 60, 66, 255));
    dl->PushClipRect(canvas_p0, canvas_p1, true);

    // Terrain texture.
    if (g_show_terrain && s_tex != 0) {
        const ImVec2 p0 = to_screen(0.0, 0.0);
        const ImVec2 p1 = to_screen(world_w, world_h);
        dl->AddImage(ImTextureRef(static_cast<ImTextureID>(s_tex)), p0, p1);
    } else {
        const ImVec2 p0 = to_screen(0.0, 0.0);
        const ImVec2 p1 = to_screen(world_w, world_h);
        dl->AddRectFilled(p0, p1, IM_COL32(80, 82, 88, 255));
        dl->AddRect(p0, p1, IM_COL32(120, 122, 128, 255));
    }

    // Parcels: translucent fill + front edge highlight.
    if (g_show_parcels) {
        const ImU32 fill = IM_COL32(206, 178, 120, 90);
        const ImU32 edge = IM_COL32(122, 88, 48, 220);
        for (const auto& p : g_parcels) {
            const size_t f = p.front.size();
            if (f == 0) continue;
            const size_t depth = p.cells.size() / f;
            const auto c00 = p.cells[0];
            const auto c0f = p.cells[f - 1];
            const auto cd0 = p.cells[(depth - 1) * f];
            const auto cdf = p.cells.back();
            const ImVec2 q[4] = {to_screen(c00.first * cs, c00.second * cs),
                                 to_screen((c0f.first + 1) * cs, c0f.second * cs),
                                 to_screen((cdf.first + 1) * cs, (cdf.second + 1) * cs),
                                 to_screen(cd0.first * cs, (cd0.second + 1) * cs)};
            dl->AddConvexPolyFilled(q, 4, fill);
            dl->AddLine(q[0], q[1], edge, std::max(2.0f, 0.25f * static_cast<float>(cs) * scale));
        }
    }

    // Roads: polylines through cell centers.
    if (g_show_roads) {
        const float th = std::max(2.0f, 0.55f * static_cast<float>(cs) * scale);
        for (const auto& r : g_network.roads) {
            for (size_t i = 1; i < r.cells.size(); ++i) {
                dl->AddLine(cell_center(r.cells[i - 1]), cell_center(r.cells[i]),
                            IM_COL32(44, 40, 36, 255), th);
            }
        }
        for (const auto& r : g_network.roads) {
            for (size_t i = 1; i < r.cells.size(); ++i) {
                dl->AddLine(cell_center(r.cells[i - 1]), cell_center(r.cells[i]),
                            IM_COL32(150, 138, 120, 255), std::max(1.0f, th * 0.3f));
            }
        }
    }

    // Buildings: marker + facing arrow (+ label when zoomed in).
    if (g_show_buildings) {
        const float cell_px = static_cast<float>(cs) * scale;
        for (const auto& b : g_placement.buildings) {
            if (b.parcel_index < 0) continue;
            const ImVec2 c = cell_center(b.cell);
            const float r = std::max(3.0f, 0.38f * cell_px);
            dl->AddCircleFilled(c, r, building_color(b.type));
            dl->AddCircle(c, r, IM_COL32(30, 28, 26, 255), 0, 1.5f);
            dl->AddLine(c,
                        ImVec2(c.x + static_cast<float>(b.facing.first) * r * 2.0f,
                               c.y + static_cast<float>(b.facing.second) * r * 2.0f),
                        IM_COL32(30, 28, 26, 255), 1.5f);
            if (cell_px > 10.0f) {
                dl->AddText(ImVec2(c.x + r + 2.0f, c.y - 6.0f), IM_COL32(245, 245, 240, 255),
                            b.type.c_str());
            }
        }
    }

    // Anchors: diamonds (center red, water_site cyan, edge white).
    if (g_show_anchors) {
        for (const auto& a : g_anchors) {
            const ImVec2 c = cell_center(a.cell);
            const float r = 7.0f;
            ImU32 col = IM_COL32(240, 240, 240, 255);
            if (a.kind == AnchorKind::center) col = IM_COL32(228, 70, 60, 255);
            if (a.kind == AnchorKind::water_site) col = IM_COL32(70, 200, 230, 255);
            const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                                   ImVec2(c.x - r, c.y)};
            dl->AddConvexPolyFilled(pts, 4, col);
            dl->AddPolyline(pts, 4, IM_COL32(30, 28, 26, 255), ImDrawFlags_Closed, 1.5f);
        }
    }

    dl->PopClipRect();
    dl->AddRect(canvas_p0, canvas_p1, ImGui::ColorConvertFloat4ToU32(DrUI::Colors::Border));
    ImGui::End();
}

}  // namespace vv
