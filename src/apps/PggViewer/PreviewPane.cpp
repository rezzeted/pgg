#include "pch.h"

#include "PreviewPane.h"

#include <algorithm>
#include <cmath>

#include <sokol_app.h>
#include <util/sokol_imgui.h>

void drawPreviewWindowContents(GeometryPreview& preview, PreviewPaneRect& lastImageRectPx) {
    if (ImGui::SmallButton("Fit")) preview.fit();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", preview.summary().empty() ? "(no geometry)" : preview.summary().c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 64.0f);
    avail.y = std::max(avail.y, 64.0f);
    const ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
    const int wantW = static_cast<int>(avail.x * std::max(1.0f, fbScale.x));
    const int wantH = static_cast<int>(avail.y * std::max(1.0f, fbScale.y));
    preview.ensureTarget(wantW, wantH);

    ImGui::InvisibleButton("##preview_canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const int px0 = static_cast<int>(std::lround(rmin.x * fbScale.x));
    const int py0 = static_cast<int>(std::lround(rmin.y * fbScale.y));
    lastImageRectPx.x = px0;
    lastImageRectPx.y = py0;
    lastImageRectPx.w = std::max(0, static_cast<int>(std::lround(rmax.x * fbScale.x)) - px0);
    lastImageRectPx.h = std::max(0, static_cast<int>(std::lround(rmax.y * fbScale.y)) - py0);
    if (preview.texView().id != SG_INVALID_ID) {
        const bool topLeft = sg_query_features().origin_top_left;
        const ImVec2 uv0 = topLeft ? ImVec2(0, 0) : ImVec2(0, 1);
        const ImVec2 uv1 = topLeft ? ImVec2(1, 1) : ImVec2(1, 0);
        ImGui::GetWindowDrawList()->AddImage(simgui_imtextureid(preview.texView()), rmin, rmax, uv0, uv1);
    }
    if (!preview.error().empty()) {
        const float wrapW = std::max(80.0f, rmax.x - rmin.x - 24.0f);
        const ImVec2 ts = ImGui::CalcTextSize(preview.error().c_str(), nullptr, false, wrapW);
        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(), ImGui::GetFontSize(),
            ImVec2(rmin.x + 12.0f, std::max(rmin.y + 12.0f, (rmin.y + rmax.y - ts.y) * 0.5f)),
            IM_COL32(255, 120, 100, 255), preview.error().c_str(), nullptr, wrapW);
    } else if (!preview.hasGeometry()) {
        const char* msg = "select a node and press Preview";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2((rmin.x + rmax.x - ts.x) * 0.5f, (rmin.y + rmax.y - ts.y) * 0.5f),
            IM_COL32(150, 150, 160, 255), msg);
    }

    const ImGuiIO& io = ImGui::GetIO();
    const bool active = ImGui::IsItemActive();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) preview.nudgeDistanceWheel(io.MouseWheel);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        preview.nudgeOrbit(io.MouseDelta.x * 0.01f, io.MouseDelta.y * 0.01f);
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
        preview.nudgePan(io.MouseDelta.x, io.MouseDelta.y);
}
