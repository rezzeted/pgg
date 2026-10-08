#pragma once

#include "edgar/generator/common/simulated_annealing_configuration.hpp"

#include <stdexcept>
#include <vector>

namespace edgar::generator::common {

class SAConfigurationProvider {
public:
    explicit SAConfigurationProvider(SimulatedAnnealingConfiguration fixed)
        : fixed_(std::move(fixed)), use_fixed_(true) {}

    explicit SAConfigurationProvider(std::vector<SimulatedAnnealingConfiguration> per_chain)
        : per_chain_(std::move(per_chain)), use_fixed_(false) {
        if (per_chain_.empty()) {
            throw std::invalid_argument("SAConfigurationProvider: per_chain must not be empty");
        }
    }

    const SimulatedAnnealingConfiguration& get(int chain_number) const {
        if (chain_number < 0) {
            throw std::invalid_argument("SAConfigurationProvider: chain_number must be non-negative");
        }
        if (use_fixed_) {
            return fixed_;
        }
        if (chain_number >= static_cast<int>(per_chain_.size())) {
            throw std::invalid_argument("SAConfigurationProvider: chain_number out of range");
        }
        return per_chain_[chain_number];
    }

    bool use_fixed() const { return use_fixed_; }

    int chain_count() const { return use_fixed_ ? -1 : static_cast<int>(per_chain_.size()); }

private:
    SimulatedAnnealingConfiguration fixed_;
    std::vector<SimulatedAnnealingConfiguration> per_chain_;
    bool use_fixed_;
};

} // namespace edgar::generator::common
