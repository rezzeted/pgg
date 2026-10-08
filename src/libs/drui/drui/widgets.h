#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include "theme.h"
#include "icons.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace DrUI {

// ── Internal helpers ────────────────────────────────────────────────

namespace detail {

static constexpr float kButtonFontScale = 1.1f;

inline void PushButtonFont() {
    ImGui::PushFont(nullptr,
                    ImGui::GetStyle().FontSizeBase * kButtonFontScale);
}

inline ImU32 ButtonBg(bool held, bool hovered) {
    ImVec4 b = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    if (held)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(b.x * 0.75f, b.y * 0.75f, b.z * 0.75f, b.w));
    if (hovered)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(std::min(b.x * 1.12f, 1.0f),
                   std::min(b.y * 1.12f, 1.0f),
                   std::min(b.z * 1.12f, 1.0f), b.w));
    return ImGui::GetColorU32(ImGuiCol_Button);
}

// Optical Y correction so the visual center of capital letters
// aligns with the geometric center of the button.
inline float TextCenterCorrection() {
    ImFontBaked* fb = ImGui::GetFontBaked();
    const ImFontGlyph* g = fb->FindGlyph((ImWchar)'A');
    if (!g) return 0.0f;
    return fb->Size * 0.5f - (g->Y0 + g->Y1) * 0.5f;
}

inline int DetectIconPrefix(const char* s) {
    if (!s) return 0;
    auto u = [](char c) { return (unsigned char)c; };
    if (u(s[0]) == 0xEF && (u(s[1]) & 0xC0) == 0x80 && (u(s[2]) & 0xC0) == 0x80)
        return 3;
    return 0;
}

inline ImWchar DecodeIconCodepoint(const char* s) {
    auto u = [](char c) -> unsigned { return (unsigned char)c; };
    return (ImWchar)(((u(s[0]) & 0x0F) << 12) | ((u(s[1]) & 0x3F) << 6) | (u(s[2]) & 0x3F));
}

inline ImVec2 CenterIconInRect(const char* icon, float cx, float cy, float w, float h) {
    ImFontBaked* fb = ImGui::GetFontBaked();
    ImWchar cp = DecodeIconCodepoint(icon);
    const ImFontGlyph* g = fb->FindGlyph(cp);
    if (!g) {
        ImVec2 sz = ImGui::CalcTextSize(icon);
        return ImVec2(cx + (w - sz.x) * 0.5f, cy + (h - sz.y) * 0.5f);
    }
    float vis_w = g->X1 - g->X0;
    float vis_h = g->Y1 - g->Y0;
    return ImVec2(cx + (w - vis_w) * 0.5f - g->X0,
                  cy + (h - vis_h) * 0.5f - g->Y0);
}

} // namespace detail

// ── Widgets ─────────────────────────────────────────────────────────

// General-purpose button. Auto-detects a leading FA icon in the label
// and renders icon / text at independently centered Y positions.
inline bool Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0)) {
    detail::PushButtonFont();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 label_sz = ImGui::CalcTextSize(label, nullptr, true);

    ImVec2 sz;
    sz.x = (size_arg.x > 0) ? size_arg.x : (label_sz.x + style.FramePadding.x * 2.0f);
    sz.y = (size_arg.y > 0) ? size_arg.y : (label_sz.y + style.FramePadding.y * 2.0f);

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool pressed = ImGui::InvisibleButton("##ui", sz);
    ImGui::PopID();

    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cursor, ImVec2(cursor.x + sz.x, cursor.y + sz.y),
                      detail::ButtonBg(held, hovered), style.FrameRounding);

    ImU32  col      = ImGui::GetColorU32(ImGuiCol_Text);
    float  base_y   = cursor.y + (sz.y - label_sz.y) * 0.5f;
    float  lx       = cursor.x + (sz.x - label_sz.x) * 0.5f;
    ImFont* cur_font = ImGui::GetFont();
    float   cur_size = ImGui::GetFontSize();

    int icon_len = detail::DetectIconPrefix(label);
    if (icon_len > 0) {
        float text_corr = detail::TextCenterCorrection();

        ImWchar icon_cp = detail::DecodeIconCodepoint(label);
        const ImFontGlyph* ig = ImGui::GetFontBaked()->FindGlyph(icon_cp);
        float icon_y = base_y;
        if (ig)
            icon_y = cursor.y + sz.y * 0.5f - (ig->Y0 + ig->Y1) * 0.5f;
        dl->AddText(cur_font, cur_size, ImVec2(lx, icon_y), col, label, label + icon_len);

        const char* rest = label + icon_len;
        while (*rest == ' ') ++rest;
        if (*rest) {
            float icon_w  = ImGui::CalcTextSize(label, label + icon_len).x;
            float space_w = ImGui::CalcTextSize(" ").x;
            dl->AddText(cur_font, cur_size,
                        ImVec2(lx + icon_w + space_w, base_y + text_corr), col, rest);
        }
    } else {
        float text_corr = detail::TextCenterCorrection();
        dl->AddText(cur_font, cur_size, ImVec2(lx, base_y + text_corr), col, label);
    }

    ImGui::PopFont();
    return pressed;
}

