#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "edgar/generator/common/room_template_repeat_mode.hpp"
#include "edgar/generator/grid2d/door_mode_grid2d.hpp"
#include "edgar/geometry/polygon_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

namespace edgar::generator::grid2d {

class RoomTemplateGrid2D {
public:
    RoomTemplateGrid2D(geometry::PolygonGrid2D outline, std::shared_ptr<IDoorModeGrid2D> doors,
                       std::string name = "Room template",
                       std::optional<RoomTemplateRepeatMode> repeat_mode = std::nullopt,
                       std::vector<geometry::TransformationGrid2D> allowed_transformations = {});

    const std::string& name() const { return name_; }
    const geometry::PolygonGrid2D& outline() const { return outline_; }
    const IDoorModeGrid2D& doors() const { return *doors_; }
    std::optional<RoomTemplateRepeatMode> repeat_mode() const { return repeat_mode_; }
    const std::vector<geometry::TransformationGrid2D>& allowed_transformations() const {
        return allowed_transformations_;
    }

private:
    geometry::PolygonGrid2D outline_;
    std::shared_ptr<IDoorModeGrid2D> doors_;
    std::string name_;
    std::optional<RoomTemplateRepeatMode> repeat_mode_;
    std::vector<geometry::TransformationGrid2D> allowed_transformations_;
};

} // namespace edgar::generator::grid2d
