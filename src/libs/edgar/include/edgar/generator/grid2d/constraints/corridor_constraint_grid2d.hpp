#pragma once

#include "edgar/generator/common/energy_data.hpp"

#include <cstddef>
#include <vector>

namespace edgar::generator::grid2d::constraints {

struct CorridorConstraintGrid2D {
    static common::EnergyData evaluate_pair(std::size_t i, std::size_t j, const common::EnergyData& basic_pair_energy,
                                            const std::vector<bool>* is_corridor,
                                            bool optimize_corridor_constraints) {
        common::EnergyData out;
        if (!optimize_corridor_constraints || is_corridor == nullptr) {
            return out;
        }
        if (basic_pair_energy.overlap_penalty > 0.0 && (*is_corridor)[i] != (*is_corridor)[j]) {
            out.corridor_penalty = 1.0;
        }
        return out;
    }
};

} // namespace edgar::generator::grid2d::constraints
