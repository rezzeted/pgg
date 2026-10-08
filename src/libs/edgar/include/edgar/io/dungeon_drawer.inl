#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/vector2_int.hpp"
#include "edgar/io/layout_grid_cells.hpp"
#include "edgar/io/layout_outline_with_door_gaps.hpp"

namespace edgar::io {
namespace detail {

inline void set_pixel(std::vector<std::uint8_t>& rgba, int w, int h, int x, int y, std::uint8_t r, std::uint8_t g,
                        std::uint8_t b) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return;
    }
    const int i = (y * w + x) * 4;
    rgba[static_cast<std::size_t>(i + 0)] = r;
    rgba[static_cast<std::size_t>(i + 1)] = g;
    rgba[static_cast<std::size_t>(i + 2)] = b;
    rgba[static_cast<std::size_t>(i + 3)] = 255;
}

inline std::vector<geometry::Vector2Int> world_outline(const geometry::PolygonGrid2D& local, geometry::Vector2Int pos) {
    std::vector<geometry::Vector2Int> w;
    for (const auto& p : local.points()) {
        w.push_back(p + pos);
    }
    return w;
}

/// Rasterize one orthogonal segment in grid space (inclusive endpoints), same convention as legacy outline loop.
inline void rasterize_orthogonal_world_edge(std::vector<std::uint8_t>& rgba, int img_w, int img_h, double scale,
                                            double ox, double oy, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb,
                                            geometry::Vector2Int from, geometry::Vector2Int to) {
    const int dx = (to.x > from.x) - (to.x < from.x);
    const int dy = (to.y > from.y) - (to.y < from.y);
    int x = from.x;
    int y = from.y;
    while (true) {
        const int px = static_cast<int>(std::floor(x * scale + ox));
        const int py = static_cast<int>(std::floor(y * scale + oy));
        set_pixel(rgba, img_w, img_h, px, py, cr, cg, cb);
        if (x == to.x && y == to.y) {
            break;
        }
        if (from.x == to.x) {
            y += dy;
        } else {
            x += dx;
        }
    }
}

/// Two-pixel-thick orthogonal stroke (pairs of adjacent pixels perpendicular to the segment).
inline void rasterize_orthogonal_world_edge_thick(std::vector<std::uint8_t>& rgba, int img_w, int img_h,
                                                    double scale, double ox, double oy, std::uint8_t cr,
                                                    std::uint8_t cg, std::uint8_t cb, geometry::Vector2Int from,
                                                    geometry::Vector2Int to) {
    const int dx = (to.x > from.x) - (to.x < from.x);
    const int dy = (to.y > from.y) - (to.y < from.y);
    const bool horizontal = (from.y == to.y);
    int x = from.x;
    int y = from.y;
    while (true) {
        const int px = static_cast<int>(std::floor(x * scale + ox));
        const int py = static_cast<int>(std::floor(y * scale + oy));
        set_pixel(rgba, img_w, img_h, px, py, cr, cg, cb);
        if (horizontal) {
            set_pixel(rgba, img_w, img_h, px, py + 1, cr, cg, cb);
        } else {
            set_pixel(rgba, img_w, img_h, px + 1, py, cr, cg, cb);
        }
        if (x == to.x && y == to.y) {
            break;
        }
        if (from.x == to.x) {
            y += dy;
        } else {
            x += dx;
        }
    }
}

/// Dashed stroke in world-grid steps (dash_w on, gap_w off) for internal grid lines.
inline void rasterize_orthogonal_world_edge_dashed(std::vector<std::uint8_t>& rgba, int img_w, int img_h,
                                                   double scale, double ox, double oy, std::uint8_t cr,
                                                   std::uint8_t cg, std::uint8_t cb, geometry::Vector2Int from,
                                                   geometry::Vector2Int to, int dash_w, int gap_w) {
    const int dx = (to.x > from.x) - (to.x < from.x);
    const int dy = (to.y > from.y) - (to.y < from.y);
    int x = from.x;
    int y = from.y;
    int step = 0;
    const int period = dash_w + gap_w;
    while (true) {
        if (period > 0 && step % period < dash_w) {
            const int px = static_cast<int>(std::floor(x * scale + ox));
            const int py = static_cast<int>(std::floor(y * scale + oy));
            set_pixel(rgba, img_w, img_h, px, py, cr, cg, cb);
        }
        if (x == to.x && y == to.y) {
            break;
        }
        if (from.x == to.x) {
            y += dy;
        } else {
            x += dx;
        }
        ++step;
    }
}

} // namespace detail