// Icon + text button with explicit separate arguments.
inline bool Button(const char* icon, const char* text) {
    detail::PushButtonFont();
    ImVec2 icon_sz = ImGui::CalcTextSize(icon);
    ImVec2 text_sz = ImGui::CalcTextSize(text);
    const ImGuiStyle& style = ImGui::GetStyle();
    float gap = 6.0f;

    float content_w = icon_sz.x + gap + text_sz.x;
    float content_h = std::max(icon_sz.y, text_sz.y);
    ImVec2 btn_sz(content_w + style.FramePadding.x * 2.0f,
                  content_h + style.FramePadding.y * 2.0f);

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImGui::PushID(text);
    bool pressed = ImGui::InvisibleButton("##btn", btn_sz);
    ImGui::PopID();

    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cursor, ImVec2(cursor.x + btn_sz.x, cursor.y + btn_sz.y),
                      detail::ButtonBg(held, hovered), style.FrameRounding);

    ImFont* cur_font = ImGui::GetFont();
    float   cur_size = ImGui::GetFontSize();
    ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    float text_y = cursor.y + (btn_sz.y - text_sz.y) * 0.5f + detail::TextCenterCorrection();

    ImWchar icon_cp = detail::DecodeIconCodepoint(icon);
    const ImFontGlyph* ig = ImGui::GetFontBaked()->FindGlyph(icon_cp);
    float icon_y = cursor.y + (btn_sz.y - icon_sz.y) * 0.5f;
    if (ig)
        icon_y = cursor.y + btn_sz.y * 0.5f - (ig->Y0 + ig->Y1) * 0.5f;

    dl->AddText(cur_font, cur_size,
                ImVec2(cursor.x + style.FramePadding.x, icon_y), text_col, icon);
    dl->AddText(cur_font, cur_size,
                ImVec2(cursor.x + style.FramePadding.x + icon_sz.x + gap, text_y),
                text_col, text);

    ImGui::PopFont();
    return pressed;
}

// Square icon-only button.
inline bool IconButton(const char* icon, float size = 0.0f) {
    detail::PushButtonFont();
    if (size <= 0.0f) size = ImGui::GetFrameHeight();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImGui::PushID(icon);
    bool pressed = ImGui::InvisibleButton("##ico", ImVec2(size, size));
    ImGui::PopID();

    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cursor, ImVec2(cursor.x + size, cursor.y + size),
                      detail::ButtonBg(held, hovered), ImGui::GetStyle().FrameRounding);

    ImVec2 ipos = detail::CenterIconInRect(icon, cursor.x, cursor.y, size, size);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ipos, ImGui::GetColorU32(ImGuiCol_Text), icon);

    ImGui::PopFont();
    return pressed;
}

// Square icon-only toggle button (on = accent bg + bright icon, off = gray).
inline bool IconToggle(const char* icon, bool* v, float size = 0.0f) {
    detail::PushButtonFont();
    if (size <= 0.0f) size = ImGui::GetFrameHeight();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImGui::PushID(icon);
    ImGui::PushID(v);
    bool pressed = ImGui::InvisibleButton("##ico_tog", ImVec2(size, size));
    if (pressed) *v = !*v;
    ImGui::PopID();
    ImGui::PopID();

    bool hovered = ImGui::IsItemHovered();
    bool held    = ImGui::IsItemActive();
    bool on      = *v;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rnd = ImGui::GetStyle().FrameRounding;

    ImU32 bg;
    if (on) {
        bg = detail::ButtonBg(held, hovered);
    } else {
        ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        if (held)
            bg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(frame.x * 0.85f, frame.y * 0.85f, frame.z * 0.85f, frame.w));
        else if (hovered)
            bg = ImGui::ColorConvertFloat4ToU32(
                ImVec4(std::min(frame.x * 1.25f, 1.0f),
                       std::min(frame.y * 1.25f, 1.0f),
                       std::min(frame.z * 1.25f, 1.0f), frame.w));
        else
            bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    }

    dl->AddRectFilled(cursor, ImVec2(cursor.x + size, cursor.y + size), bg, rnd);

    ImU32 icon_col = on ? ImGui::GetColorU32(ImGuiCol_Text)
                        : ImGui::GetColorU32(ImGuiCol_TextDisabled);

    ImVec2 ipos = detail::CenterIconInRect(icon, cursor.x, cursor.y, size, size);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ipos, icon_col, icon);

    ImGui::PopFont();
    return pressed;
}

// InputInt with icon +/- stepper buttons.
inline bool InputInt(const char* label, int* v, int step = 1, int step_fast = 10,
                     float field_width = 0.0f) {
    bool changed = false;
    float btn_sz = ImGui::GetFrameHeight();

    ImGui::PushID(label);
    if (IconButton(ICON_FA_MINUS, btn_sz)) { *v -= step; changed = true; }
    ImGui::SameLine(0, 2);
    if (field_width > 0) ImGui::SetNextItemWidth(field_width);
    else                 ImGui::SetNextItemWidth(btn_sz * 3.0f);
    if (ImGui::InputInt("##v", v, 0, 0)) changed = true;
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_FA_PLUS, btn_sz)) { *v += step; changed = true; }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    return changed;
}

