#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace DrUI {

enum class ThemeId { Dark, Light, Olive, Nord, Cherry, COUNT };

inline ThemeId g_current_theme = ThemeId::Dark;

namespace Colors {

inline ImVec4 BackgroundPrimary;
inline ImVec4 BackgroundSecondary;
inline ImVec4 BackgroundTertiary;

inline ImVec4 AccentPrimary;
inline ImVec4 AccentSecondary;
inline ImVec4 AccentHover;

inline ImVec4 Success;
inline ImVec4 Warning;
inline ImVec4 Error;
inline ImVec4 Info;

inline ImVec4 TextPrimary;
inline ImVec4 TextSecondary;
inline ImVec4 TextDisabled;

inline ImVec4 FrameBackground;

inline ImVec4 Border;
inline ImVec4 FrameBorder;
inline ImVec4 Selection;

inline ImVec4 CardBg;
inline ImVec4 CardBorder;
inline ImVec4 CanvasBg;
inline ImU32  CanvasDot;
inline ImU32  CanvasDotMajor;
inline ImU32  CanvasLabel;

// Accent as ImU32, optionally with custom alpha.
inline ImU32 Accent(unsigned char a = 255) {
    ImVec4 c = AccentPrimary;
    c.w = a / 255.0f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

inline ImU32 AccentBright(float f = 1.20f, unsigned char a = 255) {
    ImVec4 c(std::min(AccentPrimary.x * f, 1.0f),
             std::min(AccentPrimary.y * f, 1.0f),
             std::min(AccentPrimary.z * f, 1.0f),
             a / 255.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

inline ImU32 AccentDim(float f = 0.80f, unsigned char a = 255) {
    return AccentBright(f, a);
}

} // namespace Colors

inline const char* ThemeName(ThemeId id) {
    switch (id) {
        case ThemeId::Dark:   return "\xd0\xa2\xd1\x91\xd0\xbc\xd0\xbd\xd0\xb0\xd1\x8f";
        case ThemeId::Light:  return "\xd0\xa1\xd0\xb2\xd0\xb5\xd1\x82\xd0\xbb\xd0\xb0\xd1\x8f";
        case ThemeId::Olive:  return "\xd0\x9e\xd0\xbb\xd0\xb8\xd0\xb2\xd0\xba\xd0\xbe\xd0\xb2\xd0\xb0\xd1\x8f";
        case ThemeId::Nord:   return "Nord";
        case ThemeId::Cherry: return "\xd0\x92\xd0\xb8\xd1\x88\xd0\xbd\xd1\x91\xd0\xb2\xd0\xb0\xd1\x8f";
        default: return "?";
    }
}

inline void SetThemeColors(ThemeId id) {
    using namespace Colors;
    g_current_theme = id;

    switch (id) {
    case ThemeId::Dark:
        BackgroundPrimary   = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
        BackgroundSecondary = ImVec4(0.11f, 0.11f, 0.14f, 1.0f);
        BackgroundTertiary  = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
        AccentPrimary       = ImVec4(0.37f, 0.55f, 0.95f, 1.0f);
        AccentSecondary     = ImVec4(0.30f, 0.35f, 0.55f, 1.0f);
        AccentHover         = ImVec4(0.45f, 0.63f, 0.98f, 1.0f);
        Success             = ImVec4(0.24f, 0.58f, 0.44f, 1.0f);
        Warning             = ImVec4(0.75f, 0.56f, 0.22f, 1.0f);
        Error               = ImVec4(0.78f, 0.28f, 0.30f, 1.0f);
        Info                = ImVec4(0.40f, 0.70f, 0.95f, 1.0f);
        TextPrimary         = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        TextSecondary       = ImVec4(0.70f, 0.70f, 0.75f, 1.0f);
        TextDisabled        = ImVec4(0.45f, 0.45f, 0.50f, 1.0f);
        FrameBackground     = ImVec4(0.16f, 0.16f, 0.20f, 1.0f);
        Border              = ImVec4(0.28f, 0.28f, 0.33f, 1.0f);
        FrameBorder         = ImVec4(0.25f, 0.25f, 0.30f, 1.0f);
        Selection           = ImVec4(0.37f, 0.55f, 0.95f, 0.25f);
        CardBg              = ImVec4(0.10f, 0.10f, 0.14f, 1.0f);
        CardBorder          = ImVec4(0.22f, 0.22f, 0.28f, 0.7f);
        CanvasBg            = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
        CanvasDot           = IM_COL32(255, 255, 255, 16);
        CanvasDotMajor      = IM_COL32(255, 255, 255, 35);
        CanvasLabel         = IM_COL32(255, 255, 255, 30);
        break;

    case ThemeId::Light:
        BackgroundPrimary   = ImVec4(0.88f, 0.89f, 0.90f, 1.0f);
        BackgroundSecondary = ImVec4(0.96f, 0.96f, 0.97f, 1.0f);
        BackgroundTertiary  = ImVec4(0.92f, 0.92f, 0.93f, 1.0f);
        AccentPrimary       = ImVec4(0.45f, 0.62f, 0.88f, 1.0f);
        AccentSecondary     = ImVec4(0.35f, 0.50f, 0.75f, 1.0f);
        AccentHover         = ImVec4(0.55f, 0.70f, 0.92f, 1.0f);
        Success             = ImVec4(0.35f, 0.68f, 0.48f, 1.0f);
        Warning             = ImVec4(0.82f, 0.68f, 0.30f, 1.0f);
        Error               = ImVec4(0.82f, 0.40f, 0.40f, 1.0f);
        Info                = ImVec4(0.40f, 0.62f, 0.85f, 1.0f);
        TextPrimary         = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
        TextSecondary       = ImVec4(0.38f, 0.38f, 0.42f, 1.0f);
        TextDisabled        = ImVec4(0.56f, 0.56f, 0.60f, 1.0f);
        FrameBackground     = ImVec4(0.86f, 0.87f, 0.88f, 1.0f);
        Border              = ImVec4(0.72f, 0.73f, 0.75f, 1.0f);
        FrameBorder         = ImVec4(0.78f, 0.78f, 0.80f, 1.0f);
        Selection           = ImVec4(0.20f, 0.45f, 0.85f, 0.20f);
        CardBg              = ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        CardBorder          = ImVec4(0.78f, 0.79f, 0.82f, 0.7f);
        CanvasBg            = ImVec4(0.94f, 0.94f, 0.95f, 1.0f);
        CanvasDot           = IM_COL32(0, 0, 0, 18);
        CanvasDotMajor      = IM_COL32(0, 0, 0, 40);
        CanvasLabel         = IM_COL32(0, 0, 0, 25);
        break;

    case ThemeId::Olive:
        BackgroundPrimary   = ImVec4(0.72f, 0.74f, 0.66f, 1.0f);
        BackgroundSecondary = ImVec4(0.78f, 0.80f, 0.72f, 1.0f);
        BackgroundTertiary  = ImVec4(0.74f, 0.76f, 0.68f, 1.0f);
        AccentPrimary       = ImVec4(0.45f, 0.58f, 0.18f, 1.0f);
        AccentSecondary     = ImVec4(0.35f, 0.46f, 0.12f, 1.0f);
        AccentHover         = ImVec4(0.52f, 0.66f, 0.24f, 1.0f);
        Success             = ImVec4(0.28f, 0.58f, 0.32f, 1.0f);
        Warning             = ImVec4(0.76f, 0.58f, 0.16f, 1.0f);
        Error               = ImVec4(0.72f, 0.28f, 0.22f, 1.0f);
        Info                = ImVec4(0.38f, 0.56f, 0.34f, 1.0f);
        TextPrimary         = ImVec4(0.15f, 0.17f, 0.12f, 1.0f);
        TextSecondary       = ImVec4(0.30f, 0.33f, 0.26f, 1.0f);
        TextDisabled        = ImVec4(0.48f, 0.50f, 0.44f, 1.0f);
        FrameBackground     = ImVec4(0.70f, 0.72f, 0.64f, 1.0f);
        Border              = ImVec4(0.58f, 0.60f, 0.50f, 1.0f);
        FrameBorder         = ImVec4(0.62f, 0.64f, 0.54f, 1.0f);
        Selection           = ImVec4(0.45f, 0.58f, 0.18f, 0.22f);
        CardBg              = ImVec4(0.82f, 0.84f, 0.76f, 1.0f);
        CardBorder          = ImVec4(0.62f, 0.65f, 0.54f, 0.7f);
        CanvasBg            = ImVec4(0.76f, 0.78f, 0.70f, 1.0f);
        CanvasDot           = IM_COL32(60, 70, 40, 20);
        CanvasDotMajor      = IM_COL32(60, 70, 40, 45);
        CanvasLabel         = IM_COL32(60, 70, 40, 35);
        break;

    case ThemeId::Nord:
        BackgroundPrimary   = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);
        BackgroundSecondary = ImVec4(0.23f, 0.26f, 0.32f, 1.0f);
        BackgroundTertiary  = ImVec4(0.26f, 0.30f, 0.37f, 1.0f);
        AccentPrimary       = ImVec4(0.53f, 0.75f, 0.82f, 1.0f);
        AccentSecondary     = ImVec4(0.36f, 0.51f, 0.58f, 1.0f);
        AccentHover         = ImVec4(0.60f, 0.82f, 0.88f, 1.0f);
        Success             = ImVec4(0.64f, 0.74f, 0.55f, 1.0f);
        Warning             = ImVec4(0.92f, 0.80f, 0.55f, 1.0f);
        Error               = ImVec4(0.75f, 0.38f, 0.42f, 1.0f);
        Info                = ImVec4(0.51f, 0.63f, 0.76f, 1.0f);
        TextPrimary         = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
        TextSecondary       = ImVec4(0.70f, 0.74f, 0.80f, 1.0f);
        TextDisabled        = ImVec4(0.44f, 0.50f, 0.56f, 1.0f);
        FrameBackground     = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
        Border              = ImVec4(0.30f, 0.34f, 0.42f, 1.0f);
        FrameBorder         = ImVec4(0.28f, 0.32f, 0.38f, 1.0f);
        Selection           = ImVec4(0.53f, 0.75f, 0.82f, 0.22f);
        CardBg              = ImVec4(0.21f, 0.24f, 0.30f, 1.0f);
        CardBorder          = ImVec4(0.30f, 0.34f, 0.42f, 0.7f);
        CanvasBg            = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
        CanvasDot           = IM_COL32(220, 230, 240, 16);
        CanvasDotMajor      = IM_COL32(220, 230, 240, 36);
        CanvasLabel         = IM_COL32(220, 230, 240, 28);
        break;

    case ThemeId::Cherry:
        BackgroundPrimary   = ImVec4(0.09f, 0.06f, 0.07f, 1.0f);
        BackgroundSecondary = ImVec4(0.14f, 0.10f, 0.11f, 1.0f);
        BackgroundTertiary  = ImVec4(0.18f, 0.13f, 0.15f, 1.0f);
        AccentPrimary       = ImVec4(0.90f, 0.35f, 0.50f, 1.0f);
        AccentSecondary     = ImVec4(0.62f, 0.24f, 0.35f, 1.0f);
        AccentHover         = ImVec4(0.95f, 0.45f, 0.58f, 1.0f);
        Success             = ImVec4(0.30f, 0.60f, 0.45f, 1.0f);
        Warning             = ImVec4(0.82f, 0.62f, 0.28f, 1.0f);
        Error               = ImVec4(0.85f, 0.25f, 0.25f, 1.0f);
        Info                = ImVec4(0.70f, 0.40f, 0.55f, 1.0f);
        TextPrimary         = ImVec4(0.95f, 0.90f, 0.92f, 1.0f);
        TextSecondary       = ImVec4(0.72f, 0.65f, 0.68f, 1.0f);
        TextDisabled        = ImVec4(0.45f, 0.40f, 0.42f, 1.0f);
        FrameBackground     = ImVec4(0.16f, 0.11f, 0.13f, 1.0f);
        Border              = ImVec4(0.32f, 0.22f, 0.26f, 1.0f);
        FrameBorder         = ImVec4(0.28f, 0.20f, 0.23f, 1.0f);
        Selection           = ImVec4(0.90f, 0.35f, 0.50f, 0.25f);
        CardBg              = ImVec4(0.12f, 0.08f, 0.10f, 1.0f);
        CardBorder          = ImVec4(0.30f, 0.18f, 0.22f, 0.7f);
        CanvasBg            = ImVec4(0.07f, 0.05f, 0.06f, 1.0f);
        CanvasDot           = IM_COL32(255, 200, 210, 16);
        CanvasDotMajor      = IM_COL32(255, 200, 210, 36);
        CanvasLabel         = IM_COL32(255, 200, 210, 28);
        break;

    default: break;
    }
}

inline void ApplyStyle(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    using namespace Colors;

    style.Colors[ImGuiCol_WindowBg]             = BackgroundSecondary;
    style.Colors[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_PopupBg]              = BackgroundTertiary;
    style.Colors[ImGuiCol_Border]               = FrameBorder;
    style.Colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0.3f);

    style.Colors[ImGuiCol_Text]                 = TextPrimary;
    style.Colors[ImGuiCol_TextDisabled]         = TextDisabled;

    style.Colors[ImGuiCol_Header]               = Selection;
    ImVec4 hdrHov(AccentPrimary.x, AccentPrimary.y, AccentPrimary.z, 0.40f);
    style.Colors[ImGuiCol_HeaderHovered]        = hdrHov;
    style.Colors[ImGuiCol_HeaderActive]         = AccentPrimary;

    style.Colors[ImGuiCol_Button]               = AccentPrimary;
    style.Colors[ImGuiCol_ButtonHovered]        = AccentHover;
    style.Colors[ImGuiCol_ButtonActive]         = AccentSecondary;

    style.Colors[ImGuiCol_FrameBg]              = FrameBackground;
    style.Colors[ImGuiCol_FrameBgHovered]       = BackgroundTertiary;
    style.Colors[ImGuiCol_FrameBgActive]        = BackgroundTertiary;

    style.Colors[ImGuiCol_Tab]                      = BackgroundSecondary;
    ImVec4 tabHov(AccentPrimary.x, AccentPrimary.y, AccentPrimary.z, 0.15f);
    style.Colors[ImGuiCol_TabHovered]               = tabHov;
    ImVec4 tabSel(BackgroundTertiary.x + 0.04f,
                  BackgroundTertiary.y + 0.04f,
                  BackgroundTertiary.z + 0.05f, 1.0f);
    style.Colors[ImGuiCol_TabSelected]              = tabSel;
    style.Colors[ImGuiCol_TabSelectedOverline]      = AccentPrimary;
    style.Colors[ImGuiCol_TabDimmed]                = BackgroundSecondary;
    style.Colors[ImGuiCol_TabDimmedSelected]        = BackgroundTertiary;
    ImVec4 tabDimOvr(AccentPrimary.x, AccentPrimary.y, AccentPrimary.z, 0.45f);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline]= tabDimOvr;