template <typename TRoom>
void DungeonDrawer<TRoom>::draw_layout_and_save(const generator::grid2d::LayoutGrid2D<TRoom>& layout,
                                               const std::string& path, const DungeonDrawerOptions& options) const {
    if (layout.rooms.empty()) {
        throw std::invalid_argument("layout has no rooms");
    }

    int min_x = std::numeric_limits<int>::max();
    int min_y = std::numeric_limits<int>::max();
    int max_x = std::numeric_limits<int>::min();
    int max_y = std::numeric_limits<int>::min();

    for (const auto& room : layout.rooms) {
        const auto poly = detail::world_outline(room.outline, room.position);
        for (const auto& p : poly) {
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
        }
    }

    const double pad = options.padding_absolute + options.padding_percentage * std::max(max_x - min_x, max_y - min_y);
    const int bbox_w = std::max(1, max_x - min_x + 1);
    const int bbox_h = std::max(1, max_y - min_y + 1);

    double scale = options.scale;
    if (options.width > 0 && options.height > 0) {
        const double sx = (options.width - 2 * pad) / static_cast<double>(bbox_w);
        const double sy = (options.height - 2 * pad) / static_cast<double>(bbox_h);
        scale = std::min(sx, sy);
    }

    const int img_w = std::max(1, static_cast<int>(std::ceil(bbox_w * scale + 2 * pad)));
    const int img_h = std::max(1, static_cast<int>(std::ceil(bbox_h * scale + 2 * pad)));

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(img_w * img_h * 4), 0);
    const auto bg_r = static_cast<std::uint8_t>((options.background_rgb >> 16) & 0xFF);
    const auto bg_g = static_cast<std::uint8_t>((options.background_rgb >> 8) & 0xFF);
    const auto bg_b = static_cast<std::uint8_t>(options.background_rgb & 0xFF);
    for (int y = 0; y < img_h; ++y) {
        for (int x = 0; x < img_w; ++x) {
            detail::set_pixel(rgba, img_w, img_h, x, y, bg_r, bg_g, bg_b);
        }
    }

    const double ox = pad - min_x * scale;
    const double oy = pad - min_y * scale;

    const auto fill_r = static_cast<std::uint8_t>((options.room_fill_rgb >> 16) & 0xFF);
    const auto fill_g = static_cast<std::uint8_t>((options.room_fill_rgb >> 8) & 0xFF);
    const auto fill_b = static_cast<std::uint8_t>(options.room_fill_rgb & 0xFF);

    const auto shade_r = static_cast<std::uint8_t>((options.shade_rgb >> 16) & 0xFF);
    const auto shade_g = static_cast<std::uint8_t>((options.shade_rgb >> 8) & 0xFF);
    const auto shade_b = static_cast<std::uint8_t>(options.shade_rgb & 0xFF);

    const auto grid_r = static_cast<std::uint8_t>((options.grid_rgb >> 16) & 0xFF);
    const auto grid_g = static_cast<std::uint8_t>((options.grid_rgb >> 8) & 0xFF);
    const auto grid_b = static_cast<std::uint8_t>(options.grid_rgb & 0xFF);

    if (options.enable_shading) {
        for (const auto& room : layout.rooms) {
            std::vector<geometry::OrthogonalLineGrid2D> door_lines;
            door_lines.reserve(room.doors.size());
            for (const auto& d : room.doors) {
                door_lines.push_back(d.door_line);
            }
            auto outline_local = layout_outline_with_door_gaps(room.outline, std::move(door_lines));
            if (outline_local.empty()) {
                continue;
            }
            geometry::Vector2Int last = outline_local.back().first + room.position;
            for (const auto& pr : outline_local) {
                const geometry::Vector2Int pt = pr.first + room.position;
                if (pr.second) {
                    detail::rasterize_orthogonal_world_edge_thick(rgba, img_w, img_h, scale, ox, oy, shade_r,
                                                                  shade_g, shade_b, last, pt);
                }
                last = pt;
            }
        }
    }

    for (const auto& room : layout.rooms) {
        const auto world = detail::world_outline(room.outline, room.position);
        int rmin_x = std::numeric_limits<int>::max();
        int rmin_y = std::numeric_limits<int>::max();
        int rmax_x = std::numeric_limits<int>::min();
        int rmax_y = std::numeric_limits<int>::min();
        for (const auto& p : world) {
            rmin_x = std::min(rmin_x, p.x);
            rmin_y = std::min(rmin_y, p.y);
            rmax_x = std::max(rmax_x, p.x);
            rmax_y = std::max(rmax_y, p.y);
        }
        for (int y = rmin_y; y <= rmax_y; ++y) {
            for (int x = rmin_x; x <= rmax_x; ++x) {
                if (point_in_polygon_xy({x, y}, world)) {
                    const int px = static_cast<int>(std::floor(x * scale + ox));
                    const int py = static_cast<int>(std::floor(y * scale + oy));
                    detail::set_pixel(rgba, img_w, img_h, px, py, fill_r, fill_g, fill_b);
                }
            }
        }
    }

    if (options.enable_grid_lines) {
        for (const auto& room : layout.rooms) {
            const geometry::PolygonGrid2D poly_world = room.outline + room.position;
            const std::vector<geometry::Vector2Int> lattice = grid_cell_lattice_points(poly_world);
            const auto lattice_set = grid_cell_lattice_point_set(lattice);
            for (const auto& p : lattice) {
                const geometry::Vector2Int right{p.x + 1, p.y};
                const geometry::Vector2Int bottom{p.x, p.y - 1};
                if (lattice_set_contains(lattice_set, right)) {
                    detail::rasterize_orthogonal_world_edge_dashed(rgba, img_w, img_h, scale, ox, oy, grid_r, grid_g,
                                                                   grid_b, p, right, 2, 2);
                }
                if (lattice_set_contains(lattice_set, bottom)) {
                    detail::rasterize_orthogonal_world_edge_dashed(rgba, img_w, img_h, scale, ox, oy, grid_r, grid_g,
                                                                   grid_b, p, bottom, 2, 2);
                }
            }
        }
    }

    const auto line_r = static_cast<std::uint8_t>((options.outline_rgb >> 16) & 0xFF);
    const auto line_g = static_cast<std::uint8_t>((options.outline_rgb >> 8) & 0xFF);
    const auto line_b = static_cast<std::uint8_t>(options.outline_rgb & 0xFF);

    for (const auto& room : layout.rooms) {
        std::vector<geometry::OrthogonalLineGrid2D> door_lines;
        door_lines.reserve(room.doors.size());
        for (const auto& d : room.doors) {
            door_lines.push_back(d.door_line);
        }
        auto outline_local = layout_outline_with_door_gaps(room.outline, std::move(door_lines));
        if (outline_local.empty()) {
            continue;
        }
        geometry::Vector2Int last = outline_local.back().first + room.position;
        for (const auto& pr : outline_local) {
            const geometry::Vector2Int pt = pr.first + room.position;
            if (pr.second) {
                detail::rasterize_orthogonal_world_edge_thick(rgba, img_w, img_h, scale, ox, oy, line_r, line_g,
                                                                line_b, last, pt);
            }
            last = pt;
        }
    }

    write_png_rgba(path, img_w, img_h, rgba);
}

} // namespace edgar::io