// InputFloat with icon +/- stepper buttons.
inline bool InputFloat(const char* label, float* v, float step = 0.1f,
                       float step_fast = 1.0f, const char* fmt = "%.2f",
                       float field_width = 0.0f) {
    bool changed = false;
    float btn_sz = ImGui::GetFrameHeight();

    ImGui::PushID(label);
    if (IconButton(ICON_FA_MINUS, btn_sz)) { *v -= step; changed = true; }
    ImGui::SameLine(0, 2);
    if (field_width > 0) ImGui::SetNextItemWidth(field_width);
    else                 ImGui::SetNextItemWidth(btn_sz * 3.0f);
    if (ImGui::InputFloat("##v", v, 0, 0, fmt)) changed = true;
    ImGui::SameLine(0, 2);
    if (IconButton(ICON_FA_PLUS, btn_sz)) { *v += step; changed = true; }
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    return changed;
}

// CollapsingHeader without frame border.
inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleVar();
    return open;
}

// Animated toggle switch (iOS-style oval track + sliding knob).
inline bool ToggleSwitch(const char* str_id, bool* v) {
    const float h = ImGui::GetFrameHeight() * 0.75f;
    const float w = h * 1.9f;
    const float r = h * 0.5f;

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::PushID(str_id);
    ImGuiID anim_id = ImGui::GetID("##t_anim");
    bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(w, h));
    if (pressed) *v = !*v;
    bool hovered = ImGui::IsItemHovered();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGui::PopID();

    float t = storage->GetFloat(anim_id, *v ? 1.0f : 0.0f);
    float target = *v ? 1.0f : 0.0f;
    float spd = ImGui::GetIO().DeltaTime * 8.0f;
    t = (t < target) ? std::min(t + spd, target) : std::max(t - spd, target);
    storage->SetFloat(anim_id, t);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 off_c = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    ImVec4 on_c  = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImVec4 trk(off_c.x + (on_c.x - off_c.x) * t,
               off_c.y + (on_c.y - off_c.y) * t,
               off_c.z + (on_c.z - off_c.z) * t, 1.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                      ImGui::ColorConvertFloat4ToU32(trk), r);

    float kr = r - 2.0f;
    float kx = pos.x + r + t * (w - h);
    float ky = pos.y + r;
    auto bg = Colors::BackgroundSecondary;
    auto lift = [](float c, float f){ return std::min(c * f, 1.0f); };
    ImU32 knob = hovered ? ImGui::ColorConvertFloat4ToU32(ImVec4(lift(bg.x,3.0f), lift(bg.y,3.0f), lift(bg.z,3.0f), 1.0f))
                         : ImGui::ColorConvertFloat4ToU32(ImVec4(lift(bg.x,2.5f), lift(bg.y,2.5f), lift(bg.z,2.5f), 1.0f));
    dl->AddCircleFilled(ImVec2(kx, ky), kr, knob);

    return pressed;
}

// Search field with embedded magnifier icon and clear button.
inline bool SearchInput(const char* str_id, char* buf, size_t buf_size,
                        float width = -1.0f) {
    ImGui::PushID(str_id);
    const ImGuiStyle& style = ImGui::GetStyle();
    float h = ImGui::GetFrameHeight();
    if (width < 0) width = ImGui::GetContentRegionAvail().x;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 icon_sz = ImGui::CalcTextSize(ICON_FA_SEARCH);
    float pad = style.FramePadding.x;
    float icon_space = pad + icon_sz.x + 4.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(icon_space, style.FramePadding.y));
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputTextWithHint(
        "##in", "\xd0\x9f\xd0\xbe\xd0\xb8\xd1\x81\xd0\xba...", buf, buf_size);
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddText(ImVec2(pos.x + pad, pos.y + (h - icon_sz.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_FA_SEARCH);

    if (buf[0] != '\0') {
        ImVec2 clr_sz = ImGui::CalcTextSize(ICON_FA_TIMES);
        float cx = pos.x + width - pad - clr_sz.x;
        float cy = pos.y + (h - clr_sz.y) * 0.5f;
        ImVec2 saved = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(cx - 4, pos.y));
        if (ImGui::InvisibleButton("##clr", ImVec2(clr_sz.x + 8, h))) {
            buf[0] = '\0';
            changed = true;
        }
        ImU32 cc = ImGui::IsItemHovered()
                       ? ImGui::GetColorU32(ImGuiCol_Text)
                       : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddText(ImVec2(cx, cy), cc, ICON_FA_TIMES);
        ImGui::SetCursorScreenPos(saved);
    }

    ImGui::PopID();
    return changed;
}

