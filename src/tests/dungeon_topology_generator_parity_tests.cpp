#include <gtest/gtest.h>

#include "dungeon_topology_generator/geometry/polygon_grid2d.hpp"
#include "dungeon_topology_generator/geometry/vector2_int.hpp"
#include "dungeon_topology_generator/geometry/transformation_grid2d.hpp"
#include "dungeon_topology_generator/geometry/rectangle_grid2d.hpp"
#include "dungeon_topology_generator/geometry/orthogonal_line_grid2d.hpp"
#include "dungeon_topology_generator/geometry/orthogonal_line_intersection.hpp"
#include "dungeon_topology_generator/geometry/overlap.hpp"
#include "dungeon_topology_generator/geometry/polygon_overlap_grid2d.hpp"
#include "dungeon_topology_generator/geometry/clipper2_util.hpp"
#include "dungeon_topology_generator/graphs/undirected_graph.hpp"
#include "dungeon_topology_generator/graphs/graph_algorithms.hpp"
#include "dungeon_topology_generator/graphs/planar_faces.hpp"
#include "dungeon_topology_generator/geometry/bipartite_matching.hpp"
#include "dungeon_topology_generator/generator/grid2d/level_description_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/room_template_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/manual_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/simple_door_mode_grid2d.hpp"
#include "dungeon_topology_generator/generator/grid2d/door_utils.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_spaces_generator.hpp"
#include "dungeon_topology_generator/generator/grid2d/configuration_spaces_grid2d.hpp"

#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using namespace dungeon_topology_generator;
using namespace dungeon_topology_generator::geometry;
using namespace dungeon_topology_generator::graphs;
using namespace dungeon_topology_generator::generator::grid2d;

TEST(DungeonTopologyGeneratorGeometry, PolygonGetSquare_CorrectPointsAndWidth) {
    auto sq = PolygonGrid2D::get_square(8);
    EXPECT_EQ(sq.points().size(), 4u);
    auto br = sq.bounding_rectangle();
    EXPECT_EQ(br.width(), 8);
    EXPECT_EQ(br.height(), 8);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonGetRectangle_CorrectPointsAndWidth) {
    auto rect = PolygonGrid2D::get_rectangle(6, 10);
    EXPECT_EQ(rect.points().size(), 4u);
    auto br = rect.bounding_rectangle();
    EXPECT_EQ(br.width(), 6);
    EXPECT_EQ(br.height(), 10);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonBoundingRectangle_Rectangle) {
    auto rect = PolygonGrid2D::get_rectangle(2, 4);
    auto br = rect.bounding_rectangle();
    EXPECT_EQ(br.a, Vector2Int(0, 0));
    EXPECT_EQ(br.b, Vector2Int(2, 4));
}

TEST(DungeonTopologyGeneratorGeometry, PolygonBoundingRectangle_LShape) {
    auto lshape = PolygonGrid2DBuilder()
        .add_point(0, 0)
        .add_point(0, 6)
        .add_point(3, 6)
        .add_point(3, 3)
        .add_point(7, 3)
        .add_point(7, 0)
        .build();
    auto br = lshape.bounding_rectangle();
    EXPECT_EQ(br.a, Vector2Int(0, 0));
    EXPECT_EQ(br.b, Vector2Int(7, 6));
}

TEST(DungeonTopologyGeneratorGeometry, PolygonRotate_Square180) {
    auto sq = PolygonGrid2D::get_square(4);
    auto rotated = sq.rotate(180);
    std::vector<Vector2Int> expected = {{0, 0}, {0, -4}, {-4, -4}, {-4, 0}};
    ASSERT_EQ(rotated.points().size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(rotated.points()[i], expected[i]);
    }
}

TEST(DungeonTopologyGeneratorGeometry, PolygonRotate_Rectangle270) {
    auto rect = PolygonGrid2D::get_rectangle(2, 5);
    auto rotated = rect.rotate(270);
    std::vector<Vector2Int> expected = {{0, 0}, {-5, 0}, {-5, 2}, {0, 2}};
    ASSERT_EQ(rotated.points().size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(rotated.points()[i], expected[i]);
    }
}

TEST(DungeonTopologyGeneratorGeometry, PolygonTransform_AllEight) {
    auto poly = PolygonGrid2D::get_rectangle(2, 1);
    auto transforms = poly.get_all_transformations();
    ASSERT_EQ(transforms.size(), 8u);
    EXPECT_EQ(transforms[0].points()[0], Vector2Int(0, 0));
    EXPECT_EQ(transforms[0].points()[1], Vector2Int(0, 1));
    EXPECT_EQ(transforms[0].points()[2], Vector2Int(2, 1));
    EXPECT_EQ(transforms[0].points()[3], Vector2Int(2, 0));
    EXPECT_EQ(transforms[1].points()[0], Vector2Int(0, 0));
    EXPECT_EQ(transforms[1].points()[1], Vector2Int(1, 0));
    EXPECT_EQ(transforms[1].points()[2], Vector2Int(1, -2));
    EXPECT_EQ(transforms[1].points()[3], Vector2Int(0, -2));
}

TEST(DungeonTopologyGeneratorGeometry, PolygonPlus_Translate) {
    auto sq = PolygonGrid2D::get_square(3);
    auto offset = Vector2Int(2, 3);
    auto translated = sq + offset;
    for (std::size_t i = 0; i < sq.points().size(); ++i) {
        EXPECT_EQ(translated.points()[i], sq.points()[i] + offset);
    }
}

TEST(DungeonTopologyGeneratorGeometry, PolygonScale_ReturnsScaled) {
    auto sq = PolygonGrid2D::get_square(3);
    auto scaled = sq.scale(Vector2Int(2, 3));
    auto expected = PolygonGrid2D::get_rectangle(6, 9);
    EXPECT_EQ(scaled, expected);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonConstructor_TooFewPoints) {
    EXPECT_THROW(PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 5).build(), std::invalid_argument);
    EXPECT_THROW(PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 5).add_point(5, 5).build(), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonConstructor_ValidPolygons) {
    EXPECT_NO_THROW(PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 3).add_point(3, 3).add_point(3, 0).build());
    EXPECT_NO_THROW(PolygonGrid2DBuilder()
        .add_point(0, 0).add_point(0, 6).add_point(3, 6).add_point(3, 3).add_point(6, 3).add_point(6, 0).build());
}

TEST(DungeonTopologyGeneratorGeometry, PolygonConstructor_CounterClockwise) {
    EXPECT_THROW(PolygonGrid2DBuilder().add_point(3, 0).add_point(3, 3).add_point(0, 3).add_point(0, 0).build(),
                 std::invalid_argument);
    EXPECT_THROW(PolygonGrid2DBuilder()
        .add_point(6, 0).add_point(6, 3).add_point(3, 3).add_point(3, 6).add_point(0, 6).add_point(0, 0).build(),
                 std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonConstructor_EdgesNotOrthogonal) {
    EXPECT_THROW(PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 5).add_point(3, 4).add_point(4, 0).build(),
                 std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonEquals_SameOrder_ReturnTrue) {
    auto a = PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 3).add_point(3, 3).add_point(3, 0).build();
    auto b = PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 3).add_point(3, 3).add_point(3, 0).build();
    EXPECT_TRUE(a == b);
}

