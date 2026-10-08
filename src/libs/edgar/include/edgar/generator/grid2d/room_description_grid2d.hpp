#pragma once

#include <stdexcept>
#include <vector>

#include "edgar/generator/grid2d/room_template_grid2d.hpp"

namespace edgar::generator::grid2d {

class RoomDescriptionGrid2D {
public:
    /// \param stage Generation stage (1 = main graph for chain decomposition; 2 = merged in two-stage layouts). Matches Edgar-DotNet room stage.
    RoomDescriptionGrid2D(bool is_corridor, std::vector<RoomTemplateGrid2D> room_templates, int stage = 1)
        : is_corridor_(is_corridor), room_templates_(std::move(room_templates)), stage_(stage) {
        if (room_templates_.empty()) {
            throw std::invalid_argument("room_templates must not be empty");
        }
        if (stage != 1 && stage != 2) {
            throw std::invalid_argument("stage must be 1 or 2");
        }
    }

    bool is_corridor() const { return is_corridor_; }
    const std::vector<RoomTemplateGrid2D>& room_templates() const { return room_templates_; }

    /// Two-stage generation: stage-one rooms form `get_stage_one_graph()`; stage-two rooms are attached later.
    int stage() const { return stage_; }

private:
    bool is_corridor_{};
    std::vector<RoomTemplateGrid2D> room_templates_;
    int stage_ = 1;
};

} // namespace edgar::generator::grid2d