// Tag input — words become removable chip-tags inside the field.
// Chips wrap to multiple rows; the frame grows vertically as needed.
inline bool TagInput(const char* str_id,
                     std::vector<std::string>& tags,
                     char* buf, size_t buf_size,
                     float width = -1.0f) {
    ImGui::PushID(str_id);
    bool changed = false;

    const ImGuiStyle& style = ImGui::GetStyle();
    float row_h = ImGui::GetFrameHeight();
    if (width < 0) width = ImGui::GetContentRegionAvail().x;
    float rnd = style.FrameRounding;
    float pad = style.FramePadding.x;
    float pad_y = style.FramePadding.y;
    float chip_h   = row_h - 6.0f;
    float chip_rnd = chip_h * 0.35f;
    float chip_pad = 4.0f;
    float gap  = 3.0f;
    float row_gap = 3.0f;
    float right_edge = width - pad;
    float min_input_w = 60.0f;

    ImVec2 x_sz = ImGui::CalcTextSize(ICON_FA_TIMES);
    float x_w = x_sz.x + chip_pad * 2.0f;

    // --- First pass: compute chip positions & total height ---
    struct ChipLayout { float x, y, w; };
    std::vector<ChipLayout> chips(tags.size());

    float lx = pad;
    float ly = pad_y;
    for (int i = 0; i < (int)tags.size(); ++i) {
        ImVec2 tsz = ImGui::CalcTextSize(tags[i].c_str());
        float cw = chip_pad + tsz.x + chip_pad + x_w;
        if (lx + cw > right_edge && lx > pad + 0.1f) {
            lx = pad;
            ly += chip_h + row_gap;
        }
        chips[i] = { lx, ly, cw };
        lx += cw + gap;
    }

    float input_row_y = ly;
    float remaining = width - pad - lx;
    if (remaining < min_input_w && lx > pad + 0.1f) {
        input_row_y = ly + chip_h + row_gap;
        lx = pad;
    }

    float total_h = input_row_y + row_h + pad_y;

    // --- Reserve space & draw background ---
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(width, total_h));

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + total_h),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), rnd);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + total_h),
                ImGui::GetColorU32(ImGuiCol_Border), rnd);

    // --- Draw chips ---
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool clicked = ImGui::IsMouseClicked(0);
    int remove_idx = -1;

    for (int i = 0; i < (int)tags.size(); ++i) {
        float cx = origin.x + chips[i].x;
        float cy = origin.y + chips[i].y;
        float cw = chips[i].w;

        ImVec4 chip_bg = Colors::AccentSecondary;
        chip_bg.w = 0.70f;
        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + chip_h),
                          ImGui::ColorConvertFloat4ToU32(chip_bg), chip_rnd);

        ImVec2 tsz = ImGui::CalcTextSize(tags[i].c_str());
        float text_y = cy + (chip_h - tsz.y) * 0.5f;
        dl->AddText(ImVec2(cx + chip_pad, text_y),
                    ImGui::GetColorU32(ImGuiCol_Text), tags[i].c_str());

        float xb_x = cx + cw - x_w;
        bool x_hov = mouse.x >= xb_x && mouse.x < xb_x + x_w &&
                     mouse.y >= cy   && mouse.y < cy + chip_h;
        if (x_hov && clicked) remove_idx = i;

        float xi_x = xb_x + chip_pad;
        float xi_y = cy + (chip_h - x_sz.y) * 0.5f;
        dl->AddText(ImVec2(xi_x, xi_y),
                    x_hov ? ImGui::GetColorU32(ImGuiCol_Text)
                          : ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    ICON_FA_TIMES);
    }

    if (remove_idx >= 0) {
        tags.erase(tags.begin() + remove_idx);
        changed = true;
    }

    // --- Input text field ---
    float input_x = origin.x + lx;
    float input_y = origin.y + input_row_y;
    float input_w = std::max(min_input_w, origin.x + width - pad - input_x);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, style.FramePadding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));

    ImGui::SetCursorScreenPos(ImVec2(input_x, input_y));
    ImGui::SetNextItemWidth(input_w);

    struct CBData { bool commit; };
    CBData cbd = { false };

    auto cb = [](ImGuiInputTextCallbackData* data) -> int {
        if (data->EventChar == ' ' || data->EventChar == '\n') {
            ((CBData*)data->UserData)->commit = true;
            return 1;
        }
        return 0;
    };

    ImGuiID input_id = ImGui::GetID("##in");
    ImGui::InputText("##in", buf, buf_size,
                     ImGuiInputTextFlags_CallbackCharFilter,
                     cb, &cbd);

    bool input_active = ImGui::IsItemActive();
    bool need_refocus = false;

    if (cbd.commit && buf[0] != '\0') {
        tags.emplace_back(buf);
        buf[0] = '\0';
        changed = true;
        need_refocus = true;
    }

    if (input_active && buf[0] == '\0' && !tags.empty() &&
        ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        tags.pop_back();
        changed = true;
    }

    if (input_active && buf[0] != '\0' &&
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        tags.emplace_back(buf);
        buf[0] = '\0';
        changed = true;
        need_refocus = true;
    }

    if (need_refocus) {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(input_id))
            state->ClearText();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + total_h + style.ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));

    ImGui::PopID();
    return changed;
}

// Spinning arc loading indicator.
inline void Spinner(const char* str_id, float radius = 10.0f,
                    float thickness = 3.0f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(pos.x + radius, pos.y + radius);
    float a0 = std::fmod((float)ImGui::GetTime() * 5.0f, IM_PI * 2.0f);
    float a1 = a0 + IM_PI * 1.2f;
    ImU32 col = ImGui::GetColorU32(ImGuiCol_Button);
    dl->PathClear();
    for (int i = 0; i <= 30; ++i) {
        float a = a0 + (a1 - a0) * (float)i / 30.0f;
        dl->PathLineTo(ImVec2(c.x + std::cos(a) * radius,
                               c.y + std::sin(a) * radius));
    }
    dl->PathStroke(col, 0, thickness);
}