TEST(DungeonTopologyGeneratorGeometry, PolygonEquals_DifferentOrder_ReturnFalse) {
    auto a = PolygonGrid2DBuilder().add_point(0, 0).add_point(0, 3).add_point(3, 3).add_point(3, 0).build();
    auto b = PolygonGrid2DBuilder().add_point(3, 0).add_point(0, 0).add_point(0, 3).add_point(3, 3).build();
    EXPECT_FALSE(a == b);
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_IdenticalSquares_ReturnsTrue) {
    auto sq = PolygonGrid2D::get_square(2);
    EXPECT_TRUE(polygons_overlap_area(sq, Vector2Int(0, 0), sq, Vector2Int(0, 0)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_OverlappingSquares_ReturnsTrue) {
    auto sq = PolygonGrid2D::get_square(2);
    EXPECT_TRUE(polygons_overlap_area(sq, Vector2Int(0, 0), sq, Vector2Int(1, 0)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_NestedRectangles_ReturnsTrue) {
    auto outer = PolygonGrid2D::get_rectangle(4, 5);
    auto inner = PolygonGrid2D::get_rectangle(2, 3);
    EXPECT_TRUE(polygons_overlap_area(outer, Vector2Int(0, 0), inner, Vector2Int(1, 1)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_NonOverlappingSquares_ReturnsFalse) {
    auto sq = PolygonGrid2D::get_square(2);
    EXPECT_FALSE(polygons_overlap_area(sq, Vector2Int(0, 0), sq, Vector2Int(6, 0)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_TouchingSquaresSide_ReturnsFalse) {
    auto sq = PolygonGrid2D::get_square(2);
    EXPECT_FALSE(polygons_overlap_area(sq, Vector2Int(0, 0), sq, Vector2Int(0, 2)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_TouchingRectanglesCorner_ReturnsFalse) {
    auto r1 = PolygonGrid2D::get_rectangle(6, 3);
    auto r2 = PolygonGrid2D::get_rectangle(4, 3);
    EXPECT_FALSE(polygons_overlap_area(r1, Vector2Int(0, 0), r2, Vector2Int(6, 3)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_LShapeVsSquare_Overlapping_ReturnsTrue) {
    auto p1 = PolygonGrid2DBuilder()
        .add_point(0, 0).add_point(0, 6).add_point(3, 6)
        .add_point(3, 3).add_point(6, 3).add_point(6, 0).build();
    auto p2 = PolygonGrid2D::get_square(3);
    EXPECT_TRUE(polygons_overlap_area(p1, Vector2Int(0, 0), p2, Vector2Int(3, 0)));
}

TEST(DungeonTopologyGeneratorGeometry, Overlap_RotatedLShapeVsSquare_NonOverlapping_ReturnsFalse) {
    auto p1 = PolygonGrid2DBuilder()
        .add_point(0, 0).add_point(0, 6).add_point(3, 6)
        .add_point(3, 3).add_point(6, 3).add_point(6, 0).build();
    auto p1_rotated = p1.rotate(90);
    auto p2 = PolygonGrid2D::get_square(3);
    EXPECT_FALSE(polygons_overlap_area(p1_rotated, Vector2Int(0, 0), p2, Vector2Int(0, 0)));
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_OverlapStart) {
    auto p1 = PolygonGrid2D::get_square(5);
    auto p2 = PolygonGrid2D::get_rectangle(2, 3);
    auto line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 10));
    auto result = overlap_along_line(p1, p2, line);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].first, Vector2Int(0, 0));
    EXPECT_TRUE(result[0].second);
    EXPECT_EQ(result[1].first, Vector2Int(0, 3));
    EXPECT_FALSE(result[1].second);
}

TEST(DungeonTopologyGeneratorGraphs, AddVertexDuplicate_Throws) {
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0);
    EXPECT_THROW(g.add_vertex(0), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGraphs, AddEdgeDuplicate_Throws) {
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0);
    g.add_vertex(1);
    g.add_edge(0, 1);
    EXPECT_THROW(g.add_edge(0, 1), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGraphs, AddEdgeNonExistingVertex_Throws) {
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0);
    EXPECT_THROW(g.add_edge(0, 1), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorGraphs, HasEdge_Correct) {
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0);
    g.add_vertex(1);
    g.add_vertex(2);
    g.add_edge(0, 1);
    g.add_edge(1, 2);
    EXPECT_TRUE(g.has_edge(0, 1));
    EXPECT_TRUE(g.has_edge(1, 0));
    EXPECT_TRUE(g.has_edge(1, 2));
    EXPECT_FALSE(g.has_edge(0, 2));
}

TEST(DungeonTopologyGeneratorGraphs, VertexAndEdgeCount) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 5; ++i) g.add_vertex(i);
    for (int i = 0; i < 4; ++i) g.add_edge(i, i + 1);
    EXPECT_EQ(g.vertex_count(), 5u);
}

TEST(DungeonTopologyGeneratorGraphs, IsConnected_C3) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 3; ++i) g.add_vertex(i);
    g.add_edge(0, 1);
    g.add_edge(1, 2);
    g.add_edge(2, 0);
    EXPECT_TRUE(is_connected(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsConnected_TwoC3sByEdge) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 6; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    g.add_edge(3, 4); g.add_edge(4, 5); g.add_edge(5, 3);
    g.add_edge(0, 3);
    EXPECT_TRUE(is_connected(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsConnected_NotConnected_TwoC3s) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 6; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    g.add_edge(3, 4); g.add_edge(4, 5); g.add_edge(5, 3);
    EXPECT_FALSE(is_connected(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsConnected_NoEdges) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 20; ++i) g.add_vertex(i);
    EXPECT_FALSE(is_connected(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsTree_Linear) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 4; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 3);
    EXPECT_TRUE(is_tree(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsTree_Cycle_NotTree) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 3; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    EXPECT_FALSE(is_tree(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsTree_Disconnected_NotTree) {
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 4; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(2, 3);
    EXPECT_FALSE(is_tree(g));
}

TEST(DungeonTopologyGeneratorGeometry, LineIntersection_NoIntersection_DifferentY) {
    OrthogonalLineGrid2D line1(Vector2Int(1, 1), Vector2Int(5, 1));
    OrthogonalLineGrid2D line2(Vector2Int(1, 2), Vector2Int(5, 2));
    OrthogonalLineGrid2D result;
    EXPECT_FALSE(OrthogonalLineIntersection::try_get_intersection(line1, line2, result));
}

TEST(DungeonTopologyGeneratorGeometry, LineIntersection_PointIntersection) {
    OrthogonalLineGrid2D line1(Vector2Int(1, 1), Vector2Int(5, 1));
    OrthogonalLineGrid2D line2(Vector2Int(5, 1), Vector2Int(10, 1));
    OrthogonalLineGrid2D result;
    EXPECT_TRUE(OrthogonalLineIntersection::try_get_intersection(line1, line2, result));
    auto rn = result.normalized();
    EXPECT_EQ(rn.from, Vector2Int(5, 1));
    EXPECT_EQ(rn.to, Vector2Int(5, 1));
}

TEST(DungeonTopologyGeneratorGeometry, LineIntersection_LineIntersection) {
    OrthogonalLineGrid2D line1(Vector2Int(3, 2), Vector2Int(10, 2));
    OrthogonalLineGrid2D line2(Vector2Int(7, 2), Vector2Int(13, 2));
    OrthogonalLineGrid2D result;
    EXPECT_TRUE(OrthogonalLineIntersection::try_get_intersection(line1, line2, result));
    auto rn = result.normalized();
    EXPECT_EQ(rn.from, Vector2Int(7, 2));
    EXPECT_EQ(rn.to, Vector2Int(10, 2));
}

TEST(DungeonTopologyGeneratorGeometry, LineIntersection_PerpendicularPoint) {
    OrthogonalLineGrid2D line1(Vector2Int(1, 1), Vector2Int(5, 1));
    OrthogonalLineGrid2D line2(Vector2Int(3, -2), Vector2Int(3, 5));
    OrthogonalLineGrid2D result;
    EXPECT_TRUE(OrthogonalLineIntersection::try_get_intersection(line1, line2, result));
    EXPECT_EQ(result.from, Vector2Int(3, 1));
    EXPECT_EQ(result.to, Vector2Int(3, 1));
}

TEST(DungeonTopologyGeneratorGeometry, PartitionByIntersection_OnePoint) {
    OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(10, 0));
    std::vector<OrthogonalLineGrid2D> intersection = {OrthogonalLineGrid2D(Vector2Int(5, 0), Vector2Int(5, 0))};
    auto partitions = OrthogonalLineIntersection::partition_by_intersection(line, intersection);
    ASSERT_EQ(partitions.size(), 2u);
    EXPECT_EQ(partitions[0].from, Vector2Int(0, 0));
    EXPECT_EQ(partitions[0].to, Vector2Int(4, 0));
    EXPECT_EQ(partitions[1].from, Vector2Int(6, 0));
    EXPECT_EQ(partitions[1].to, Vector2Int(10, 0));
}

TEST(DungeonTopologyGeneratorGeometry, PartitionByIntersection_LineSegment) {
    OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(10, 0));
    std::vector<OrthogonalLineGrid2D> intersection = {OrthogonalLineGrid2D(Vector2Int(4, 0), Vector2Int(6, 0))};
    auto partitions = OrthogonalLineIntersection::partition_by_intersection(line, intersection);
    ASSERT_EQ(partitions.size(), 2u);
    EXPECT_EQ(partitions[0].from, Vector2Int(0, 0));
    EXPECT_EQ(partitions[0].to, Vector2Int(3, 0));
    EXPECT_EQ(partitions[1].from, Vector2Int(7, 0));
    EXPECT_EQ(partitions[1].to, Vector2Int(10, 0));
}

TEST(DungeonTopologyGeneratorGeometry, RemoveIntersections_OneLine) {
    std::vector<OrthogonalLineGrid2D> lines = {OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(10, 0))};
    auto result = OrthogonalLineIntersection::remove_intersections(lines);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].from, lines[0].from);
    EXPECT_EQ(result[0].to, lines[0].to);
}

TEST(DungeonTopologyGeneratorGeometry, RemoveIntersections_MultipleLines) {
    std::vector<OrthogonalLineGrid2D> lines = {
        OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(10, 0)),
        OrthogonalLineGrid2D(Vector2Int(0, 5), Vector2Int(10, 5)),
        OrthogonalLineGrid2D(Vector2Int(2, -5), Vector2Int(2, 10)),
        OrthogonalLineGrid2D(Vector2Int(7, -5), Vector2Int(7, 10)),
    };
    auto result = OrthogonalLineIntersection::remove_intersections(lines);
    std::set<Vector2Int> result_pts;
    for (const auto& l : result) {
        auto pts = l.grid_points_inclusive();
        result_pts.insert(pts.begin(), pts.end());
    }
    std::set<Vector2Int> expected_pts;
    for (const auto& l : lines) {
        auto pts = l.grid_points_inclusive();
        expected_pts.insert(pts.begin(), pts.end());
    }
    EXPECT_EQ(result_pts, expected_pts);
}

TEST(DungeonTopologyGeneratorLevelDescription, AddRoomDuplicate_Throws) {
    auto sq = RoomTemplateGrid2D(PolygonGrid2D::get_square(8),
                                 std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    RoomDescriptionGrid2D room_desc(false, {sq});
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, room_desc);
    EXPECT_THROW(level.add_room(0, room_desc), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorLevelDescription, AddConnectionDuplicate_Throws) {
    auto sq = RoomTemplateGrid2D(PolygonGrid2D::get_square(8),
                                 std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    RoomDescriptionGrid2D room_desc(false, {sq});
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, room_desc);
    level.add_room(1, room_desc);
    level.add_connection(0, 1);
    EXPECT_THROW(level.add_connection(0, 1), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorLevelDescription, AddConnectionNonExistingRoom_Throws) {
    auto sq = RoomTemplateGrid2D(PolygonGrid2D::get_square(8),
                                 std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    RoomDescriptionGrid2D room_desc(false, {sq});
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, room_desc);
    EXPECT_THROW(level.add_connection(0, 1), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorLevelDescription, CorridorWithTooManyNeighbors_Throws) {
    auto corridor_tmpl = RoomTemplateGrid2D(PolygonGrid2D::get_square(3),
                                            std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    auto sq_tmpl = RoomTemplateGrid2D(PolygonGrid2D::get_square(8),
                                      std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, RoomDescriptionGrid2D(true, {corridor_tmpl}));
    level.add_room(1, RoomDescriptionGrid2D(false, {sq_tmpl}));
    level.add_room(2, RoomDescriptionGrid2D(false, {sq_tmpl}));
    level.add_room(3, RoomDescriptionGrid2D(false, {sq_tmpl}));
    level.add_connection(0, 1);
    level.add_connection(0, 2);
    level.add_connection(0, 3);
    EXPECT_THROW(level.get_graph(), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorLevelDescription, TwoNeighboringCorridors_Throws) {
    auto corridor_tmpl = RoomTemplateGrid2D(PolygonGrid2D::get_square(3),
                                            std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, RoomDescriptionGrid2D(true, {corridor_tmpl}));
    level.add_room(1, RoomDescriptionGrid2D(true, {corridor_tmpl}));
    level.add_connection(0, 1);
    EXPECT_THROW(level.get_graph(), std::invalid_argument);
}

TEST(DungeonTopologyGeneratorLevelDescription, OnlyBasicRooms_GraphBuilt) {
    auto sq = RoomTemplateGrid2D(PolygonGrid2D::get_square(8),
                                 std::make_shared<SimpleDoorModeGrid2D>(1, 1));
    RoomDescriptionGrid2D room_desc(false, {sq});
    LevelDescriptionGrid2D<int> level;
    level.add_room(0, room_desc);
    level.add_room(1, room_desc);
    level.add_room(2, room_desc);
    level.add_room(3, room_desc);
    level.add_connection(0, 1);
    level.add_connection(1, 2);
    level.add_connection(2, 3);
    auto g = level.get_graph();
    EXPECT_EQ(g.vertex_count(), 4u);
}

// ===========================================================================
// Этап F: Door tests (SimpleDoorMode, MergeDoorLines)
// ===========================================================================

static std::vector<DoorLineGrid2D> sorted_by_from(const std::vector<DoorLineGrid2D>& doors) {
    auto copy = doors;
    std::sort(copy.begin(), copy.end(), [](const DoorLineGrid2D& a, const DoorLineGrid2D& b) {
        if (a.line.from != b.line.from) return a.line.from < b.line.from;
        if (a.line.to != b.line.to) return a.line.to < b.line.to;
        return static_cast<int>(a.get_direction()) < static_cast<int>(b.get_direction());
    });
    return copy;
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_Length1_NoOverlap) {
    auto rect = PolygonGrid2D::get_rectangle(3, 5);
    SimpleDoorModeGrid2D mode(1, 0);
    auto doors = mode.get_doors(rect);

    ASSERT_EQ(doors.size(), 4u);
    for (const auto& d : doors) {
        EXPECT_EQ(d.length, 1);
    }

    auto s = sorted_by_from(doors);
    EXPECT_EQ(s[0].line.from, Vector2Int(0, 0));
    EXPECT_EQ(s[0].line.to, Vector2Int(0, 4));
    EXPECT_EQ(s[1].line.from, Vector2Int(0, 5));
    EXPECT_EQ(s[1].line.to, Vector2Int(2, 5));
    EXPECT_EQ(s[2].line.from, Vector2Int(3, 0));
    EXPECT_EQ(s[2].line.to, Vector2Int(1, 0));
    EXPECT_EQ(s[3].line.from, Vector2Int(3, 5));
    EXPECT_EQ(s[3].line.to, Vector2Int(3, 1));
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_Length1_OneOverlap) {
    auto rect = PolygonGrid2D::get_rectangle(3, 5);
    SimpleDoorModeGrid2D mode(1, 1);
    auto doors = mode.get_doors(rect);

    ASSERT_EQ(doors.size(), 4u);

    auto s = sorted_by_from(doors);
    EXPECT_EQ(s[0].line.from, Vector2Int(0, 1));
    EXPECT_EQ(s[0].line.to, Vector2Int(0, 3));
    EXPECT_EQ(s[1].line.from, Vector2Int(1, 5));
    EXPECT_EQ(s[1].line.to, Vector2Int(1, 5));
    EXPECT_EQ(s[2].line.from, Vector2Int(2, 0));
    EXPECT_EQ(s[2].line.to, Vector2Int(2, 0));
    EXPECT_EQ(s[3].line.from, Vector2Int(3, 4));
    EXPECT_EQ(s[3].line.to, Vector2Int(3, 2));
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_Length1_TwoOverlap) {
    auto rect = PolygonGrid2D::get_rectangle(3, 5);
    SimpleDoorModeGrid2D mode(1, 2);
    auto doors = mode.get_doors(rect);

    ASSERT_EQ(doors.size(), 2u);
    auto s = sorted_by_from(doors);
    EXPECT_EQ(s[0].line.from, Vector2Int(0, 2));
    EXPECT_EQ(s[0].line.to, Vector2Int(0, 2));
    EXPECT_EQ(s[1].line.from, Vector2Int(3, 3));
    EXPECT_EQ(s[1].line.to, Vector2Int(3, 3));
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_Length2_NoOverlap) {
    auto rect = PolygonGrid2D::get_rectangle(3, 5);
    SimpleDoorModeGrid2D mode(2, 0);
    auto doors = mode.get_doors(rect);

    ASSERT_EQ(doors.size(), 4u);
    for (const auto& d : doors) {
        EXPECT_EQ(d.length, 2);
    }

    auto s = sorted_by_from(doors);
    EXPECT_EQ(s[0].line.from, Vector2Int(0, 0));
    EXPECT_EQ(s[0].line.to, Vector2Int(0, 3));
    EXPECT_EQ(s[1].line.from, Vector2Int(0, 5));
    EXPECT_EQ(s[1].line.to, Vector2Int(1, 5));
    EXPECT_EQ(s[2].line.from, Vector2Int(3, 0));
    EXPECT_EQ(s[2].line.to, Vector2Int(2, 0));
    EXPECT_EQ(s[3].line.from, Vector2Int(3, 5));
    EXPECT_EQ(s[3].line.to, Vector2Int(3, 2));
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_InvalidArgs) {
    EXPECT_THROW(SimpleDoorModeGrid2D(-1, 0), std::invalid_argument);
    EXPECT_THROW(SimpleDoorModeGrid2D(0, -1), std::invalid_argument);
    EXPECT_THROW(SimpleDoorModeGrid2D(1, -1), std::invalid_argument);
    EXPECT_NO_THROW(SimpleDoorModeGrid2D(0, 0));
    EXPECT_NO_THROW(SimpleDoorModeGrid2D(1, 0));
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_LengthZero) {
    auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    SimpleDoorModeGrid2D mode(0, 0);
    auto doors = mode.get_doors(polygon);
    auto expected = std::vector<DoorLineGrid2D>{
        {OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 5)), 0},
        {OrthogonalLineGrid2D(Vector2Int(0, 5), Vector2Int(3, 5)), 0},
        {OrthogonalLineGrid2D(Vector2Int(3, 5), Vector2Int(3, 0)), 0},
        {OrthogonalLineGrid2D(Vector2Int(3, 0), Vector2Int(0, 0)), 0},
    };

    auto actual_sorted = sorted_by_from(doors);
    auto expected_sorted = sorted_by_from(expected);
    ASSERT_EQ(actual_sorted.size(), expected_sorted.size());
    for (size_t i = 0; i < actual_sorted.size(); ++i) {
        EXPECT_EQ(actual_sorted[i].line.from, expected_sorted[i].line.from);
        EXPECT_EQ(actual_sorted[i].line.to, expected_sorted[i].line.to);
        EXPECT_EQ(actual_sorted[i].length, expected_sorted[i].length);
    }
}

TEST(DungeonTopologyGeneratorDoors, MergeDoorLines_CorrectlyMerges) {
    std::vector<DoorLineGrid2D> input = {
        {OrthogonalLineGrid2D(Vector2Int(1, 0), Vector2Int(2, 0)), 2},
        {OrthogonalLineGrid2D(Vector2Int(3, 0), Vector2Int(5, 0)), 2},
        {OrthogonalLineGrid2D(Vector2Int(-3, 0), Vector2Int(0, 0)), 1},
        {OrthogonalLineGrid2D(Vector2Int(-3, 0), Vector2Int(0, 0)), 2},
        {OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 3)), 2},
        {OrthogonalLineGrid2D(Vector2Int(0, -2), Vector2Int(0, -1)), 2},
    };

    auto merged = merge_door_lines(std::move(input));

    std::vector<DoorLineGrid2D> expected = {
        {OrthogonalLineGrid2D(Vector2Int(-3, 0), Vector2Int(0, 0)), 1},
        {OrthogonalLineGrid2D(Vector2Int(-3, 0), Vector2Int(5, 0)), 2},
        {OrthogonalLineGrid2D(Vector2Int(0, -2), Vector2Int(0, 3)), 2},
    };

    auto ms = sorted_by_from(merged);
    auto es = sorted_by_from(expected);
    ASSERT_EQ(ms.size(), es.size());
    for (size_t i = 0; i < ms.size(); ++i) {
        EXPECT_EQ(ms[i].line.from, es[i].line.from) << "at index " << i;
        EXPECT_EQ(ms[i].line.to, es[i].line.to) << "at index " << i;
        EXPECT_EQ(ms[i].length, es[i].length) << "at index " << i;
    }
}

TEST(DungeonTopologyGeneratorDoors, MergeDoorLines_DifferentSocketsDoNotMerge) {
    auto socket_a = std::make_shared<int>(1);
    auto socket_b = std::make_shared<int>(2);
    std::vector<DoorLineGrid2D> input = {
        DoorLineGrid2D{
            .line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(1, 0)),
            .length = 1,
            .socket = socket_a,
        },
        DoorLineGrid2D{
            .line = OrthogonalLineGrid2D(Vector2Int(2, 0), Vector2Int(3, 0)),
            .length = 1,
            .socket = socket_b,
        },
    };

    auto merged = merge_door_lines(std::move(input));
    EXPECT_EQ(merged.size(), 2u);
}

TEST(DungeonTopologyGeneratorDoors, ManualDoorMode_LengthZeroCorners) {
    auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    ManualDoorModeGrid2D mode({
        DoorGrid2D{.from = Vector2Int(0, 0), .to = Vector2Int(0, 0)},
        DoorGrid2D{.from = Vector2Int(0, 5), .to = Vector2Int(0, 5)},
        DoorGrid2D{.from = Vector2Int(3, 5), .to = Vector2Int(3, 5)},
        DoorGrid2D{.from = Vector2Int(3, 0), .to = Vector2Int(3, 0)},
    });
    auto doors = mode.get_doors(polygon);
    auto sorted = sorted_by_from(doors);
    ASSERT_EQ(sorted.size(), 8u);
    EXPECT_EQ(sorted.front().line.from, Vector2Int(0, 0));
    EXPECT_EQ(sorted.back().line.from, Vector2Int(3, 5));
}

TEST(DungeonTopologyGeneratorDoors, ManualDoorMode_LengthZeroInside) {
    auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    ManualDoorModeGrid2D mode({
        DoorGrid2D{.from = Vector2Int(0, 1), .to = Vector2Int(0, 1)},
        DoorGrid2D{.from = Vector2Int(1, 5), .to = Vector2Int(1, 5)},
        DoorGrid2D{.from = Vector2Int(3, 4), .to = Vector2Int(3, 4)},
        DoorGrid2D{.from = Vector2Int(2, 0), .to = Vector2Int(2, 0)},
    });
    auto doors = sorted_by_from(mode.get_doors(polygon));
    ASSERT_EQ(doors.size(), 4u);
    EXPECT_EQ(doors[0].line.from, Vector2Int(0, 1));
    EXPECT_EQ(doors[0].get_direction(), OrthogonalDirection::Top);
    EXPECT_EQ(doors[1].line.from, Vector2Int(1, 5));
    EXPECT_EQ(doors[1].get_direction(), OrthogonalDirection::Right);
    EXPECT_EQ(doors[2].line.from, Vector2Int(2, 0));
    EXPECT_EQ(doors[2].get_direction(), OrthogonalDirection::Left);
    EXPECT_EQ(doors[3].line.from, Vector2Int(3, 4));
    EXPECT_EQ(doors[3].get_direction(), OrthogonalDirection::Bottom);
}

TEST(DungeonTopologyGeneratorDoors, ConfigurationSpaceBetween_RespectsDoorSocket) {
    const auto square = PolygonGrid2D::get_square(4);
    auto socket_a = std::make_shared<int>(1);
    auto socket_b = std::make_shared<int>(2);
    const std::vector<DoorLineGrid2D> moving = {
        DoorLineGrid2D{
            .line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 3)),
            .length = 1,
            .socket = socket_a,
        }
    };
    const std::vector<DoorLineGrid2D> fixed = {
        DoorLineGrid2D{
            .line = OrthogonalLineGrid2D(Vector2Int(4, 0), Vector2Int(4, 3)),
            .length = 1,
            .socket = socket_b,
        }
    };
    const auto cs = ConfigurationSpacesGrid2D::configuration_space_between(square, moving, square, fixed);
    EXPECT_TRUE(cs.lines.empty());
}

