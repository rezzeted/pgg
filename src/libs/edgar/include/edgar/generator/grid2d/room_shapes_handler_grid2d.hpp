#pragma once

#include "edgar/generator/common/room_template_repeat_mode.hpp"
#include "edgar/generator/grid2d/level_description_grid2d.hpp"
#include "edgar/generator/grid2d/level_description_mapping_grid2d.hpp"
#include "edgar/generator/grid2d/weighted_shape_grid2d.hpp"
#include "edgar/geometry/transformation_grid2d.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace edgar::generator::grid2d {

template <typename TRoom>
class RoomShapesHandlerGrid2D {
public:
    struct ShapeSelection {
        RoomTemplateGrid2D room_template;
        geometry::TransformationGrid2D transformation = geometry::TransformationGrid2D::Identity;
        geometry::PolygonGrid2D outline;
        int alias = -1;
    };

    RoomShapesHandlerGrid2D(const LevelDescriptionGrid2D<TRoom>& level, const LevelDescriptionMappingGrid2D<TRoom>& mapping)
        : level_(level), mapping_(mapping), full_graph_(level.get_graph()) {
        build_alias_table();
    }

    /// Mirrors C# `RoomShapesHandler.GetPossibleShapesForNode`: candidate shapes for `room_index`
    /// after filtering by the repeat modes of templates placed on *other* rooms.
    /// `placed_aliases[i]` is the shape alias currently assigned to room i (nullopt if unplaced).
    /// With `try_to_fix_empty`, relaxes NoRepeat -> NoImmediate -> AllowRepeat like the C# handler.
    /// `mode_override` corresponds to the C# `repeatModeOverride` constructor argument.
    std::vector<ShapeSelection> possible_shapes_for_room(
        int room_index, const std::vector<std::optional<int>>& placed_aliases, bool try_to_fix_empty = false,
        std::optional<RoomTemplateRepeatMode> mode_override = std::nullopt) const {
        const auto& candidates = weighted_shapes_by_room_[static_cast<std::size_t>(room_index)];

        // Corridor rooms are exempt from repeat-mode filtering (C#: returns all shapes for the node)
        if (level_.get_room_description(mapping_.room_id(room_index)).is_corridor()) {
            return selections_from(candidates, std::nullopt);
        }

        auto shapes = filter_by_repeat_mode(room_index, placed_aliases, mode_override);
        if (shapes.empty() && try_to_fix_empty) {
            if (!mode_override.has_value() || *mode_override == RoomTemplateRepeatMode::NoRepeat) {
                shapes = filter_by_repeat_mode(room_index, placed_aliases, RoomTemplateRepeatMode::NoImmediate);
            }
            if (shapes.empty() && (!mode_override.has_value() || *mode_override != RoomTemplateRepeatMode::AllowRepeat)) {
                shapes = filter_by_repeat_mode(room_index, placed_aliases, RoomTemplateRepeatMode::AllowRepeat);
            }
        }
        return shapes;
    }

    ShapeSelection select_for_room(
        int room_index, std::mt19937& rng, const std::vector<std::optional<RoomTemplateGrid2D>>* placed_templates = nullptr,
        const std::vector<geometry::TransformationGrid2D>* placed_transforms = nullptr,
        std::optional<int> previous_alias = std::nullopt) const {
        const auto mode = effective_repeat_mode(room_index);
        const auto& candidates = weighted_shapes_by_room_[static_cast<std::size_t>(room_index)];
        std::vector<std::size_t> allowed;
        std::vector<double> weights;
        allowed.reserve(candidates.size());
        weights.reserve(candidates.size());

        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const int alias = candidates[i].shape_alias;
            if (!is_allowed_by_repeat_mode(mode, alias, placed_templates, placed_transforms, previous_alias)) {
                continue;
            }
            allowed.push_back(i);
            weights.push_back(candidates[i].weight);
        }