// Gradient fade-in / fade-out horizontal separator.
inline void GradientSeparator(float pad_x = 0.0f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - pad_x * 2.0f;
    ImGui::Dummy(ImVec2(w + pad_x * 2.0f, 4.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = pos.y + 1.5f;
    ImVec2 p0(pos.x + pad_x, y);
    ImVec2 pm(pos.x + pad_x + w * 0.5f, y + 1.0f);
    ImVec2 p1(pos.x + pad_x + w, y + 1.0f);
    ImU32 trans  = Colors::Accent(0);
    ImU32 accent = Colors::Accent(100);
    dl->AddRectFilledMultiColor(p0, pm, trans, accent, accent, trans);
    dl->AddRectFilledMultiColor(ImVec2(pm.x, p0.y), p1,
                                accent, trans, trans, accent);
}

// Card container with elevated background.
// Pattern: if (CardBegin("Title")) { ...content... } CardEnd();
inline bool CardBegin(const char* title, bool collapsible = true) {
    ImGui::PushID(title);
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors::CardBg);
    ImGui::PushStyleColor(ImGuiCol_Border,  Colors::CardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,  8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    ImGui::BeginChild("##card", ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

    bool open = true;
    if (collapsible) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        open = ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleVar();
    } else {
        ImGui::TextDisabled("%s", title);
    }
    return open;
}

inline void CardEnd() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

// Notification badge drawn at the top-right corner of the previous item.
inline void Badge(int count) {
    if (count <= 0) return;
    ImVec2 mn = ImGui::GetItemRectMin();
    ImVec2 mx = ImGui::GetItemRectMax();
    char text[16];
    std::snprintf(text, sizeof(text), "%d", count > 99 ? 99 : count);
    ImVec2 tsz = ImGui::CalcTextSize(text);
    float r = std::max(tsz.x, tsz.y) * 0.5f + 3.0f;
    ImVec2 center(mx.x - 2.0f, mn.y + 2.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(center, r,
                        ImGui::ColorConvertFloat4ToU32(Colors::Error));
    dl->AddText(ImVec2(center.x - tsz.x * 0.5f, center.y - tsz.y * 0.5f),
                IM_COL32(255, 255, 255, 255), text);
}

// Custom slider (float) styled like ProgressBar: gradient track, gloss, round thumb.
inline bool SliderFloat(const char* label, float* v, float v_min, float v_max,
                        const char* fmt = "%.2f", const ImVec2& size_arg = ImVec2(-1, 0)) {
    ImGui::PushID(label);

    const ImGuiStyle& style = ImGui::GetStyle();
    float avail  = ImGui::GetContentRegionAvail().x;
    ImVec2 tsz   = ImGui::CalcTextSize(label);
    float label_w = tsz.x + style.ItemInnerSpacing.x;
    float w = (size_arg.x > 0) ? size_arg.x : (avail - label_w);
    float h = (size_arg.y > 0) ? size_arg.y
              : (ImGui::GetFontSize() + style.FramePadding.y * 2.0f);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##slider", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        float t  = std::clamp((mx - pos.x) / w, 0.0f, 1.0f);
        *v = v_min + t * (v_max - v_min);
    }

    float frac = (v_max > v_min)
                     ? std::clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f)
                     : 0.0f;
    float rnd = h * 0.5f;
    float thumb_r = h * 0.42f;
    float thumb_cx = std::clamp(pos.x + w * frac,
                                pos.x + thumb_r, pos.x + w - thumb_r);
    float thumb_y  = pos.y + h * 0.5f;
    float fill_end = thumb_cx + thumb_r;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), rnd);

    if (fill_end > pos.x + 1.0f) {
        ImU32 fill = active  ? Colors::AccentBright(1.25f)
                   : hovered ? Colors::AccentBright(1.15f)
                             : Colors::Accent();
        ImVec2 fp1(fill_end, pos.y + h);
        dl->AddRectFilled(pos, fp1, fill, rnd);
        float mid = pos.y + h * 0.4f;
        dl->PushClipRect(pos, ImVec2(fill_end, mid), true);
        dl->AddRectFilled(pos, fp1, IM_COL32(255, 255, 255, 25), rnd);
        dl->PopClipRect();
    }

    ImU32 thumb_shadow = IM_COL32(0, 0, 0, 80);
    auto bg = Colors::BackgroundSecondary;
    auto lift = [](float c, float f){ return std::min(c * f, 1.0f); };
    ImU32 thumb_col    = active  ? ImGui::ColorConvertFloat4ToU32(ImVec4(lift(bg.x,3.5f), lift(bg.y,3.5f), lift(bg.z,3.5f), 1.0f))
                       : hovered ? ImGui::ColorConvertFloat4ToU32(ImVec4(lift(bg.x,3.0f), lift(bg.y,3.0f), lift(bg.z,3.0f), 1.0f))
                                 : ImGui::ColorConvertFloat4ToU32(ImVec4(lift(bg.x,2.5f), lift(bg.y,2.5f), lift(bg.z,2.5f), 1.0f));
    dl->AddCircleFilled(ImVec2(thumb_cx, thumb_y + 1.0f), thumb_r, thumb_shadow);
    dl->AddCircleFilled(ImVec2(thumb_cx, thumb_y), thumb_r, thumb_col);
    dl->AddCircle(ImVec2(thumb_cx, thumb_y), thumb_r, Colors::Accent(120), 0, 1.2f);

    char val_buf[64];
    std::snprintf(val_buf, sizeof(val_buf), fmt, *v);
    ImVec2 val_sz = ImGui::CalcTextSize(val_buf);
    float val_x = pos.x + (w - val_sz.x) * 0.5f;
    float val_y = pos.y + (h - val_sz.y) * 0.5f;
    bool thumb_overlaps_text = (thumb_cx - thumb_r < val_x + val_sz.x) &&
                               (thumb_cx + thumb_r > val_x);
    if (thumb_overlaps_text) {
        dl->AddText(ImVec2(val_x + 1.0f, val_y + 1.0f), IM_COL32(0, 0, 0, 140), val_buf);
    }
    dl->AddText(ImVec2(val_x, val_y),
                ImGui::GetColorU32(ImGuiCol_Text), val_buf);

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    ImGui::PopID();
    return active;
}

