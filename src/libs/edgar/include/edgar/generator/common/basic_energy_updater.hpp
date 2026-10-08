#pragma once

#include "edgar/generator/common/energy_data.hpp"

#include <algorithm>
#include <cmath>

namespace edgar::generator::common {

/// Maps `EnergyData` to a scalar for minimization.
/// C# formula: `exp(overlap/(sigma*625)) * exp(distance/(sigma*50)) - 1`
/// where `sigma = 10 * averageSize` (the `energy_sigma` parameter).
struct BasicEnergyUpdater {
    static double total_penalty(const EnergyData& e, double energy_sigma = 1.0) {
        const double sigma = std::max(energy_sigma, 1.0);
        const double basic = std::exp(e.overlap_penalty / (sigma * 625.0))
                           * std::exp(e.move_distance_penalty / (sigma * 50.0))
                           - 1.0;
        return basic + e.corridor_penalty + e.minimum_distance_penalty;
    }
};

} // namespace edgar::generator::common
