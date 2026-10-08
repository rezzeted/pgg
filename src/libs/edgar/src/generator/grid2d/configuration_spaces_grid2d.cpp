#include "edgar/generator/grid2d/configuration_spaces_grid2d.hpp"

#include "edgar/generator/grid2d/configuration_spaces_generator.hpp"
#include "edgar/geometry/orthogonal_line_intersection.hpp"
#include "edgar/geometry/overlap.hpp"

#include <algorithm>

namespace edgar::generator::grid2d {

const std::unordered_set<geometry::Vector2Int>& ConfigurationSpaceGrid2D::points() const {
    if (!points_cache_) {
        auto built = std::make_shared<std::unordered_set<geometry::Vector2Int>>();
        for (const auto& line : lines) {
            for (const auto& p : line.grid_points_inclusive()) {
                built->insert(p);
            }
        }
        points_cache_ = std::move(built);
    }
    return *points_cache_;
}

bool ConfigurationSpaceGrid2D::contains_offset(geometry::Vector2Int p) const {
    return points().count(p) != 0;
}

bool offset_on_configuration_space(geometry::Vector2Int offset, const ConfigurationSpaceGrid2D& space) {
    return space.contains_offset(offset);
}

std::vector<geometry::Vector2Int> enumerate_configuration_space_offsets(const ConfigurationSpaceGrid2D& space) {
    std::vector<geometry::Vector2Int> out;
    constexpr std::size_t kMaxTotal = 4000;
    for (const auto& line : space.lines) {
        const auto pts = line.grid_points_inclusive();
        std::size_t stride = 1;
        if (pts.size() > 600) {
            stride = pts.size() / 600 + 1;
        }
        for (std::size_t i = 0; i < pts.size() && out.size() < kMaxTotal; i += stride) {
            out.push_back(pts[i]);
        }
    }
    std::sort(out.begin(), out.end(), [](const geometry::Vector2Int& a, const geometry::Vector2Int& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const geometry::Vector2Int& a, const geometry::Vector2Int& b) {
                              return a.x == b.x && a.y == b.y;
                          }),
              out.end());
    return out;
}

static bool placement_non_overlapping(int moving_index, geometry::Vector2Int candidate_pos,
                                      const std::vector<geometry::PolygonGrid2D>& outlines,
                                      const std::vector<geometry::Vector2Int>& positions,
                                      const std::vector<bool>& placed) {
    for (std::size_t j = 0; j < outlines.size(); ++j) {
        if (static_cast<int>(j) == moving_index || !placed[j]) {
            continue;
        }
        if (geometry::polygons_overlap_area(outlines[static_cast<std::size_t>(moving_index)], candidate_pos,
                                            outlines[j], positions[j])) {
            return false;
        }
    }
    return true;
}

