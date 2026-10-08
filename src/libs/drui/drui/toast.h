#pragma once

#include "theme.h"
#include "icons.h"

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace DrUI {

enum class ToastType { Info, Success, Warning, Error };

struct ToastEntry {
    std::string message;
    ToastType   type;
    double      start_time;
    float       lifetime;
};

inline std::vector<ToastEntry>& GetToasts() {
    static std::vector<ToastEntry> toasts;
    return toasts;
}

inline void ShowToast(const char* msg, ToastType type = ToastType::Info,
                      float lifetime = 3.0f) {
    ToastEntry t;
    t.message    = msg;
    t.type       = type;
    t.start_time = ImGui::GetTime();
    t.lifetime   = lifetime;
    GetToasts().push_back(t);
}

struct ToastAnchor {
    ImVec2 pos;
    ImVec2 size;
};

inline void DrawToasts(const ToastAnchor& anchor) {
    auto& toasts = GetToasts();
    double now = ImGui::GetTime();
    float y_off = 0.0f;
    const float toast_w = 300.0f;
    const float margin  = 12.0f;

    for (int i = (int)toasts.size() - 1; i >= 0; --i) {
        auto& t = toasts[i];
        float elapsed = (float)(now - t.start_time);
        if (elapsed > t.lifetime) {
            toasts.erase(toasts.begin() + i);
            continue;
        }

        float alpha = 1.0f;
        if (elapsed < 0.3f)
            alpha = elapsed / 0.3f;
        else if (elapsed > t.lifetime - 0.5f)
            alpha = (t.lifetime - elapsed) / 0.5f;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        ImU32 bar_col;
        const char* icon;
        switch (t.type) {
        case ToastType::Success:
            bar_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(Colors::Success.x, Colors::Success.y, Colors::Success.z, alpha));
            icon = ICON_FA_CHECK_CIRCLE; break;
        case ToastType::Warning:
            bar_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(Colors::Warning.x, Colors::Warning.y, Colors::Warning.z, alpha));
            icon = ICON_FA_WARNING;      break;
        case ToastType::Error:
            bar_col = ImGui::ColorConvertFloat4ToU32(
                ImVec4(Colors::Error.x, Colors::Error.y, Colors::Error.z, alpha));
            icon = ICON_FA_CIRCLE_XMARK; break;
        default:
            bar_col = Colors::Accent((unsigned char)(255 * alpha));
            icon = ICON_FA_INFO_CIRCLE;  break;
        }

        ImVec2 tsz = ImGui::CalcTextSize(t.message.c_str(), nullptr, false,
                                          toast_w - 50.0f);
        float toast_h = std::max(tsz.y + 20.0f, 40.0f);

        float x = anchor.pos.x + anchor.size.x - toast_w - margin;
        float y = anchor.pos.y + anchor.size.y - margin - toast_h - y_off;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 mn(x, y), mx(x + toast_w, y + toast_h);

        ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(Colors::BackgroundTertiary.x, Colors::BackgroundTertiary.y,
                   Colors::BackgroundTertiary.z, 0.90f * alpha));
        ImU32 brd_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(Colors::Border.x, Colors::Border.y, Colors::Border.z, 0.5f * alpha));

        dl->AddRectFilled(mn, mx, bg_col, 8.0f);
        dl->AddRect(mn, mx, brd_col, 8.0f);
        dl->AddRectFilled(mn, ImVec2(mn.x + 3, mx.y), bar_col,
                          8.0f, ImDrawFlags_RoundCornersLeft);

        ImVec2 isz = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(mn.x + 12, mn.y + (toast_h - isz.y) * 0.5f),
                    bar_col, icon);

        ImU32 txt_col = ImGui::ColorConvertFloat4ToU32(
            ImVec4(Colors::TextPrimary.x, Colors::TextPrimary.y,
                   Colors::TextPrimary.z, alpha));
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(mn.x + 12 + isz.x + 8, mn.y + 10),
                    txt_col, t.message.c_str(), nullptr, toast_w - 50.0f);

        y_off += toast_h + 8.0f;
    }
}

} // namespace DrUI
