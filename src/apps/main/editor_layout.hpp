#pragma once

#include "drui/drui.h"

#include <imgui.h>
#include <algorithm>

struct PanelLayout {
    ImVec2 pos{};
    ImVec2 size{};
};

struct EditorLayout {
    PanelLayout left{};
    PanelLayout center{};
    PanelLayout bottom{};
};

struct SplitterState {
    bool dragging = false;
};

struct EditorSplitters {
    float split_h = 0.22f;
    float split_v = 0.72f;
    SplitterState h_state{};
    SplitterState v_state{};
};

struct PanelVisibility {
    bool show_left_panel = true;
    bool show_log_panel = true;
    bool status_bar = true;
    bool bottom_collapsed = true;
    float dpi_scale = 1.0f;
};

static constexpr ImGuiWindowFlags kPanelFlags =
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoTitleBar;

// Layout: left | (center above bottom). When show_left_panel is false, center+stack use full width.
inline EditorLayout CalculateLayout(ImGuiViewport* vp, float gap, float menu_h, float status_h,
                                    const EditorSplitters& sp, const PanelVisibility& pv) {
    const float x = vp->WorkPos.x;
    const float y = vp->WorkPos.y + menu_h;
    const float w = vp->WorkSize.x;
    const float h = vp->WorkSize.y - menu_h - status_h;

    const float usable_h = h - gap * 3.0f;
    float center_h = 0.0f;
    float bottom_h = 0.0f;
    if (!pv.show_log_panel) {
        center_h = usable_h;
        bottom_h = 0.0f;
    } else if (pv.bottom_collapsed) {
        const auto& style = ImGui::GetStyle();
        const float tab_bar_h = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        bottom_h = tab_bar_h;
        center_h = usable_h - bottom_h;
    } else {
        center_h = usable_h * sp.split_v;
        bottom_h = usable_h - center_h;
    }

    EditorLayout l{};

    if (pv.show_left_panel) {
        const float usable_w = w - gap * 3.0f;
        const float left_w = usable_w * sp.split_h;
        const float right_col_w = usable_w - left_w;

        const float bottom_top = y + gap + center_h + gap;

        l.left.pos = ImVec2(x + gap, y + gap);
        l.left.size = ImVec2(left_w, h - gap * 2.0f);

        l.center.pos = ImVec2(x + gap + left_w + gap, y + gap);
        l.center.size = ImVec2(right_col_w, center_h);

        l.bottom.pos = ImVec2(x + gap + left_w + gap, bottom_top);
        l.bottom.size = ImVec2(right_col_w, bottom_h);
    } else {
        const float usable_w = w - gap * 2.0f;
        const float bottom_top = y + gap + center_h + gap;

        l.center.pos = ImVec2(x + gap, y + gap);
        l.center.size = ImVec2(usable_w, center_h);

        l.bottom.pos = ImVec2(x + gap, bottom_top);
        l.bottom.size = ImVec2(usable_w, bottom_h);
    }

    return l;
}

inline void HandleSplitters(const EditorLayout& layout, float gap, ImGuiViewport* vp, float menu_h,
                            float status_h, EditorSplitters& sp, const PanelVisibility& pv) {
    ImGuiIO& io = ImGui::GetIO();
    const float w = vp->WorkSize.x;
    const float h = vp->WorkSize.y - menu_h - status_h;
    const float usable_w = w - gap * 3.0f;
    const float usable_h = h - gap * 3.0f;
    const float origin_x = vp->WorkPos.x;
    const float origin_y = vp->WorkPos.y + menu_h;

    if (pv.show_left_panel) {
        float x0 = layout.left.pos.x + layout.left.size.x;
        float x1 = layout.center.pos.x;
        float y0 = layout.left.pos.y;
        float y1 = layout.left.pos.y + layout.left.size.y;
        float extend = 4.0f;
        bool hovered = io.MousePos.x >= x0 - extend && io.MousePos.x <= x1 + extend &&
                       io.MousePos.y >= y0 && io.MousePos.y <= y1;
        if (hovered || sp.h_state.dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (hovered && io.MouseClicked[0])
            sp.h_state.dragging = true;
        if (sp.h_state.dragging) {
            if (io.MouseDown[0]) {
                float new_left = io.MousePos.x - origin_x - gap - gap * 0.5f;
                sp.split_h = std::clamp(new_left / usable_w, 0.12f, 0.50f);
            } else {
                sp.h_state.dragging = false;
            }
        }
    }

    if (pv.show_log_panel && !pv.bottom_collapsed) {
        float y0 = layout.center.pos.y + layout.center.size.y;
        float y1 = layout.bottom.pos.y;
        float x0 = layout.center.pos.x;
        float x1 = layout.center.pos.x + layout.center.size.x;
        float extend = 4.0f;
        bool hovered = io.MousePos.y >= y0 - extend && io.MousePos.y <= y1 + extend &&
                       io.MousePos.x >= x0 && io.MousePos.x <= x1;
        if (hovered || sp.v_state.dragging)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (hovered && io.MouseClicked[0])
            sp.v_state.dragging = true;
        if (sp.v_state.dragging) {
            if (io.MouseDown[0]) {
                float new_center = io.MousePos.y - origin_y - gap - gap * 0.5f;
                sp.split_v = std::clamp(new_center / usable_h, 0.30f, 0.90f);
            } else {
                sp.v_state.dragging = false;
            }
        }
    }
}

inline void DrawSplitterIndicators(const EditorLayout& layout, const EditorSplitters& sp,
                                   const PanelVisibility& pv) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float extend = 4.0f;

    auto draw = [&](float x0, float y0, float x1, float y1, bool vert, bool dragging) {
        bool hov = vert ? (io.MousePos.x >= x0 - extend && io.MousePos.x <= x1 + extend &&
                           io.MousePos.y >= y0 && io.MousePos.y <= y1)
                        : (io.MousePos.y >= y0 - extend && io.MousePos.y <= y1 + extend &&
                           io.MousePos.x >= x0 && io.MousePos.x <= x1);
        if (!hov && !dragging)
            return;

        ImU32 line_col = dragging ? DrUI::Colors::Accent(200) : DrUI::Colors::Accent(100);
        ImU32 dot_col = dragging ? DrUI::Colors::Accent(240) : DrUI::Colors::Accent(150);

        if (vert) {
            float cx = (x0 + x1) * 0.5f;
            dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), line_col, 1.0f);
            float cy = (y0 + y1) * 0.5f;
            for (int i = -1; i <= 1; ++i)
                dl->AddCircleFilled(ImVec2(cx, cy + i * 8.0f), 2.5f, dot_col);
        } else {
            float cy = (y0 + y1) * 0.5f;
            dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), line_col, 1.0f);
            float cx = (x0 + x1) * 0.5f;
            for (int i = -1; i <= 1; ++i)
                dl->AddCircleFilled(ImVec2(cx + i * 8.0f, cy), 2.5f, dot_col);
        }
    };

    if (pv.show_left_panel) {
        draw(layout.left.pos.x + layout.left.size.x, layout.left.pos.y, layout.center.pos.x,
             layout.left.pos.y + layout.left.size.y, true, sp.h_state.dragging);
    }

    if (pv.show_log_panel && layout.bottom.size.y > 1.0f) {
        draw(layout.center.pos.x, layout.center.pos.y + layout.center.size.y, layout.center.pos.x + layout.center.size.x,
             layout.bottom.pos.y, false, sp.v_state.dragging);
    }
}
