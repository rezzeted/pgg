#pragma once

namespace edgar::generator::common {

/// Aggregated constraint penalties for a layout (ported subset of Edgar `EnergyData`).
struct EnergyData {
    double overlap_penalty = 0;
    double corridor_penalty = 0;
    double minimum_distance_penalty = 0;
    double move_distance_penalty = 0;

    /// C# `EnergyData.IsValid`: valid iff no overlap AND all graph-neighbors have door-compatible positions.
    bool is_valid() const { return overlap_penalty <= 0.0 && move_distance_penalty <= 0.0; }
};

} // namespace edgar::generator::common