TEST(DungeonTopologyGeneratorDoors, SimpleDoorMode_Square10_Symmetric) {
    auto sq = PolygonGrid2D::get_square(10);
    SimpleDoorModeGrid2D mode(1, 0);
    auto doors = mode.get_doors(sq);
    EXPECT_EQ(doors.size(), 4u);
    for (const auto& d : doors) {
        EXPECT_EQ(d.length, 1);
    }
}

// ===========================================================================
// Этап H: Utility tests (Vector2Int transform, OrthogonalLine)
// ===========================================================================

TEST(DungeonTopologyGeneratorUtils, Vector2Int_Transform_All8) {
    Vector2Int v(2, 3);

    EXPECT_EQ(v.transform(TransformationGrid2D::Identity), Vector2Int(2, 3));
    EXPECT_EQ(v.transform(TransformationGrid2D::Rotate90), Vector2Int(3, -2));
    EXPECT_EQ(v.transform(TransformationGrid2D::Rotate180), Vector2Int(-2, -3));
    EXPECT_EQ(v.transform(TransformationGrid2D::Rotate270), Vector2Int(-3, 2));
    EXPECT_EQ(v.transform(TransformationGrid2D::MirrorX), Vector2Int(2, -3));
    EXPECT_EQ(v.transform(TransformationGrid2D::MirrorY), Vector2Int(-2, 3));
    EXPECT_EQ(v.transform(TransformationGrid2D::Diagonal13), Vector2Int(3, 2));
    EXPECT_EQ(v.transform(TransformationGrid2D::Diagonal24), Vector2Int(-3, -2));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_Direction) {
    EXPECT_EQ(OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(3, 0)).get_direction(), OrthogonalDirection::Right);
    EXPECT_EQ(OrthogonalLineGrid2D(Vector2Int(3, 0), Vector2Int(0, 0)).get_direction(), OrthogonalDirection::Left);
    EXPECT_EQ(OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 3)).get_direction(), OrthogonalDirection::Top);
    EXPECT_EQ(OrthogonalLineGrid2D(Vector2Int(0, 3), Vector2Int(0, 0)).get_direction(), OrthogonalDirection::Bottom);
    EXPECT_EQ(OrthogonalLineGrid2D(Vector2Int(1, 1), Vector2Int(1, 1)).get_direction(), OrthogonalDirection::Undefined);
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_GridPointsInclusive) {
    auto pts1 = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 3)).grid_points_inclusive();
    ASSERT_EQ(pts1.size(), 4u);
    EXPECT_EQ(pts1[0], Vector2Int(0, 0));
    EXPECT_EQ(pts1[3], Vector2Int(0, 3));

    auto pts2 = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(3, 0)).grid_points_inclusive();
    ASSERT_EQ(pts2.size(), 4u);
    EXPECT_EQ(pts2[0], Vector2Int(0, 0));
    EXPECT_EQ(pts2[3], Vector2Int(3, 0));

    auto pts3 = OrthogonalLineGrid2D(Vector2Int(3, 0), Vector2Int(0, 0)).grid_points_inclusive();
    ASSERT_EQ(pts3.size(), 4u);
    EXPECT_EQ(pts3[0], Vector2Int(3, 0));
    EXPECT_EQ(pts3[3], Vector2Int(0, 0));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_ContainsPoint) {
    OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(3, 0));
    EXPECT_TRUE(line.contains_point(Vector2Int(0, 0)));
    EXPECT_TRUE(line.contains_point(Vector2Int(1, 0)));
    EXPECT_TRUE(line.contains_point(Vector2Int(3, 0)));
    EXPECT_FALSE(line.contains_point(Vector2Int(4, 0)));
    EXPECT_FALSE(line.contains_point(Vector2Int(0, 1)));
    EXPECT_FALSE(line.contains_point(Vector2Int(-1, 0)));

    OrthogonalLineGrid2D vline(Vector2Int(0, 0), Vector2Int(0, 5));
    EXPECT_TRUE(vline.contains_point(Vector2Int(0, 2)));
    EXPECT_FALSE(vline.contains_point(Vector2Int(1, 2)));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_Shrink) {
    OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(5, 0));
    auto s1 = line.shrink(1);
    EXPECT_EQ(s1.from, Vector2Int(1, 0));
    EXPECT_EQ(s1.to, Vector2Int(4, 0));

    auto s2 = line.shrink(1, 2);
    EXPECT_EQ(s2.from, Vector2Int(1, 0));
    EXPECT_EQ(s2.to, Vector2Int(3, 0));

    OrthogonalLineGrid2D vline(Vector2Int(0, 0), Vector2Int(0, 5));
    auto vs = vline.shrink(2);
    EXPECT_EQ(vs.from, Vector2Int(0, 2));
    EXPECT_EQ(vs.to, Vector2Int(0, 3));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_Normalized) {
    auto n1 = OrthogonalLineGrid2D(Vector2Int(3, 0), Vector2Int(0, 0)).normalized();
    EXPECT_EQ(n1.from, Vector2Int(0, 0));
    EXPECT_EQ(n1.to, Vector2Int(3, 0));

    auto n2 = OrthogonalLineGrid2D(Vector2Int(0, 5), Vector2Int(0, 0)).normalized();
    EXPECT_EQ(n2.from, Vector2Int(0, 0));
    EXPECT_EQ(n2.to, Vector2Int(0, 5));

    auto n3 = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(3, 0)).normalized();
    EXPECT_EQ(n3.from, Vector2Int(0, 0));
    EXPECT_EQ(n3.to, Vector2Int(3, 0));
}

