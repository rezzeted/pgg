#pragma once

#include <cstdint>

namespace edgar::io {

struct DungeonDrawerOptions {
    int width = 2000;
    int height = 2000;
    double scale = 1.0;
    double padding_absolute = 4.0;
    /// Matches C# DungeonDrawerOptions default PaddingPercentage (0.15f).
    double padding_percentage = 0.15;
    /// Matches C# BackgroundColor / RoomBackgroundColor (248,248,244).
    std::uint32_t background_rgb = 0xF8F8F4;
    std::uint32_t room_fill_rgb = 0xF8F8F4;
    /// Wall outline (~RGB 50,50,50 in C# DungeonDrawer outlinePen).
    std::uint32_t outline_rgb = 0x323232;
    /// Shading pass (C# shadePen 204,206,206).
    std::uint32_t shade_rgb = 0xCCCECE;
    /// Internal grid edges (C# gridPen ~100,100,100).
    std::uint32_t grid_rgb = 0x646464;
    bool enable_shading = true;
    bool enable_grid_lines = true;
};

} // namespace edgar::io