// Custom slider (int) styled like ProgressBar.
inline bool SliderInt(const char* label, int* v, int v_min, int v_max,
                      const ImVec2& size_arg = ImVec2(-1, 0)) {
    float fv = static_cast<float>(*v);
    bool changed = SliderFloat(label, &fv,
                               static_cast<float>(v_min),
                               static_cast<float>(v_max),
                               "%0.0f", size_arg);
    *v = static_cast<int>(std::round(fv));
    *v = std::clamp(*v, v_min, v_max);
    return changed;
}

// Progress bar with gradient fill and glossy highlight.
inline void ProgressBar(float fraction, const ImVec2& size_arg = ImVec2(-1, 0),
                        const char* overlay = nullptr) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiStyle& style = ImGui::GetStyle();
    float w = (size_arg.x > 0) ? size_arg.x : ImGui::GetContentRegionAvail().x;
    float h = (size_arg.y > 0) ? size_arg.y
              : (ImGui::GetFontSize() + style.FramePadding.y * 2.0f);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rnd = h * 0.5f;
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h),
                      ImGui::GetColorU32(ImGuiCol_FrameBg), rnd);
    float fw = w * std::clamp(fraction, 0.0f, 1.0f);
    if (fw > 1.0f) {
        ImVec2 fp1(pos.x + fw, pos.y + h);
        dl->AddRectFilled(pos, fp1, Colors::Accent(), rnd);
        float mid = pos.y + h * 0.4f;
        dl->PushClipRect(pos, ImVec2(pos.x + fw, mid), true);
        dl->AddRectFilled(pos, fp1, IM_COL32(255, 255, 255, 25), rnd);
        dl->PopClipRect();

        constexpr float kPeriod   = 3.0f;
        constexpr float kBandFrac = 0.25f;
        constexpr int   kLayers   = 16;
        float st = std::fmod((float)ImGui::GetTime() / kPeriod, 1.0f);
        float band_px = fw * kBandFrac;
        float sweep_x = pos.x - band_px + st * (fw + band_px);

        for (int i = 0; i < kLayers; ++i) {
            float inset = band_px * 0.5f * (float)i / (float)kLayers;
            float cl = std::max(sweep_x + inset, pos.x);
            float cr = std::min(sweep_x + band_px - inset, pos.x + fw);
            if (cr <= cl) continue;
            dl->PushClipRect(ImVec2(cl, pos.y), ImVec2(cr, pos.y + h), true);
            dl->AddRectFilled(pos, fp1, IM_COL32(255, 255, 255, 4), rnd);
            dl->PopClipRect();
        }
    }
    if (overlay) {
        ImVec2 tsz = ImGui::CalcTextSize(overlay);
        dl->AddText(ImVec2(pos.x + (w - tsz.x) * 0.5f,
                           pos.y + (h - tsz.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), overlay);
    }
}

// Animated shimmer text — a bright highlight band sweeps left-to-right
// across the text, similar to the Cursor IDE loading effect.
inline void ShimmerText(const char* text,
                        float speed      = 2.0f,
                        float band_width = 0.35f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_sz = ImGui::CalcTextSize(text);
    ImGui::Dummy(text_sz);

    if (text_sz.x < 1.0f) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font   = ImGui::GetFont();
    float font_size = ImGui::GetFontSize();

    float t = std::fmod((float)ImGui::GetTime() / speed, 1.0f);
    float center = -band_width + t * (1.0f + 2.0f * band_width);
    float half   = band_width * 0.5f;

    ImVec4 base_col    = Colors::TextDisabled;
    constexpr float kLift = 0.45f;
    ImVec4 shimmer_col = ImVec4(
        base_col.x + (1.0f - base_col.x) * kLift,
        base_col.y + (1.0f - base_col.y) * kLift,
        base_col.z + (1.0f - base_col.z) * kLift, 1.0f);

    float x = pos.x;
    const char* s = text;
    while (*s) {
        unsigned int cp = 0;
        int bytes = ImTextCharFromUtf8(&cp, s, nullptr);
        if (bytes <= 0) break;
        const char* char_end = s + bytes;

        ImVec2 ch_sz = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, s, char_end);
        float norm_x = (x + ch_sz.x * 0.5f - pos.x) / text_sz.x;

        float dist = std::abs(norm_x - center) / half;
        float intensity = std::clamp(1.0f - dist, 0.0f, 1.0f);
        intensity = intensity * intensity * (3.0f - 2.0f * intensity);

        ImVec4 col(base_col.x + (shimmer_col.x - base_col.x) * intensity,
                   base_col.y + (shimmer_col.y - base_col.y) * intensity,
                   base_col.z + (shimmer_col.z - base_col.z) * intensity,
                   base_col.w + (shimmer_col.w - base_col.w) * intensity);

        dl->AddText(font, font_size, ImVec2(x, pos.y),
                    ImGui::ColorConvertFloat4ToU32(col), s, char_end);
        x += ch_sz.x;
        s = char_end;
    }
}