// C# `ConfigurationSpacesGrid2D.GetMaximumIntersection`: exact intersection of configuration
// space lines (offset by neighbor positions) over the largest satisfiable subset of neighbors.
static std::vector<geometry::OrthogonalLineGrid2D> maximum_intersection_lines(
    const std::vector<ConfigurationSpaceGrid2D>& css, const std::vector<int>& neighbor_indices,
    const std::vector<geometry::Vector2Int>& positions, std::mt19937& rng) {
    const int d = static_cast<int>(neighbor_indices.size());
    if (d == 0) {
        return {};
    }
    std::vector<int> order(static_cast<std::size_t>(d));
    for (int i = 0; i < d; ++i) {
        order[static_cast<std::size_t>(i)] = i;
    }
    std::shuffle(order.begin(), order.end(), rng);

    // Subset sizes from d down to 1 (C# GetCombinations order), indices into `order`.
    // Full-neighbor intersection is tried first; smaller subsets only when it is empty —
    // matches C# GetMaximumIntersection and is required for convergence on dense maps.
    for (int k = d; k >= 1; --k) {
        std::vector<int> idx(static_cast<std::size_t>(k));
        for (int i = 0; i < k; ++i) {
            idx[static_cast<std::size_t>(i)] = i;
        }
        while (true) {
            std::vector<geometry::OrthogonalLineGrid2D> intersection;
            bool empty = false;
            for (int i = 0; i < k && !empty; ++i) {
                const int li = order[static_cast<std::size_t>(idx[static_cast<std::size_t>(i)])];
                const auto& pos = positions[static_cast<std::size_t>(neighbor_indices[static_cast<std::size_t>(li)])];
                std::vector<geometry::OrthogonalLineGrid2D> shifted;
                shifted.reserve(css[static_cast<std::size_t>(li)].lines.size());
                for (const auto& line : css[static_cast<std::size_t>(li)].lines) {
                    shifted.push_back(line + pos);
                }
                intersection = intersection.empty()
                                   ? std::move(shifted)
                                   : geometry::OrthogonalLineIntersection::get_intersections(shifted, intersection);
                if (intersection.empty()) {
                    empty = true;
                }
            }
            if (!empty) {
                return intersection;
            }
            // Next k-subset in lexicographic order
            int i = k - 1;
            while (i >= 0 && idx[static_cast<std::size_t>(i)] == d - k + i) {
                --i;
            }
            if (i < 0) {
                break;
            }
            ++idx[static_cast<std::size_t>(i)];
            for (int j = i + 1; j < k; ++j) {
                idx[static_cast<std::size_t>(j)] = idx[static_cast<std::size_t>(j - 1)] + 1;
            }
        }
    }
    return {};
}

std::optional<geometry::Vector2Int> sample_maximum_intersection_position(
    const geometry::PolygonGrid2D& moving, const std::vector<DoorLineGrid2D>& moving_doors,
    const std::vector<int>& neighbor_indices, int moving_index, const std::vector<geometry::PolygonGrid2D>& outlines,
    const std::vector<geometry::Vector2Int>& positions, const std::vector<std::vector<DoorLineGrid2D>>& neighbor_doors,
    const std::vector<bool>& placed, std::mt19937& rng, std::size_t max_point_checks, bool moving_is_corridor,
    const std::vector<bool>* neighbor_is_corridor_by_index) {
    if (neighbor_indices.empty()) {
        return std::nullopt;
    }

    // NOTE: corridor doors are fixed outline doors everywhere else in the port
    // (validation in precompute_cs_validity, door extraction in compute_layout_doors),
    // so placement must use the same regular configuration space. The former
    // get_configuration_space_over_corridor self-call (corridor == moving) produced a
    // set disjoint from the validated one and the generator could never converge.
    (void)moving_is_corridor;
    (void)neighbor_is_corridor_by_index;
    ConfigurationSpacesGenerator gen;
    std::vector<ConfigurationSpaceGrid2D> css;
    css.reserve(neighbor_indices.size());
    for (int k : neighbor_indices) {
        css.push_back(gen.get_configuration_space(moving, moving_doors, outlines[static_cast<std::size_t>(k)],
                                                    neighbor_doors[static_cast<std::size_t>(k)]));
    }

    // Exact maximum intersection over neighbor subsets (C#), then a random point of it
    const auto intersection = maximum_intersection_lines(css, neighbor_indices, positions, rng);
    std::vector<geometry::Vector2Int> candidates;
    for (const auto& line : intersection) {
        const auto pts = line.grid_points_inclusive();
        std::size_t stride = 1;
        if (pts.size() > 600) {
            stride = pts.size() / 600 + 1;
        }
        for (std::size_t i = 0; i < pts.size(); i += stride) {
            candidates.push_back(pts[i]);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const geometry::Vector2Int& a, const geometry::Vector2Int& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const geometry::Vector2Int& a, const geometry::Vector2Int& b) {
                                     return a.x == b.x && a.y == b.y;
                                 }),
                     candidates.end());
    std::shuffle(candidates.begin(), candidates.end(), rng);

    std::size_t inspected = 0;
    for (const geometry::Vector2Int& pi : candidates) {
        if (inspected >= max_point_checks) {
            break;
        }
        ++inspected;
        if (!placement_non_overlapping(moving_index, pi, outlines, positions, placed)) {
            continue;
        }
        return pi;
    }
    return std::nullopt;
}

} // namespace edgar::generator::grid2d