    style.Colors[ImGuiCol_TitleBg]              = BackgroundPrimary;
    style.Colors[ImGuiCol_TitleBgActive]        = BackgroundPrimary;
    style.Colors[ImGuiCol_TitleBgCollapsed]     = BackgroundPrimary;
    style.Colors[ImGuiCol_MenuBarBg]            = BackgroundPrimary;

    style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.4f, 0.4f, 0.4f, 0.2f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = AccentPrimary;
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = AccentHover;

    style.Colors[ImGuiCol_CheckMark]            = AccentPrimary;
    style.Colors[ImGuiCol_SliderGrab]           = AccentPrimary;
    style.Colors[ImGuiCol_SliderGrabActive]     = AccentHover;

    style.Colors[ImGuiCol_Separator]            = Border;
    style.Colors[ImGuiCol_SeparatorHovered]     = AccentPrimary;
    style.Colors[ImGuiCol_SeparatorActive]      = AccentHover;

    style.Colors[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ResizeGripHovered]    = AccentPrimary;
    style.Colors[ImGuiCol_ResizeGripActive]     = AccentHover;

    style.Colors[ImGuiCol_PlotHistogram]        = AccentPrimary;
    style.Colors[ImGuiCol_PlotHistogramHovered] = AccentHover;

    style.Colors[ImGuiCol_TableHeaderBg]        = BackgroundPrimary;
    style.Colors[ImGuiCol_TableBorderStrong]    = Border;
    style.Colors[ImGuiCol_TableBorderLight]     = BackgroundTertiary;
    style.Colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.04f);

    style.Colors[ImGuiCol_DragDropTarget]       = AccentSecondary;
    style.Colors[ImGuiCol_NavCursor]            = AccentPrimary;

    style.WindowPadding     = ImVec2(12.0f * scale, 12.0f * scale);
    style.FramePadding      = ImVec2(10.0f * scale, 7.0f * scale);
    style.ItemSpacing       = ImVec2(8.0f * scale, 8.0f * scale);
    style.ItemInnerSpacing  = ImVec2(6.0f * scale, 6.0f * scale);
    style.CellPadding       = ImVec2(8.0f * scale, 6.0f * scale);
    style.IndentSpacing     = 18.0f * scale;
    style.ScrollbarSize     = 10.0f * scale;
    style.GrabMinSize       = 12.0f * scale;

    style.WindowRounding    = 8.0f * scale;
    style.ChildRounding     = 8.0f * scale;
    style.FrameRounding     = 2.0f * scale;
    style.PopupRounding     = 8.0f * scale;
    style.ScrollbarRounding = 4.0f * scale;
    style.GrabRounding      = 2.0f * scale;
    style.TabRounding       = 0.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;
    style.TabBarBorderSize  = 0.0f;
    style.TabBarOverlineSize = 3.0f * scale;

    style.WindowMinSize           = ImVec2(200.0f * scale, 100.0f * scale);
    style.DisplaySafeAreaPadding  = ImVec2(4.0f * scale, 4.0f * scale);
}

inline void ApplyTheme(ThemeId id, float scale) {
    SetThemeColors(id);
    ApplyStyle(scale);
}

} // namespace DrUI