// ===========================================================================
// Этап E: Configuration space tests
// ===========================================================================

TEST(DungeonTopologyGeneratorConfigSpace, BetweenTwoSquares_NonEmpty) {
    auto sq = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D door_mode(1, 0);
    auto doors = door_mode.get_doors(sq);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space(sq, doors, sq, doors);

    EXPECT_FALSE(cs.lines.empty());
    EXPECT_FALSE(cs.reverse_doors.empty());
}

TEST(DungeonTopologyGeneratorConfigSpace, CompatibleNonOverlapping) {
    auto sq = PolygonGrid2D::get_square(5);

    EXPECT_TRUE(ConfigurationSpacesGrid2D::compatible_non_overlapping(
        sq, Vector2Int(0, 0), sq, Vector2Int(6, 0)));
    EXPECT_TRUE(ConfigurationSpacesGrid2D::compatible_non_overlapping(
        sq, Vector2Int(0, 0), sq, Vector2Int(0, 6)));
    EXPECT_FALSE(ConfigurationSpacesGrid2D::compatible_non_overlapping(
        sq, Vector2Int(0, 0), sq, Vector2Int(0, 0)));
    EXPECT_FALSE(ConfigurationSpacesGrid2D::compatible_non_overlapping(
        sq, Vector2Int(0, 0), sq, Vector2Int(3, 3)));
}

TEST(DungeonTopologyGeneratorConfigSpace, OffsetOnCS_ValidAndInvalid) {
    auto sq = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D door_mode(1, 0);
    auto doors = door_mode.get_doors(sq);

    auto cs = ConfigurationSpacesGrid2D::configuration_space_between(sq, doors, sq, doors);

    EXPECT_TRUE(offset_on_configuration_space(Vector2Int(5, 0), cs));
    EXPECT_TRUE(offset_on_configuration_space(Vector2Int(-5, 0), cs));
    EXPECT_TRUE(offset_on_configuration_space(Vector2Int(0, 5), cs));
    EXPECT_TRUE(offset_on_configuration_space(Vector2Int(0, -5), cs));
    EXPECT_FALSE(offset_on_configuration_space(Vector2Int(0, 0), cs));
    EXPECT_FALSE(offset_on_configuration_space(Vector2Int(3, 3), cs));
}

TEST(DungeonTopologyGeneratorConfigSpace, EnumerateOffsets_NonEmpty) {
    auto sq = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D door_mode(1, 0);
    auto doors = door_mode.get_doors(sq);

    auto cs = ConfigurationSpacesGrid2D::configuration_space_between(sq, doors, sq, doors);
    auto offsets = enumerate_configuration_space_offsets(cs);

    EXPECT_FALSE(offsets.empty());
    for (const auto& off : offsets) {
        EXPECT_TRUE(offset_on_configuration_space(off, cs));
    }
}

static std::set<Vector2Int> collect_cs_points(const ConfigurationSpaceGrid2D& cs) {
    std::set<Vector2Int> points;
    for (const auto& line : cs.lines) {
        auto pts = line.grid_points_inclusive();
        points.insert(pts.begin(), pts.end());
    }
    return points;
}

static std::set<Vector2Int> collect_cs_points(std::initializer_list<OrthogonalLineGrid2D> lines) {
    std::set<Vector2Int> points;
    for (const auto& line : lines) {
        auto pts = line.grid_points_inclusive();
        points.insert(pts.begin(), pts.end());
    }
    return points;
}

TEST(DungeonTopologyGeneratorConfigSpace, OverCorridor_VerticalCorridor) {
    // _edgar_ref ConfigurationSpacesGeneratorTests.GetConfigurationSpaceOverCorridor_SquareRoomVerticalCorridor:
    // exact expected lines (-4,7)-(4,7) and (-4,-7)-(4,-7)
    auto room = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D room_door_mode(1, 0);
    auto room_doors = room_door_mode.get_doors(room);

    auto corridor = PolygonGrid2D::get_rectangle(1, 2);
    // C# feeds these through ManualDoorMode -> degenerate point lines with side directions
    auto corridor_doors = ManualDoorModeGrid2D({
                                  DoorGrid2D{.from = {1, 0}, .to = {0, 0}},
                                  DoorGrid2D{.from = {0, 2}, .to = {1, 2}},
                              })
                              .get_doors(corridor);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space_over_corridor(
        room, room_doors, room, room_doors, corridor, corridor_doors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-4, 7), Vector2Int(4, 7)),
                                             OrthogonalLineGrid2D(Vector2Int(-4, -7), Vector2Int(4, -7))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpace, OverCorridor_HorizontalCorridor) {
    // _edgar_ref ConfigurationSpacesGeneratorTests.GetConfigurationSpaceOverCorridor_SquareRoomHorizontalCorridor:
    // exact expected lines (-7,-4)-(-7,4) and (7,-4)-(7,4)
    auto room = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D room_door_mode(1, 0);
    auto room_doors = room_door_mode.get_doors(room);

    auto corridor = PolygonGrid2D::get_rectangle(2, 1);
    auto corridor_doors = ManualDoorModeGrid2D({
                                  DoorGrid2D{.from = {0, 1}, .to = {0, 0}},
                                  DoorGrid2D{.from = {2, 0}, .to = {2, 1}},
                              })
                              .get_doors(corridor);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space_over_corridor(
        room, room_doors, room, room_doors, corridor, corridor_doors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-7, -4), Vector2Int(-7, 4)),
                                             OrthogonalLineGrid2D(Vector2Int(7, -4), Vector2Int(7, 4))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpace, OverCorridors_CombinedHAndV) {
    // _edgar_ref ConfigurationSpacesGeneratorTests.GetConfigurationSpaceOverCorridor_SquareRoomHorizontalVerticalCorridors
    auto room = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D room_door_mode(1, 0);
    auto room_doors = room_door_mode.get_doors(room);

    auto h_corridor = PolygonGrid2D::get_rectangle(2, 1);
    auto h_corridor_doors = ManualDoorModeGrid2D({
                                    DoorGrid2D{.from = {0, 1}, .to = {0, 0}},
                                    DoorGrid2D{.from = {2, 0}, .to = {2, 1}},
                                })
                                .get_doors(h_corridor);

    auto v_corridor = PolygonGrid2D::get_rectangle(1, 2);
    auto v_corridor_doors = ManualDoorModeGrid2D({
                                    DoorGrid2D{.from = {1, 0}, .to = {0, 0}},
                                    DoorGrid2D{.from = {0, 2}, .to = {1, 2}},
                                })
                                .get_doors(v_corridor);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space_over_corridors(
        room, room_doors, room, room_doors,
        {{h_corridor, h_corridor_doors}, {v_corridor, v_corridor_doors}});

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-7, -4), Vector2Int(-7, 4)),
                                             OrthogonalLineGrid2D(Vector2Int(7, -4), Vector2Int(7, 4)),
                                             OrthogonalLineGrid2D(Vector2Int(-4, 7), Vector2Int(4, 7)),
                                             OrthogonalLineGrid2D(Vector2Int(-4, -7), Vector2Int(4, -7))});
    EXPECT_EQ(points, expected);
}

