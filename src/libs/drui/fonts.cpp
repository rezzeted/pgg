#include "drui/fonts.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static std::string s_resolved_icon_path;

static const char* find_icon_font() {
    namespace fs = std::filesystem;

    fs::path exe_dir;
#ifdef _WIN32
    char buf[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, buf, MAX_PATH) > 0)
        exe_dir = fs::path(buf).parent_path();
#elif defined(__APPLE__)
    char buf[1024]{};
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        exe_dir = fs::canonical(fs::path(buf)).parent_path();
#endif

    const char* names[] = {
        "fa-solid-900.otf",
        "Font Awesome 6 Free-Solid-900.otf",
        "Font Awesome 7 Free-Solid-900.otf",
    };

    for (const char* name : names) {
        if (!exe_dir.empty()) {
            auto p = exe_dir / "resources" / "fonts" / name;
            if (fs::exists(p)) { s_resolved_icon_path = p.string(); return s_resolved_icon_path.c_str(); }
        }
        fs::path rel = fs::path("resources") / "fonts" / name;
        if (fs::exists(rel)) { s_resolved_icon_path = rel.string(); return s_resolved_icon_path.c_str(); }
        fs::path up3 = fs::path("../../..") / "resources" / "fonts" / name;
        if (fs::exists(up3)) { s_resolved_icon_path = up3.string(); return s_resolved_icon_path.c_str(); }
    }
    return nullptr;
}

static ImFont* LoadFontLadder(ImGuiIO& io, const char* path,
                              const ImFontConfig& cfg,
                              const ImWchar* glyph_ranges,
                              float base_px, const float* sizes_mul, int n_sizes,
                              const char* icon_path, const ImWchar* icon_ranges) {
    ImFont* base_font = nullptr;
    ImFont* first = nullptr;
    for (int i = 0; i < n_sizes; ++i) {
        const float px = std::round(base_px * sizes_mul[i]);
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, px, &cfg, glyph_ranges);
        if (!f) continue;
        if (!first) first = f;
        if (icon_path) {
            ImFontConfig icon_cfg{};
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = px;
            io.Fonts->AddFontFromFileTTF(icon_path, px, &icon_cfg, icon_ranges);
        }
        if (std::fabs(px - base_px) <= 0.5f) base_font = f;
    }
    return base_font ? base_font : first;
}

namespace DrUI {

ImFont* SetupFonts(ImGuiIO& io, float dpi_scale) {
    static const ImWchar cyrillic_ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
        0x2DE0, 0x2DFF, // Cyrillic Extended-A
        0xA640, 0xA69F, // Cyrillic Extended-B
        0,
    };

    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;
    cfg.RasterizerMultiply = 1.0f;

    const float base_px = 16.0f * dpi_scale;
    const float sizes_mul[] = {0.75f, 0.875f, 1.0f, 1.25f, 1.625f, 2.125f};
    const int n_sizes = (int)(sizeof(sizes_mul) / sizeof(sizes_mul[0]));

    const char* icon_path = find_icon_font();
    static const ImWchar icon_ranges[] = {0xf000, 0xf8ff, 0};

    if (icon_path)
        std::fprintf(stderr, "[font] icon font found: %s\n", icon_path);
    else
        std::fprintf(stderr, "[font] icon font NOT found — place fa-solid-900.otf into resources/fonts/\n");

    const char* fonts[] = {
#ifdef _WIN32
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
    };

    namespace fs = std::filesystem;
    ImFont* loaded_default = nullptr;

    for (const char* path : fonts) {
        if (!fs::exists(path)) continue;
        ImFont* f = LoadFontLadder(io, path, cfg, cyrillic_ranges,
                                   base_px, sizes_mul, n_sizes,
                                   icon_path, icon_ranges);
        if (f) {
            loaded_default = f;
            std::fprintf(stderr, "[font] loaded from: %s (base=%.0fpx)\n", path, base_px);
            break;
        }
    }

    if (!loaded_default)
        std::fprintf(stderr, "[font] load failed — using default ImGui font\n");

    return loaded_default;
}

} // namespace DrUI