        if (allowed.empty()) {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                allowed.push_back(i);
                weights.push_back(candidates[i].weight);
            }
        }

        std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
        const auto& chosen = candidates[allowed[pick(rng)]];
        return ShapeSelection{
            .room_template = chosen.room_template,
            .transformation = chosen.transformation,
            .outline = chosen.room_template.outline().transform(chosen.transformation),
            .alias = chosen.shape_alias,
        };
    }

    int alias_for(const RoomTemplateGrid2D& room_template, geometry::TransformationGrid2D transformation) const {
        const auto key = std::make_tuple(room_template.name(), static_cast<int>(transformation));
        auto it = alias_by_template_transform_.find(key);
        if (it == alias_by_template_transform_.end()) {
            throw std::invalid_argument("RoomShapesHandlerGrid2D: unknown template/transformation alias");
        }
        return it->second;
    }

private:
    struct WeightedShapeInstance {
        RoomTemplateGrid2D room_template;
        geometry::TransformationGrid2D transformation = geometry::TransformationGrid2D::Identity;
        int shape_alias = -1;
        double weight = 1.0;
    };

    const LevelDescriptionGrid2D<TRoom>& level_;
    const LevelDescriptionMappingGrid2D<TRoom>& mapping_;
    graphs::UndirectedAdjacencyListGraph<TRoom> full_graph_;
    std::map<std::tuple<std::string, int>, int> alias_by_template_transform_{};
    std::vector<std::vector<WeightedShapeInstance>> weighted_shapes_by_room_{};
    // Alias metadata for C#-style repeat filtering: template group and per-template repeat mode
    std::map<int, std::string> alias_template_name_{};
    std::map<std::string, std::vector<int>> aliases_by_template_name_{};
    std::map<std::string, std::optional<RoomTemplateRepeatMode>> repeat_mode_by_template_name_{};

    std::vector<ShapeSelection> selections_from(const std::vector<WeightedShapeInstance>& candidates,
                                                std::optional<std::set<int>> excluded) const {
        std::vector<ShapeSelection> out;
        for (const auto& c : candidates) {
            if (excluded.has_value() && excluded->count(c.shape_alias) != 0) {
                continue;
            }
            out.push_back(ShapeSelection{
                .room_template = c.room_template,
                .transformation = c.transformation,
                .outline = c.room_template.outline().transform(c.transformation),
                .alias = c.shape_alias,
            });
        }
        return out;
    }

    std::vector<ShapeSelection> filter_by_repeat_mode(
        int room_index, const std::vector<std::optional<int>>& placed_aliases,
        std::optional<RoomTemplateRepeatMode> mode_override) const {
        std::set<int> excluded;
        for (std::size_t j = 0; j < placed_aliases.size(); ++j) {
            if (static_cast<int>(j) == room_index || !placed_aliases[j].has_value()) {
                continue;
            }
            const int placed_alias = *placed_aliases[j];
            const auto name_it = alias_template_name_.find(placed_alias);
            if (name_it == alias_template_name_.end()) {
                continue;
            }
            const std::string& template_name = name_it->second;
            RoomTemplateRepeatMode mode = RoomTemplateRepeatMode::AllowRepeat;
            const auto mode_it = repeat_mode_by_template_name_.find(template_name);
            if (mode_it != repeat_mode_by_template_name_.end() && mode_it->second.has_value()) {
                mode = *mode_it->second;
            }
            if (mode_override.has_value()) {
                mode = *mode_override;
            }
            const bool no_repeat = mode == RoomTemplateRepeatMode::NoRepeat;
            const bool no_immediate =
                mode == RoomTemplateRepeatMode::NoImmediate && immediate_neighbors(room_index, static_cast<int>(j));
            if (no_repeat || no_immediate) {
                const auto group_it = aliases_by_template_name_.find(template_name);
                if (group_it != aliases_by_template_name_.end()) {
                    excluded.insert(group_it->second.begin(), group_it->second.end());
                }
            }
        }
        return selections_from(weighted_shapes_by_room_[static_cast<std::size_t>(room_index)], excluded);
    }

    /// C# NoImmediate adjacency is checked on the contracted stage-one graph: rooms connected
    /// directly or through a stage-two corridor count as immediate neighbors.
    bool immediate_neighbors(int a, int b) const {
        const TRoom& ra = mapping_.room_id(a);
        const TRoom& rb = mapping_.room_id(b);
        if (full_graph_.has_edge(ra, rb)) {
            return true;
        }
        for (const TRoom& n : full_graph_.neighbours(ra)) {
            const auto& desc = level_.get_room_description(n);
            if (desc.is_corridor() && desc.stage() == 2 && full_graph_.has_edge(n, rb)) {
                return true;
            }
        }
        return false;
    }


    void build_alias_table() {
        int next_alias = 0;
        weighted_shapes_by_room_.resize(mapping_.index_to_room.size());
        for (std::size_t room_index = 0; room_index < mapping_.index_to_room.size(); ++room_index) {
            const auto& room = mapping_.index_to_room[room_index];
            const auto& templates = level_.get_room_description(room).room_templates();
            auto& out = weighted_shapes_by_room_[room_index];
            for (const auto& tmpl : templates) {
                std::vector<geometry::TransformationGrid2D> transforms = tmpl.allowed_transformations();
                if (transforms.empty()) {
                    transforms.push_back(geometry::TransformationGrid2D::Identity);
                }
                const double per_instance_weight = 1.0 / static_cast<double>(transforms.size());
                for (const auto tr : transforms) {
                    const auto key = std::make_tuple(tmpl.name(), static_cast<int>(tr));
                    auto it = alias_by_template_transform_.find(key);
                    int alias = -1;
                    if (it == alias_by_template_transform_.end()) {
                        alias = next_alias++;
                        alias_by_template_transform_[key] = alias;
                        alias_template_name_[alias] = tmpl.name();
                        aliases_by_template_name_[tmpl.name()].push_back(alias);
                        repeat_mode_by_template_name_[tmpl.name()] = tmpl.repeat_mode();
                    } else {
                        alias = it->second;
                    }
                    out.push_back(WeightedShapeInstance{
                        .room_template = tmpl,
                        .transformation = tr,
                        .shape_alias = alias,
                        .weight = per_instance_weight,
                    });
                }
            }
        }
    }

    RoomTemplateRepeatMode effective_repeat_mode(int room_index) const {
        RoomTemplateRepeatMode mode = RoomTemplateRepeatMode::AllowRepeat;
        if (level_.room_template_repeat_mode_default.has_value()) {
            mode = *level_.room_template_repeat_mode_default;
        }
        if (level_.room_template_repeat_mode_override.has_value()) {
            mode = *level_.room_template_repeat_mode_override;
        }
        const auto& room = mapping_.index_to_room[static_cast<std::size_t>(room_index)];
        const auto& templates = level_.get_room_description(room).room_templates();
        if (templates.empty()) {
            return mode;
        }
        if (templates.front().repeat_mode().has_value()) {
            return *templates.front().repeat_mode();
        }
        return mode;
    }

    bool is_allowed_by_repeat_mode(RoomTemplateRepeatMode mode, int alias,
                                   const std::vector<std::optional<RoomTemplateGrid2D>>* placed_templates,
                                   const std::vector<geometry::TransformationGrid2D>* placed_transforms,
                                   std::optional<int> previous_alias) const {
        if (mode == RoomTemplateRepeatMode::AllowRepeat) {
            return true;
        }
        if (mode == RoomTemplateRepeatMode::NoImmediate && previous_alias.has_value() && *previous_alias == alias) {
            return false;
        }
        if (mode == RoomTemplateRepeatMode::NoRepeat && placed_templates != nullptr && placed_transforms != nullptr) {
            std::set<int> used_aliases;
            for (std::size_t i = 0; i < placed_templates->size(); ++i) {
                if (!(*placed_templates)[i].has_value()) {
                    continue;
                }
                used_aliases.insert(alias_for(*(*placed_templates)[i], (*placed_transforms)[i]));
            }
            if (used_aliases.count(alias) != 0) {
                return false;
            }
        }
        return true;
    }
};

} // namespace edgar::generator::grid2d