// ===========================================================================
// C# parity ports: ConfigurationSpacesGeneratorTests (exact point sets)
// (_edgar_ref @ 258c83a, Edgar.Tests/Core/ConfigurationSpaces)
// ===========================================================================

namespace {

std::vector<TransformationGrid2D> all_8_transforms() {
    return {TransformationGrid2D::Identity,  TransformationGrid2D::Rotate90, TransformationGrid2D::Rotate180,
            TransformationGrid2D::Rotate270, TransformationGrid2D::MirrorX,  TransformationGrid2D::MirrorY,
            TransformationGrid2D::Diagonal13, TransformationGrid2D::Diagonal24};
}

} // namespace

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, TwoSquares_SimpleDoorMode_ExactPoints) {
    // From the commented-out CSGeneratorTests.GetConfigurationSpace_Squares (OverlapMode(1, 0)
    // corresponds to SimpleDoorMode(doorLength 1, cornerDistance 0)); expected ring around fixed square
    auto moving = PolygonGrid2D::get_square(3);
    auto fixed = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D mode(1, 0);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space(moving, mode.get_doors(moving), fixed, mode.get_doors(fixed));

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-2, 5), Vector2Int(4, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(-2, -3), Vector2Int(4, -3)),
                                             OrthogonalLineGrid2D(Vector2Int(5, 4), Vector2Int(5, -2)),
                                             OrthogonalLineGrid2D(Vector2Int(-3, -2), Vector2Int(-3, 4))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, OverCorridors_SquareCorridorLengthZero_ExactPoints) {
    // _edgar_ref ...GetConfigurationSpaceOverCorridor_SquareRoomSquareCorridorLengthZero
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D room_template(PolygonGrid2D::get_square(5),
                                     std::make_shared<SimpleDoorModeGrid2D>(0, 0), "room",
                                     std::nullopt, all_8_transforms());
    RoomTemplateGrid2D corridor_template(PolygonGrid2D::get_square(2),
                                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                                             DoorGrid2D{.from = {1, 0}, .to = {1, 0}},
                                             DoorGrid2D{.from = {1, 2}, .to = {1, 2}},
                                         }),
                                         "corridor", std::nullopt, all_8_transforms());

    const auto room_instances = gen.get_room_template_instances(room_template);
    ASSERT_EQ(room_instances.size(), 1u);
    const auto corridor_instances = gen.get_room_template_instances(corridor_template);

    std::vector<std::pair<PolygonGrid2D, std::vector<DoorLineGrid2D>>> corridors;
    for (const auto& inst : corridor_instances) {
        corridors.emplace_back(inst.outline, inst.door_lines);
    }

    const auto& room = room_instances.front();
    auto cs = gen.get_configuration_space_over_corridors(room.outline, room.door_lines, room.outline,
                                                         room.door_lines, corridors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-7, -5), Vector2Int(-7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(7, -5), Vector2Int(7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, 7), Vector2Int(5, 7)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, -7), Vector2Int(5, -7))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, OverCorridors_DegeneratedLines_ExactPoints) {
    // _edgar_ref ...GetConfigurationSpaceOverCorridor_DegeneratedLines
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D room_template(PolygonGrid2D::get_rectangle(5, 4),
                                     std::make_shared<SimpleDoorModeGrid2D>(0, 2), "room", std::nullopt,
                                     {TransformationGrid2D::Identity});
    RoomTemplateGrid2D corridor_template(PolygonGrid2D::get_square(2),
                                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                                             DoorGrid2D{.from = {1, 0}, .to = {1, 0}},
                                             DoorGrid2D{.from = {1, 2}, .to = {1, 2}},
                                         }),
                                         "corridor", std::nullopt, {TransformationGrid2D::Rotate90});

    const auto room_instances = gen.get_room_template_instances(room_template);
    ASSERT_EQ(room_instances.size(), 1u);
    const auto corridor_instances = gen.get_room_template_instances(corridor_template);

    std::vector<std::pair<PolygonGrid2D, std::vector<DoorLineGrid2D>>> corridors;
    for (const auto& inst : corridor_instances) {
        corridors.emplace_back(inst.outline, inst.door_lines);
    }

    const auto& room = room_instances.front();
    auto cs = gen.get_configuration_space_over_corridors(room.outline, room.door_lines, room.outline,
                                                         room.door_lines, corridors);

    const auto points = collect_cs_points(cs);
    const std::set<Vector2Int> expected{Vector2Int(-7, 0), Vector2Int(7, 0)};
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, OverCorridors_SquareRoomSquareCorridor_ExactPoints) {
    // _edgar_ref ...GetConfigurationSpaceOverCorridor_SquareRoomSquareCorridor
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D room_template(PolygonGrid2D::get_square(5),
                                     std::make_shared<SimpleDoorModeGrid2D>(1, 0), "room",
                                     std::nullopt, all_8_transforms());
    RoomTemplateGrid2D corridor_template(PolygonGrid2D::get_square(2),
                                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                                             DoorGrid2D{.from = {0, 0}, .to = {1, 0}},
                                             DoorGrid2D{.from = {1, 0}, .to = {2, 0}},
                                             DoorGrid2D{.from = {0, 2}, .to = {1, 2}},
                                             DoorGrid2D{.from = {1, 2}, .to = {2, 2}},
                                         }),
                                         "corridor", std::nullopt, all_8_transforms());

    const auto room_instances = gen.get_room_template_instances(room_template);
    ASSERT_EQ(room_instances.size(), 1u);
    const auto corridor_instances = gen.get_room_template_instances(corridor_template);

    std::vector<std::pair<PolygonGrid2D, std::vector<DoorLineGrid2D>>> corridors;
    for (const auto& inst : corridor_instances) {
        corridors.emplace_back(inst.outline, inst.door_lines);
    }

    const auto& room = room_instances.front();
    auto cs = gen.get_configuration_space_over_corridors(room.outline, room.door_lines, room.outline,
                                                         room.door_lines, corridors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-7, -5), Vector2Int(-7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(7, -5), Vector2Int(7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, 7), Vector2Int(5, 7)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, -7), Vector2Int(5, -7))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, OverCorridors_DifferentDoorLengths_ExactPoints) {
    // _edgar_ref ...GetConfigurationSpaceOverCorridor_SquareRoomSquareCorridorDifferentDoorLengths
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D room_template(PolygonGrid2D::get_square(5),
                                     std::make_shared<SimpleDoorModeGrid2D>(1, 0), "room",
                                     std::nullopt, all_8_transforms());
    RoomTemplateGrid2D corridor_template(PolygonGrid2D::get_square(2),
                                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                                             DoorGrid2D{.from = {0, 0}, .to = {1, 0}},
                                             DoorGrid2D{.from = {1, 0}, .to = {2, 0}},
                                             DoorGrid2D{.from = {0, 2}, .to = {1, 2}},
                                             DoorGrid2D{.from = {1, 2}, .to = {2, 2}},
                                             DoorGrid2D{.from = {0, 0}, .to = {0, 2}},
                                             DoorGrid2D{.from = {2, 0}, .to = {2, 2}},
                                         }),
                                         "corridor", std::nullopt, all_8_transforms());

    const auto room_instances = gen.get_room_template_instances(room_template);
    ASSERT_EQ(room_instances.size(), 1u);
    const auto corridor_instances = gen.get_room_template_instances(corridor_template);

    std::vector<std::pair<PolygonGrid2D, std::vector<DoorLineGrid2D>>> corridors;
    for (const auto& inst : corridor_instances) {
        corridors.emplace_back(inst.outline, inst.door_lines);
    }

    const auto& room = room_instances.front();
    auto cs = gen.get_configuration_space_over_corridors(room.outline, room.door_lines, room.outline,
                                                         room.door_lines, corridors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({OrthogonalLineGrid2D(Vector2Int(-7, -5), Vector2Int(-7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(7, -5), Vector2Int(7, 5)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, 7), Vector2Int(5, 7)),
                                             OrthogonalLineGrid2D(Vector2Int(-5, -7), Vector2Int(5, -7))});
    EXPECT_EQ(points, expected);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, OverCorridor_LShapedCorridor_ExactPoints) {
    // _edgar_ref ...GetConfigurationSpaceOverCorridor_SquareRoomLShapedCorridor
    auto room = PolygonGrid2D::get_square(5);
    SimpleDoorModeGrid2D room_door_mode(1, 0);
    auto room_doors = room_door_mode.get_doors(room);

    auto corridor = PolygonGrid2DBuilder()
                        .add_point(0, 1)
                        .add_point(0, 2)
                        .add_point(2, 2)
                        .add_point(2, 0)
                        .add_point(1, 0)
                        .add_point(1, 1)
                        .build();
    std::vector<DoorLineGrid2D> corridor_doors = ManualDoorModeGrid2D({
        DoorGrid2D{.from = {0, 1}, .to = {0, 2}},
        DoorGrid2D{.from = {2, 0}, .to = {1, 0}},
    }).get_doors(corridor);

    ConfigurationSpacesGenerator gen;
    auto cs = gen.get_configuration_space_over_corridor(room, room_doors, room, room_doors, corridor,
                                                        corridor_doors);

    const auto points = collect_cs_points(cs);
    const auto expected = collect_cs_points({
        OrthogonalLineGrid2D(Vector2Int(-6, 2), Vector2Int(-6, 6)), // Left side
        OrthogonalLineGrid2D(Vector2Int(-5, 2), Vector2Int(-5, 6)),
        OrthogonalLineGrid2D(Vector2Int(-6, 6), Vector2Int(-2, 6)), // Top side
        OrthogonalLineGrid2D(Vector2Int(-6, 5), Vector2Int(-2, 5)),
        OrthogonalLineGrid2D(Vector2Int(2, -6), Vector2Int(6, -6)), // Bottom side
        OrthogonalLineGrid2D(Vector2Int(2, -5), Vector2Int(6, -5)),
        OrthogonalLineGrid2D(Vector2Int(5, -2), Vector2Int(5, -6)), // Right side
        OrthogonalLineGrid2D(Vector2Int(6, -2), Vector2Int(6, -6)),
    });
    EXPECT_EQ(points, expected);
}

