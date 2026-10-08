#pragma once

#include <stdexcept>
#include <vector>

#include "edgar/geometry/vector2_int.hpp"

namespace edgar::geometry {

/// Matches C# OrthogonalLineGrid2D.Direction (excluding degenerate handling in ctor).
enum class OrthogonalDirection {
    Undefined = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
    Left = 4,
};

OrthogonalDirection opposite_direction(OrthogonalDirection d);

/// Rotates a direction by the given number of clockwise degrees (must be a multiple of 90).
OrthogonalDirection rotate_direction(OrthogonalDirection d, int degrees_clockwise);

struct OrthogonalLineGrid2D {
    Vector2Int from{};
    Vector2Int to{};
    /// Direction override for degenerate (point) lines (C# `degeneratedDirection`); ignored
    /// for non-degenerate lines, where the direction is derived from the endpoints.
    OrthogonalDirection degenerated_direction = OrthogonalDirection::Undefined;

    OrthogonalLineGrid2D() = default;
    OrthogonalLineGrid2D(Vector2Int f, Vector2Int t);
    /// C# ctor with explicit direction; validates consistency for non-degenerate lines.
    OrthogonalLineGrid2D(Vector2Int f, Vector2Int t, OrthogonalDirection degenerated_dir);

    int length() const { return Vector2Int::manhattan_distance(from, to); }

    OrthogonalDirection get_direction() const;

    /// Unit step along the line from `from` toward `to` (C# `GetDirectionVector`).
    Vector2Int direction_vector() const;

    /// Inclusive grid points from `from` to `to` along the segment.
    std::vector<Vector2Int> grid_points_inclusive() const;

    /// Whether `p` lies on this closed orthogonal segment (inclusive).
    bool contains_point(Vector2Int p) const;

    /// Returns 0-based index of `p` along the line, or -1 if not on the segment (C# `Contains`).
    int index_of_point(Vector2Int p) const;

    /// Returns the n-th point along the line (C# `GetNthPoint`).
    Vector2Int nth_point(int n) const { return from + direction_vector() * n; }

    /// Clockwise rotation around the origin (same convention as C# / Vector2Int::rotate_around_center).
    OrthogonalLineGrid2D rotate(int degrees_clockwise) const;

    /// Rotate; if `normalize_after`, orient `from`→`to` along the canonical direction (C# `Rotate(..., normalize)`).
    OrthogonalLineGrid2D rotate(int degrees_clockwise, bool normalize_after) const;

    /// Swap from/to (C# `SwitchOrientation`); the stored direction flips accordingly.
    OrthogonalLineGrid2D switch_orientation() const {
        const OrthogonalDirection dir = get_direction();
        return OrthogonalDirection::Undefined == dir
                   ? OrthogonalLineGrid2D(to, from)
                   : OrthogonalLineGrid2D(to, from, opposite_direction(dir));
    }

    /// Canonical `from`→`to` for the line direction (C# `GetNormalized`).
    OrthogonalLineGrid2D normalized() const;

    /// Degrees to rotate so this line becomes horizontal Right (C# `ComputeRotation`).
    int compute_rotation_clockwise_degrees() const;

    /// Removes `from_amount` steps from the From end and `to_amount` from the To end after rotating
    /// the segment to horizontal "Right" (see C# OrthogonalLineGrid2D.Shrink).
    OrthogonalLineGrid2D shrink(int from_amount, int to_amount) const;
    OrthogonalLineGrid2D shrink(int symmetric_amount) const { return shrink(symmetric_amount, symmetric_amount); }

    friend OrthogonalLineGrid2D operator+(OrthogonalLineGrid2D line, Vector2Int offset) {
        return OrthogonalLineGrid2D(line.from + offset, line.to + offset, line.degenerated_direction);
    }

private:
    int compute_rotation_to_right() const;
};

OrthogonalDirection opposite_direction(OrthogonalDirection d);

/// Rotates a direction by the given number of clockwise degrees (must be a multiple of 90).
OrthogonalDirection rotate_direction(OrthogonalDirection d, int degrees_clockwise);

} // namespace edgar::geometry
