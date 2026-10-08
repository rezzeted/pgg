#include "edgar/generator/grid2d/room_template_grid2d.hpp"

#include <stdexcept>
#include <utility>

namespace edgar::generator::grid2d {

RoomTemplateGrid2D::RoomTemplateGrid2D(geometry::PolygonGrid2D outline, std::shared_ptr<IDoorModeGrid2D> doors,
                                       std::string name, std::optional<RoomTemplateRepeatMode> repeat_mode,
                                       std::vector<geometry::TransformationGrid2D> allowed_transformations)
    : outline_(std::move(outline)),
      doors_(std::move(doors)),
      name_(std::move(name)),
      repeat_mode_(repeat_mode),
      allowed_transformations_(std::move(allowed_transformations)) {
    if (!doors_) {
        throw std::invalid_argument("doors");
    }
    if (allowed_transformations_.empty()) {
        allowed_transformations_.push_back(geometry::TransformationGrid2D::Identity);
    }
}

} // namespace edgar::generator::grid2d