// ----- GetRoomTemplateInstances (C# ConfigurationSpacesGeneratorTests) -----

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_SquareIdentity_OneInstance) {
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t(PolygonGrid2D::get_square(10), std::make_shared<SimpleDoorModeGrid2D>(1, 0),
                         "square", std::nullopt, {TransformationGrid2D::Identity});
    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(instances[0].outline, PolygonGrid2D::get_square(10));
    EXPECT_EQ(instances[0].transformations,
              std::vector<TransformationGrid2D>{TransformationGrid2D::Identity});
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_SquareNotNormalized_NormalizedInstance) {
    ConfigurationSpacesGenerator gen;
    const auto shifted = PolygonGrid2D::get_square(10) + Vector2Int(5, 5);
    RoomTemplateGrid2D t(shifted, std::make_shared<SimpleDoorModeGrid2D>(1, 0), "square", std::nullopt,
                         {TransformationGrid2D::Identity});
    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(instances[0].outline, PolygonGrid2D::get_square(10));
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_SquareAllTransformations_OneInstance) {
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t(PolygonGrid2D::get_square(10), std::make_shared<SimpleDoorModeGrid2D>(1, 0),
                         "square", std::nullopt, all_8_transforms());
    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(instances[0].outline, PolygonGrid2D::get_square(10));
    EXPECT_EQ(instances[0].transformations.size(), 8u);
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_SquareRotationsOneDoor_FourInstances) {
    // _edgar_ref ...GetRoomTemplateInstances_SquareAllRotationsOneDoor_ReturnsFourInstance
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t(PolygonGrid2D::get_square(10),
                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                             DoorGrid2D{.from = {0, 0}, .to = {1, 0}},
                         }),
                         "square", std::nullopt,
                         {TransformationGrid2D::Identity, TransformationGrid2D::Rotate90,
                          TransformationGrid2D::Rotate180, TransformationGrid2D::Rotate270});

    const std::map<TransformationGrid2D, Vector2Int> expected_door = {
        {TransformationGrid2D::Identity, Vector2Int(1, 0)},
        {TransformationGrid2D::Rotate90, Vector2Int(0, 9)},
        {TransformationGrid2D::Rotate180, Vector2Int(9, 10)},
        {TransformationGrid2D::Rotate270, Vector2Int(10, 1)},
    };

    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 4u);
    for (const auto& inst : instances) {
        EXPECT_EQ(inst.outline, PolygonGrid2D::get_square(10));
        ASSERT_EQ(inst.transformations.size(), 1u);
        ASSERT_EQ(inst.door_lines.size(), 1u);
        const auto tr = inst.transformations.front();
        const auto it = expected_door.find(tr);
        ASSERT_TRUE(it != expected_door.end());
        EXPECT_EQ(inst.door_lines[0].length, 1);
        EXPECT_EQ(inst.door_lines[0].line.from, it->second);
        EXPECT_EQ(inst.door_lines[0].line.to, it->second);
    }
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_SquareAllTransformationsOneDoor_EightInstances) {
    // _edgar_ref ...GetRoomTemplateInstances_SquareAllTransformationsOneDoor_ReturnsFourInstance
    // (name is historical; the C# test asserts 8 instances)
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t(PolygonGrid2D::get_square(10),
                         std::make_shared<ManualDoorModeGrid2D>(std::vector<DoorGrid2D>{
                             DoorGrid2D{.from = {0, 0}, .to = {1, 0}},
                         }),
                         "square", std::nullopt, all_8_transforms());

    const std::map<TransformationGrid2D, Vector2Int> expected_door = {
        {TransformationGrid2D::Identity, Vector2Int(1, 0)},
        {TransformationGrid2D::Rotate90, Vector2Int(0, 9)},
        {TransformationGrid2D::Rotate180, Vector2Int(9, 10)},
        {TransformationGrid2D::Rotate270, Vector2Int(10, 1)},
        {TransformationGrid2D::MirrorY, Vector2Int(10, 0)},
        {TransformationGrid2D::MirrorX, Vector2Int(0, 10)},
        {TransformationGrid2D::Diagonal13, Vector2Int(0, 0)},
        {TransformationGrid2D::Diagonal24, Vector2Int(10, 10)},
    };

    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 8u);
    for (const auto& inst : instances) {
        EXPECT_EQ(inst.outline, PolygonGrid2D::get_square(10));
        ASSERT_EQ(inst.transformations.size(), 1u);
        ASSERT_EQ(inst.door_lines.size(), 1u);
        const auto tr = inst.transformations.front();
        const auto it = expected_door.find(tr);
        ASSERT_TRUE(it != expected_door.end());
        EXPECT_EQ(inst.door_lines[0].length, 1);
        EXPECT_EQ(inst.door_lines[0].line.from, it->second);
        EXPECT_EQ(inst.door_lines[0].line.to, it->second);
    }
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, RoomTemplateInstances_RectangleAllRotations_TwoInstances) {
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t(PolygonGrid2D::get_rectangle(5, 10),
                         std::make_shared<SimpleDoorModeGrid2D>(1, 0), "rect", std::nullopt,
                         {TransformationGrid2D::Identity, TransformationGrid2D::Rotate90,
                          TransformationGrid2D::Rotate180, TransformationGrid2D::Rotate270});
    const auto instances = gen.get_room_template_instances(t);
    ASSERT_EQ(instances.size(), 2u);

    const bool first_is_tall = instances[0].outline == PolygonGrid2D::get_rectangle(5, 10);
    const auto& tall = first_is_tall ? instances[0] : instances[1];
    const auto& wide = first_is_tall ? instances[1] : instances[0];
    EXPECT_EQ(tall.outline, PolygonGrid2D::get_rectangle(5, 10));
    EXPECT_EQ(wide.outline, PolygonGrid2D::get_rectangle(10, 5));
}

TEST(DungeonTopologyGeneratorConfigSpaceCsharpParity, Generate_BasicTest_ShapeCountsPerNode) {
    // _edgar_ref Edgar.IntegrationTests ConfigurationSpacesGeneratorTests.Generate_BasicTest:
    // square(10) with all transformations -> 1 shape; rectangle(5,10) -> 2 shapes;
    // node with {square, rectangle} -> 3 shapes total
    ConfigurationSpacesGenerator gen;
    RoomTemplateGrid2D t1(PolygonGrid2D::get_square(10), std::make_shared<SimpleDoorModeGrid2D>(1, 0),
                          "square", std::nullopt, all_8_transforms());
    RoomTemplateGrid2D t2(PolygonGrid2D::get_rectangle(5, 10), std::make_shared<SimpleDoorModeGrid2D>(1, 0),
                          "rect", std::nullopt, all_8_transforms());

    EXPECT_EQ(gen.get_room_template_instances(t1).size(), 1u);
    EXPECT_EQ(gen.get_room_template_instances(t2).size(), 2u);
    // Node with both templates: 1 + 2 = 3 shapes (C# GetShapesForNode(node1).Count == 3)
    EXPECT_EQ(gen.get_room_template_instances(t1).size() + gen.get_room_template_instances(t2).size(), 3u);
}


// ===== Phase A: Algorithm parity tests =====

// ===========================================================================
// C# parity ports: OverlapModeHandlerTests / SpecificPositionsModeHandlerTests
// (_edgar_ref @ 258c83a, Edgar.Tests/Core/Doors)
// ===========================================================================

namespace {

struct DoorSpec {
    Vector2Int from;
    Vector2Int to;
    OrthogonalDirection direction;
    int length;
};

std::multiset<std::tuple<int, int, int, int, int, int>> door_specs_as_multiset(
    const std::vector<DoorLineGrid2D>& doors) {
    std::multiset<std::tuple<int, int, int, int, int, int>> out;
    for (const auto& d : doors) {
        out.emplace(d.line.from.x, d.line.from.y, d.line.to.x, d.line.to.y,
                    static_cast<int>(d.get_direction()), d.length);
    }
    return out;
}

std::multiset<std::tuple<int, int, int, int, int, int>> expected_multiset(
    std::initializer_list<DoorSpec> specs) {
    std::multiset<std::tuple<int, int, int, int, int, int>> out;
    for (const auto& s : specs) {
        out.emplace(s.from.x, s.from.y, s.to.x, s.to.y, static_cast<int>(s.direction), s.length);
    }
    return out;
}

} // namespace

TEST(DungeonTopologyGeneratorDoorsCsharpParity, OverlapMode_Rectangle_NoOverlap) {
    // _edgar_ref OverlapModeHandlerTests.Rectangle_NoOverlap
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = SimpleDoorModeGrid2D(1, 0).get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 0}, {0, 4}, OrthogonalDirection::Top, 1},
                  {{0, 5}, {2, 5}, OrthogonalDirection::Right, 1},
                  {{3, 5}, {3, 1}, OrthogonalDirection::Bottom, 1},
                  {{3, 0}, {1, 0}, OrthogonalDirection::Left, 1},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, OverlapMode_Rectangle_OneOverlap) {
    // _edgar_ref OverlapModeHandlerTests.Rectangle_OneOverlap (degenerate doors on short sides)
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = SimpleDoorModeGrid2D(1, 1).get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 1}, {0, 3}, OrthogonalDirection::Top, 1},
                  {{1, 5}, {1, 5}, OrthogonalDirection::Right, 1},
                  {{3, 4}, {3, 2}, OrthogonalDirection::Bottom, 1},
                  {{2, 0}, {2, 0}, OrthogonalDirection::Left, 1},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, OverlapMode_Rectangle_TwoOverlap) {
    // _edgar_ref OverlapModeHandlerTests.Rectangle_TwoOverlap (short sides skipped entirely)
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = SimpleDoorModeGrid2D(1, 2).get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 2}, {0, 2}, OrthogonalDirection::Top, 1},
                  {{3, 3}, {3, 3}, OrthogonalDirection::Bottom, 1},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, OverlapMode_Rectangle_LengthTwo) {
    // _edgar_ref OverlapModeHandlerTests.Rectangle_LengthTwo
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = SimpleDoorModeGrid2D(2, 0).get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 0}, {0, 3}, OrthogonalDirection::Top, 2},
                  {{0, 5}, {1, 5}, OrthogonalDirection::Right, 2},
                  {{3, 5}, {3, 2}, OrthogonalDirection::Bottom, 2},
                  {{3, 0}, {2, 0}, OrthogonalDirection::Left, 2},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, OverlapMode_Rectangle_LengthZero) {
    // _edgar_ref OverlapModeHandlerTests.Rectangle_LengthZero (whole sides, length 0)
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = SimpleDoorModeGrid2D(0, 0).get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 0}, {0, 5}, OrthogonalDirection::Top, 0},
                  {{0, 5}, {3, 5}, OrthogonalDirection::Right, 0},
                  {{3, 5}, {3, 0}, OrthogonalDirection::Bottom, 0},
                  {{3, 0}, {0, 0}, OrthogonalDirection::Left, 0},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, SpecificPositions_Rectangle_LengthZeroCorners) {
    // _edgar_ref SpecificPositionsModeHandlerTests.Rectangle_LengthZeroCorners:
    // a corner point lies on two sides -> two door lines per corner
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = ManualDoorModeGrid2D({
                           DoorGrid2D{.from = {0, 0}, .to = {0, 0}},
                           DoorGrid2D{.from = {0, 5}, .to = {0, 5}},
                           DoorGrid2D{.from = {3, 5}, .to = {3, 5}},
                           DoorGrid2D{.from = {3, 0}, .to = {3, 0}},
                       })
                           .get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 0}, {0, 0}, OrthogonalDirection::Left, 0},
                  {{0, 0}, {0, 0}, OrthogonalDirection::Top, 0},
                  {{0, 5}, {0, 5}, OrthogonalDirection::Top, 0},
                  {{0, 5}, {0, 5}, OrthogonalDirection::Right, 0},
                  {{3, 5}, {3, 5}, OrthogonalDirection::Right, 0},
                  {{3, 5}, {3, 5}, OrthogonalDirection::Bottom, 0},
                  {{3, 0}, {3, 0}, OrthogonalDirection::Bottom, 0},
                  {{3, 0}, {3, 0}, OrthogonalDirection::Left, 0},
              }));
}