// ── Decorative helpers ──────────────────────────────────────────────

inline void AccentBar(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, bool vertical = true) {
    ImU32 accent1 = Colors::Accent();
    ImU32 accent2 = Colors::AccentDim(0.65f);
    if (vertical) {
        float w = 3.0f;
        dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + w, pos.y + size.y),
                                    accent1, accent1, accent2, accent2);
    } else {
        float h = 2.5f;
        dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + size.x, pos.y + h),
                                    accent1, accent2, accent2, accent1);
    }
}

inline void PanelBorder(ImDrawList* dl, const ImVec2& mn, const ImVec2& mx, bool active = false) {
    ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    ImU32 accent = Colors::Accent();
    dl->AddRect(mn, mx, border, 8.0f, 0, 1.0f);
    if (active) {
        float w = (mx.x - mn.x) * 0.3f;
        dl->AddLine(ImVec2(mn.x + 8, mn.y), ImVec2(mn.x + 8 + w, mn.y), accent, 2.0f);
    }
}

// Animated gradient border — N anchor colors rotate around a rounded rect perimeter.
inline void GradientBorder(
    ImDrawList* dl,
    const ImVec2& rect_pos, const ImVec2& rect_size,
    const ImVec4* colors, int color_count,
    float time,
    float speed      = 0.2f,
    float thickness  = 2.0f,
    float rounding   = 12.0f,
    int   segments   = 400)
{
    if (color_count < 2 || !colors || !dl) return;

    const float w = rect_size.x;
    const float h = rect_size.y;
    const float r = std::min(rounding, std::min(w, h) * 0.5f);

    const float straight_h = w - 2.0f * r;
    const float straight_v = h - 2.0f * r;
    const float arc_len    = IM_PI * r * 0.5f;
    const float total      = 2.0f * straight_h + 2.0f * straight_v + 4.0f * arc_len;
    if (total < 1.0f) return;

    // Cumulative distances for 8 path segments (4 edges + 4 corner arcs).
    // Start: top-left corner end -> clockwise.
    const float cum[9] = {
        0.0f,
        straight_h,                                           // top edge
        straight_h + arc_len,                                 // TR arc
        straight_h + arc_len + straight_v,                    // right edge
        straight_h + 2.0f * arc_len + straight_v,             // BR arc
        2.0f * straight_h + 2.0f * arc_len + straight_v,     // bottom edge
        2.0f * straight_h + 3.0f * arc_len + straight_v,     // BL arc
        2.0f * straight_h + 3.0f * arc_len + 2.0f * straight_v, // left edge
        total                                                 // TL arc
    };

    auto point_at = [&](float d) -> ImVec2 {
        d = std::fmod(d, total);
        if (d < 0.0f) d += total;

        if (d <= cum[1]) {
            float f = (straight_h > 0.0f) ? d / straight_h : 0.0f;
            return ImVec2(rect_pos.x + r + f * straight_h, rect_pos.y);
        }
        if (d <= cum[2]) {
            float a = -IM_PI * 0.5f + (d - cum[1]) / arc_len * (IM_PI * 0.5f);
            return ImVec2(rect_pos.x + w - r + std::cos(a) * r,
                          rect_pos.y + r     + std::sin(a) * r);
        }
        if (d <= cum[3]) {
            float f = (straight_v > 0.0f) ? (d - cum[2]) / straight_v : 0.0f;
            return ImVec2(rect_pos.x + w, rect_pos.y + r + f * straight_v);
        }
        if (d <= cum[4]) {
            float a = (d - cum[3]) / arc_len * (IM_PI * 0.5f);
            return ImVec2(rect_pos.x + w - r + std::cos(a) * r,
                          rect_pos.y + h - r + std::sin(a) * r);
        }
        if (d <= cum[5]) {
            float f = (straight_h > 0.0f) ? (d - cum[4]) / straight_h : 0.0f;
            return ImVec2(rect_pos.x + w - r - f * straight_h, rect_pos.y + h);
        }
        if (d <= cum[6]) {
            float a = IM_PI * 0.5f + (d - cum[5]) / arc_len * (IM_PI * 0.5f);
            return ImVec2(rect_pos.x + r     + std::cos(a) * r,
                          rect_pos.y + h - r + std::sin(a) * r);
        }
        if (d <= cum[7]) {
            float f = (straight_v > 0.0f) ? (d - cum[6]) / straight_v : 0.0f;
            return ImVec2(rect_pos.x, rect_pos.y + h - r - f * straight_v);
        }
        {
            float a = IM_PI + (d - cum[7]) / arc_len * (IM_PI * 0.5f);
            return ImVec2(rect_pos.x + r + std::cos(a) * r,
                          rect_pos.y + r + std::sin(a) * r);
        }
    };

    const float offset = std::fmod(time * speed, 1.0f);

    auto color_at = [&](float t) -> ImU32 {
        float p = std::fmod(t + offset, 1.0f);
        if (p < 0.0f) p += 1.0f;

        float scaled = p * (float)color_count;
        int idx_a = (int)scaled % color_count;
        int idx_b = (idx_a + 1) % color_count;
        float frac = scaled - (float)(int)scaled;
        frac = frac * frac * (3.0f - 2.0f * frac); // smoothstep

        const ImVec4& ca = colors[idx_a];
        const ImVec4& cb = colors[idx_b];
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            ca.x + (cb.x - ca.x) * frac,
            ca.y + (cb.y - ca.y) * frac,
            ca.z + (cb.z - ca.z) * frac,
            ca.w + (cb.w - ca.w) * frac));
    };

    // Build positions for all vertices on the perimeter.
    std::vector<ImVec2> pts(segments + 1);
    for (int i = 0; i <= segments; ++i)
        pts[i] = point_at((float)i / (float)segments * total);

    // Compute miter normals. The path is closed so wrap indices.
    std::vector<ImVec2> nrm(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        int ip = (i > 0) ? i - 1 : segments - 1;
        int in = (i < segments) ? i + 1 : 1;

        ImVec2 d0(pts[i].x - pts[ip].x, pts[i].y - pts[ip].y);
        ImVec2 d1(pts[in].x - pts[i].x,  pts[in].y - pts[i].y);
        float l0 = std::sqrt(d0.x * d0.x + d0.y * d0.y);
        float l1 = std::sqrt(d1.x * d1.x + d1.y * d1.y);
        if (l0 > 0.0f) { d0.x /= l0; d0.y /= l0; }
        if (l1 > 0.0f) { d1.x /= l1; d1.y /= l1; }

        ImVec2 d(d0.x + d1.x, d0.y + d1.y);
        float ld = std::sqrt(d.x * d.x + d.y * d.y);
        if (ld > 0.0f) { d.x /= ld; d.y /= ld; }

        nrm[i] = ImVec2(-d.y, d.x);
    }

    // Draw as a connected triangle strip — no joint gaps.
    float half = thickness * 0.5f;
    int vtx_count = (segments + 1) * 2;
    int idx_count = segments * 6;

    dl->PrimReserve(idx_count, vtx_count);
    ImDrawVert* vtx_write = dl->_VtxWritePtr;
    ImDrawIdx*  idx_write = dl->_IdxWritePtr;
    unsigned int vtx_base = dl->_VtxCurrentIdx;
    ImVec2 uv = dl->_Data->TexUvWhitePixel;

    for (int i = 0; i <= segments; ++i) {
        float t = (float)i / (float)segments;
        ImU32 col = color_at(t);
        ImVec2 p = pts[i];
        ImVec2 n = nrm[i];

        vtx_write[0].pos = ImVec2(p.x + n.x * half, p.y + n.y * half);
        vtx_write[0].uv  = uv;
        vtx_write[0].col = col;

        vtx_write[1].pos = ImVec2(p.x - n.x * half, p.y - n.y * half);
        vtx_write[1].uv  = uv;
        vtx_write[1].col = col;

        vtx_write += 2;
    }

    for (int i = 0; i < segments; ++i) {
        unsigned int b = vtx_base + (unsigned int)(i * 2);
        idx_write[0] = (ImDrawIdx)(b);
        idx_write[1] = (ImDrawIdx)(b + 1);
        idx_write[2] = (ImDrawIdx)(b + 2);
        idx_write[3] = (ImDrawIdx)(b + 1);
        idx_write[4] = (ImDrawIdx)(b + 3);
        idx_write[5] = (ImDrawIdx)(b + 2);
        idx_write += 6;
    }

    dl->_VtxWritePtr   = vtx_write;
    dl->_IdxWritePtr   = idx_write;
    dl->_VtxCurrentIdx += (unsigned int)vtx_count;
}