TEST(DungeonTopologyGeneratorDoorsCsharpParity, SpecificPositions_Rectangle_LengthZeroInside) {
    // _edgar_ref SpecificPositionsModeHandlerTests.Rectangle_LengthZeroInside
    const auto polygon = PolygonGrid2D::get_rectangle(3, 5);
    const auto doors = ManualDoorModeGrid2D({
                           DoorGrid2D{.from = {0, 1}, .to = {0, 1}},
                           DoorGrid2D{.from = {1, 5}, .to = {1, 5}},
                           DoorGrid2D{.from = {3, 4}, .to = {3, 4}},
                           DoorGrid2D{.from = {2, 0}, .to = {2, 0}},
                       })
                           .get_doors(polygon);
    EXPECT_EQ(door_specs_as_multiset(doors),
              expected_multiset({
                  {{0, 1}, {0, 1}, OrthogonalDirection::Top, 0},
                  {{1, 5}, {1, 5}, OrthogonalDirection::Right, 0},
                  {{3, 4}, {3, 4}, OrthogonalDirection::Bottom, 0},
                  {{2, 0}, {2, 0}, OrthogonalDirection::Left, 0},
              }));
}

// ===== Phase A: Algorithm parity tests =====

TEST(DungeonTopologyGeneratorGraphs, IsBipartite_OddCycles_ReturnsFalse) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0); g.add_vertex(1); g.add_vertex(2);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    EXPECT_FALSE(is_bipartite(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsBipartite_CompleteBipartite_ReturnsTrue) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 7; ++i) g.add_vertex(i);
    g.add_edge(0, 3); g.add_edge(0, 4); g.add_edge(0, 5); g.add_edge(0, 6);
    g.add_edge(1, 3); g.add_edge(1, 4); g.add_edge(1, 5); g.add_edge(1, 6);
    g.add_edge(2, 3); g.add_edge(2, 4); g.add_edge(2, 5); g.add_edge(2, 6);
    EXPECT_TRUE(is_bipartite(g));
    std::vector<int> pa, pb;
    EXPECT_TRUE(is_bipartite(g, pa, pb));
    EXPECT_EQ(pa.size(), 3u);
    EXPECT_EQ(pb.size(), 4u);
}

TEST(DungeonTopologyGeneratorGraphs, IsBipartite_NoEdges_ReturnsTrue) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 5; ++i) g.add_vertex(i);
    EXPECT_TRUE(is_bipartite(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsBipartite_NotBipartite_WithOddCycle) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 5; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 3);
    g.add_edge(3, 4); g.add_edge(4, 0);
    EXPECT_FALSE(is_bipartite(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsBipartite_MoreComponents) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 6; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    EXPECT_FALSE(is_bipartite(g));

    UndirectedAdjacencyListGraph<int> g2;
    for (int i = 0; i < 6; ++i) g2.add_vertex(i);
    g2.add_edge(0, 1); g2.add_edge(2, 3); g2.add_edge(4, 5);
    EXPECT_TRUE(is_bipartite(g2));
}

TEST(DungeonTopologyGeneratorGraphs, IsPlanar_Empty_ReturnsTrue) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    EXPECT_TRUE(is_planar(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsPlanar_C3_ReturnsTrue) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0); g.add_vertex(1); g.add_vertex(2);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    EXPECT_TRUE(is_planar(g));
}

TEST(DungeonTopologyGeneratorGraphs, IsPlanar_K5_NotPlanar) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 5; ++i) g.add_vertex(i);
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j)
            g.add_edge(i, j);
    EXPECT_FALSE(is_planar(g));
}

TEST(DungeonTopologyGeneratorGraphs, GetCycles_SingleCycle) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0); g.add_vertex(1); g.add_vertex(2); g.add_vertex(3);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 3); g.add_edge(3, 0);
    auto cycles = get_cycles(g);
    EXPECT_GE(cycles.size(), 1u);
    bool found_4 = false;
    for (const auto& c : cycles) {
        if (c.size() == 4u) found_4 = true;
    }
    EXPECT_TRUE(found_4);
}

TEST(DungeonTopologyGeneratorGraphs, GetCycles_TwoCyclesWithSharedEdge) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 4; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    g.add_edge(1, 3); g.add_edge(3, 2);
    auto cycles = get_cycles(g);
    EXPECT_GE(cycles.size(), 2u);
}

TEST(DungeonTopologyGeneratorGraphs, GetCycles_TwoCyclesWithSharedNode) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    g.add_vertex(0); g.add_vertex(1); g.add_vertex(2);
    g.add_vertex(3); g.add_vertex(4);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 0);
    g.add_edge(0, 3); g.add_edge(3, 4); g.add_edge(4, 0);
    auto cycles = get_cycles(g);
    EXPECT_GE(cycles.size(), 2u);
}

TEST(DungeonTopologyGeneratorGraphs, GetCycles_MultipleCycles) {
    using namespace dungeon_topology_generator::graphs;
    UndirectedAdjacencyListGraph<int> g;
    for (int i = 0; i < 5; ++i) g.add_vertex(i);
    g.add_edge(0, 1); g.add_edge(1, 2); g.add_edge(2, 3); g.add_edge(3, 4); g.add_edge(4, 0);
    g.add_edge(0, 2); g.add_edge(0, 3);
    auto cycles = get_cycles(g);
    EXPECT_GE(cycles.size(), 3u);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapArea_NonTouching_ReturnsZero) {
    using namespace dungeon_topology_generator::geometry;
    auto sq = PolygonGrid2D::get_square(5);
    EXPECT_DOUBLE_EQ(polygons_overlap_area_exact(sq, Vector2Int(0, 0), sq, Vector2Int(10, 10)), 0.0);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapArea_TwoSquares) {
    using namespace dungeon_topology_generator::geometry;
    auto sq = PolygonGrid2D::get_square(5);
    double area = polygons_overlap_area_exact(sq, Vector2Int(0, 0), sq, Vector2Int(3, 3));
    EXPECT_DOUBLE_EQ(area, 4.0);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapArea_TwoRectangles) {
    using namespace dungeon_topology_generator::geometry;
    auto rect = PolygonGrid2D::get_rectangle(4, 6);
    double area = polygons_overlap_area_exact(rect, Vector2Int(0, 0), rect, Vector2Int(2, 3));
    EXPECT_DOUBLE_EQ(area, 6.0);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapArea_PlusShapeAndSquare) {
    using namespace dungeon_topology_generator::geometry;
    PolygonGrid2DBuilder b;
    b.add_point(2, 2); b.add_point(0, 2); b.add_point(0, 4); b.add_point(2, 4);
    b.add_point(2, 6); b.add_point(4, 6); b.add_point(4, 4); b.add_point(6, 4);
    b.add_point(6, 2); b.add_point(4, 2); b.add_point(4, 0); b.add_point(2, 0);
    auto plus = b.build();
    auto sq = PolygonGrid2D::get_square(4);
    double area = polygons_overlap_area_exact(plus, Vector2Int(0, 0), sq, Vector2Int(1, 1));
    EXPECT_DOUBLE_EQ(area, 12.0);
}

TEST(DungeonTopologyGeneratorGeometry, DoTouch_TwoSquares) {
    using namespace dungeon_topology_generator::geometry;
    auto sq = PolygonGrid2D::get_square(5);
    EXPECT_TRUE(polygons_touch(sq, Vector2Int(0, 0), sq, Vector2Int(5, 0)));
    EXPECT_FALSE(polygons_touch(sq, Vector2Int(0, 0), sq, Vector2Int(5, 5)));
}

TEST(DungeonTopologyGeneratorGeometry, DoHaveMinimumDistance_TwoSquares) {
    using namespace dungeon_topology_generator::geometry;
    auto sq = PolygonGrid2D::get_square(5);
    EXPECT_TRUE(polygons_have_minimum_distance(sq, Vector2Int(0, 0), sq, Vector2Int(10, 10), 5));
    EXPECT_FALSE(polygons_have_minimum_distance(sq, Vector2Int(0, 0), sq, Vector2Int(7, 7), 5));
}

TEST(DungeonTopologyGeneratorGeometry, NormalizePolygon_ReordersVertices) {
    using namespace dungeon_topology_generator::geometry;
    auto sq = PolygonGrid2D::get_square(4);
    auto norm = normalize_polygon(sq);
    EXPECT_EQ(norm.points().size(), 4u);
    EXPECT_EQ(norm.points()[0], Vector2Int(0, 0));
}

// ===========================================================================
// Stage B: OrthogonalLine + Polygon + HopcroftKarp + OverlapAlongLine tests
// ===========================================================================

namespace {
auto overlap_along_line_wrap(
    const PolygonGrid2D& mp, const PolygonGrid2D& fp, const OrthogonalLineGrid2D& l) {
    return dungeon_topology_generator::geometry::overlap_along_line(mp, fp, l);
}
} // anonymous

// --- B-T1: OrthogonalLine Rotate ---

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_Rotate_ReturnsRotated) {
    {
        OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(5, 0));
        auto r1 = line.rotate(90);
        EXPECT_EQ(r1.from, Vector2Int(0, 0));
        EXPECT_EQ(r1.to, Vector2Int(0, -5));
        auto r2 = line.rotate(-270);
        EXPECT_EQ(r2.from, Vector2Int(0, 0));
        EXPECT_EQ(r2.to, Vector2Int(0, -5));
    }
    {
        OrthogonalLineGrid2D line(Vector2Int(-2, -2), Vector2Int(-2, 5));
        auto r1 = line.rotate(180);
        EXPECT_EQ(r1.from, Vector2Int(2, 2));
        EXPECT_EQ(r1.to, Vector2Int(2, -5));
        auto r2 = line.rotate(-180);
        EXPECT_EQ(r2.from, Vector2Int(2, 2));
        EXPECT_EQ(r2.to, Vector2Int(2, -5));
    }
}

// NOTE: C++ OrthogonalLineGrid2D::rotate does not validate degrees.
// Invalid angles produce degenerate lines. C# throws; skip that test.

// --- B-T2: RotateDirection ---

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_RotateDirection_ReturnsRotated) {
    EXPECT_EQ(rotate_direction(OrthogonalDirection::Right, 90), OrthogonalDirection::Bottom);
    EXPECT_EQ(rotate_direction(OrthogonalDirection::Bottom, -180), OrthogonalDirection::Top);
}

// --- B-T3: GetPoints Top/Bottom/Right/Left ---

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_GetPoints_Top) {
    auto pts = OrthogonalLineGrid2D(Vector2Int(2, 2), Vector2Int(2, 4)).grid_points_inclusive();
    ASSERT_EQ(pts.size(), 3u);
    EXPECT_EQ(pts[0], Vector2Int(2, 2));
    EXPECT_EQ(pts[1], Vector2Int(2, 3));
    EXPECT_EQ(pts[2], Vector2Int(2, 4));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_GetPoints_Bottom) {
    auto pts = OrthogonalLineGrid2D(Vector2Int(2, 4), Vector2Int(2, 2)).grid_points_inclusive();
    ASSERT_EQ(pts.size(), 3u);
    EXPECT_EQ(pts[0], Vector2Int(2, 4));
    EXPECT_EQ(pts[1], Vector2Int(2, 3));
    EXPECT_EQ(pts[2], Vector2Int(2, 2));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_GetPoints_Right) {
    auto pts = OrthogonalLineGrid2D(Vector2Int(5, 3), Vector2Int(8, 3)).grid_points_inclusive();
    ASSERT_EQ(pts.size(), 4u);
    EXPECT_EQ(pts[0], Vector2Int(5, 3));
    EXPECT_EQ(pts[1], Vector2Int(6, 3));
    EXPECT_EQ(pts[2], Vector2Int(7, 3));
    EXPECT_EQ(pts[3], Vector2Int(8, 3));
}

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_GetPoints_Left) {
    auto pts = OrthogonalLineGrid2D(Vector2Int(8, 3), Vector2Int(5, 3)).grid_points_inclusive();
    ASSERT_EQ(pts.size(), 4u);
    EXPECT_EQ(pts[0], Vector2Int(8, 3));
    EXPECT_EQ(pts[1], Vector2Int(7, 3));
    EXPECT_EQ(pts[2], Vector2Int(6, 3));
    EXPECT_EQ(pts[3], Vector2Int(5, 3));
}

// --- B-T4: Shrink_Invalid_Throws ---

TEST(DungeonTopologyGeneratorUtils, OrthogonalLine_Shrink_Invalid_Throws) {
    OrthogonalLineGrid2D line(Vector2Int(0, 0), Vector2Int(5, 0));
    EXPECT_THROW(line.shrink(3), std::invalid_argument);
    OrthogonalLineGrid2D line2(Vector2Int(0, 0), Vector2Int(-6, 0));
    EXPECT_THROW(line2.shrink(4, 3), std::invalid_argument);
}

// --- B-T5: GetAllTransformations ---

TEST(DungeonTopologyGeneratorGeometry, Polygon_GetAllTransformations_Square) {
    auto sq = PolygonGrid2D::get_square(4);
    auto transforms = sq.get_all_transformations();
    EXPECT_EQ(transforms.size(), 8u);
}

TEST(DungeonTopologyGeneratorGeometry, Polygon_GetAllTransformations_Rectangle) {
    auto rect = PolygonGrid2D::get_rectangle(3, 5);
    auto transforms = rect.get_all_transformations();
    EXPECT_EQ(transforms.size(), 8u);
}

// --- B-T6: Constructor_OverlappingEdges_Throws ---

TEST(DungeonTopologyGeneratorGeometry, PolygonConstructor_OverlappingEdges_Throws) {
    EXPECT_THROW({
        PolygonGrid2D p(std::vector<Vector2Int>{
            Vector2Int(0, 0), Vector2Int(3, 0), Vector2Int(3, 2),
            Vector2Int(0, 2), Vector2Int(0, 4)
        });
    }, std::invalid_argument);
}

// --- B-T7: OverlapAlongLine tests (bruteforce reference) ---

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_Rectangles_NonOverlapping) {
    auto p1 = PolygonGrid2D::get_square(5);
    auto p2 = PolygonGrid2D::get_rectangle(2, 3) + Vector2Int(10, 10);
    auto line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(10, 0));
    auto result = overlap_along_line_wrap(p1, p2, line);
    EXPECT_EQ(result.size(), 0u);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_Rectangles_OverlapEnd) {
    auto p1 = PolygonGrid2D::get_square(5);
    auto p2 = PolygonGrid2D::get_rectangle(2, 3) + Vector2Int(0, 8);
    auto line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 10));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, Vector2Int(0, 4));
    EXPECT_TRUE(result[0].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_Rectangles_OverlapStart2) {
    auto p1 = PolygonGrid2D::get_square(5);
    auto p2 = PolygonGrid2D::get_rectangle(2, 3) + Vector2Int(0, -3);
    auto line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 10));
    auto result = overlap_along_line_wrap(p1, p2, line);
    EXPECT_EQ(result.size(), 0u);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_Rectangles_OverlapStart) {
    auto p1 = PolygonGrid2D::get_square(5);
    auto p2 = PolygonGrid2D::get_rectangle(2, 3);
    auto line = OrthogonalLineGrid2D(Vector2Int(0, 0), Vector2Int(0, 10));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].first, Vector2Int(0, 0));
    EXPECT_TRUE(result[0].second);
    EXPECT_EQ(result[1].first, Vector2Int(0, 3));
    EXPECT_FALSE(result[1].second);
}

namespace {
PolygonGrid2D make_l_shape() {
    PolygonGrid2DBuilder b;
    b.add_point(0, 0); b.add_point(0, 6); b.add_point(3, 6);
    b.add_point(3, 3); b.add_point(6, 3); b.add_point(6, 0);
    return b.build();
}
PolygonGrid2D make_plus_shape() {
    PolygonGrid2DBuilder b;
    b.add_point(0, 2); b.add_point(0, 4); b.add_point(2, 4);
    b.add_point(2, 6); b.add_point(4, 6); b.add_point(4, 4);
    b.add_point(6, 4); b.add_point(6, 2); b.add_point(4, 2);
    b.add_point(4, 0); b.add_point(2, 0); b.add_point(2, 2);
    return b.build();
}
} // anonymous

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_SquareAndL) {
    auto p1 = PolygonGrid2D::get_square(6);
    auto p2 = make_l_shape();
    auto line = OrthogonalLineGrid2D(Vector2Int(-2, 3), Vector2Int(5, 3));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].first, Vector2Int(-2, 3));
    EXPECT_TRUE(result[0].second);
    EXPECT_EQ(result[1].first, Vector2Int(3, 3));
    EXPECT_FALSE(result[1].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_SquareAndL2) {
    auto p1 = PolygonGrid2D::get_square(6);
    auto p2 = make_l_shape();
    auto line = OrthogonalLineGrid2D(Vector2Int(3, 5), Vector2Int(3, -2));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, Vector2Int(3, 2));
    EXPECT_TRUE(result[0].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_LAndL) {
    auto p1 = make_l_shape();
    auto p2 = make_l_shape();
    auto line = OrthogonalLineGrid2D(Vector2Int(-3, -5), Vector2Int(-3, 2));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, Vector2Int(-3, -2));
    EXPECT_TRUE(result[0].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_LAndL2) {
    auto p1 = make_l_shape();
    PolygonGrid2DBuilder b;
    b.add_point(0, 0); b.add_point(0, 9); b.add_point(3, 9);
    b.add_point(3, 3); b.add_point(6, 3); b.add_point(6, 0);
    auto p2 = b.build();
    auto line = OrthogonalLineGrid2D(Vector2Int(3, 8), Vector2Int(3, -2));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, Vector2Int(3, 2));
    EXPECT_TRUE(result[0].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_LAndL3) {
    auto p1 = make_l_shape();
    auto p2 = make_l_shape();
    auto line = OrthogonalLineGrid2D(Vector2Int(3, 5), Vector2Int(3, -2));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, Vector2Int(3, 2));
    EXPECT_TRUE(result[0].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_SquareAndL3) {
    auto p1 = PolygonGrid2D::get_square(6);
    PolygonGrid2DBuilder b;
    b.add_point(0, 0); b.add_point(0, 6); b.add_point(6, 6);
    b.add_point(6, 3); b.add_point(3, 3); b.add_point(3, 0);
    auto p2 = b.build();
    auto line = OrthogonalLineGrid2D(Vector2Int(3, 2), Vector2Int(3, -5));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].first, Vector2Int(3, 2));
    EXPECT_TRUE(result[0].second);
    EXPECT_EQ(result[1].first, Vector2Int(3, -3));
    EXPECT_FALSE(result[1].second);
}

TEST(DungeonTopologyGeneratorGeometry, OverlapAlongLine_ComplexCase) {
    auto p1 = make_plus_shape();
    PolygonGrid2DBuilder b;
    b.add_point(0, 0); b.add_point(0, 8); b.add_point(8, 8);
    b.add_point(8, 2); b.add_point(6, 2); b.add_point(6, 6);
    b.add_point(2, 6); b.add_point(2, 0);
    auto p2 = b.build();
    auto line = OrthogonalLineGrid2D(Vector2Int(0, -2), Vector2Int(15, -2));
    auto result = overlap_along_line_wrap(p1, p2, line);
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0].first, Vector2Int(0, -2));
    EXPECT_TRUE(result[0].second);
    EXPECT_EQ(result[1].first, Vector2Int(2, -2));
    EXPECT_FALSE(result[1].second);
    EXPECT_EQ(result[2].first, Vector2Int(3, -2));
    EXPECT_TRUE(result[2].second);
    EXPECT_EQ(result[3].first, Vector2Int(6, -2));
    EXPECT_FALSE(result[3].second);
}

// --- B-T8: HopcroftKarp matching ---

TEST(DungeonTopologyGeneratorGeometry, HopcroftKarp_OneToMany_ReturnsOne) {
    std::vector<std::pair<int, int>> edges{{0, 0}, {0, 1}, {0, 2}};
    auto matching = hopcroft_karp_max_matching(1, 3, edges);
    EXPECT_EQ(matching.size(), 1u);
    std::set<int> left_seen, right_seen;
    for (auto& [u, v] : matching) {
        EXPECT_EQ(left_seen.count(u), 0u);
        EXPECT_EQ(right_seen.count(v), 0u);
        left_seen.insert(u);
        right_seen.insert(v);
    }
}

TEST(DungeonTopologyGeneratorGeometry, HopcroftKarp_EightVertices_ReturnsFour) {
    std::vector<std::pair<int, int>> edges{
        {0, 1}, {0, 2}, {1, 0}, {2, 1}, {3, 1}, {3, 3}
    };
    auto matching = hopcroft_karp_max_matching(4, 4, edges);
    EXPECT_EQ(matching.size(), 4u);
    std::set<int> left_seen, right_seen;
    for (auto& [u, v] : matching) {
        EXPECT_EQ(left_seen.count(u), 0u);
        EXPECT_EQ(right_seen.count(v), 0u);
        left_seen.insert(u);
        right_seen.insert(v);
    }
}

TEST(DungeonTopologyGeneratorGeometry, HopcroftKarp_CompleteGraph_ReturnsFive) {
    int n_left = 5, n_right = 6;
    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < n_left; ++i)
        for (int j = 0; j < n_right; ++j)
            edges.push_back({i, j});
    auto matching = hopcroft_karp_max_matching(n_left, n_right, edges);
    EXPECT_EQ(matching.size(), 5u);
    std::set<int> left_seen, right_seen;
    for (auto& [u, v] : matching) {
        EXPECT_EQ(left_seen.count(u), 0u);
        EXPECT_EQ(right_seen.count(v), 0u);
        left_seen.insert(u);
        right_seen.insert(v);
    }
}