// GradientBorder + outer glow.  Draws several increasingly thick/transparent
// passes behind the sharp border to simulate a soft neon-like shine.
inline void GlowGradientBorder(
    ImDrawList* dl,
    const ImVec2& rect_pos, const ImVec2& rect_size,
    const ImVec4* colors, int color_count,
    float time,
    float speed       = 0.2f,
    float thickness   = 2.0f,
    float rounding    = 12.0f,
    int   segments    = 400,
    float glow_radius = 12.0f,
    int   glow_layers = 16)
{
    if (color_count < 2 || !colors || !dl) return;

    for (int g = glow_layers; g >= 1; --g) {
        float frac = (float)g / (float)glow_layers;
        float layer_thick = thickness + glow_radius * frac;
        float alpha = 0.14f * (1.0f - frac);

        // Expand rect outward so glow only appears outside the main border.
        float expand = (layer_thick - thickness) * 0.5f;
        ImVec2 glow_pos(rect_pos.x - expand, rect_pos.y - expand);
        ImVec2 glow_size(rect_size.x + expand * 2.0f, rect_size.y + expand * 2.0f);
        float  glow_rnd = rounding + expand;

        ImVec4 glow_colors[8];
        int n = std::min(color_count, 8);
        for (int c = 0; c < n; ++c) {
            glow_colors[c] = colors[c];
            glow_colors[c].w = alpha;
        }

        GradientBorder(dl, glow_pos, glow_size,
                        glow_colors, n, time, speed,
                        layer_thick, glow_rnd, segments);
    }

    GradientBorder(dl, rect_pos, rect_size,
                    colors, color_count, time, speed,
                    thickness, rounding, segments);
}

} // namespace DrUI
