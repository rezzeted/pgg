# Матрица тестов: Edgar-DotNet (`_edgar_ref`) → порт LevelSynth (итерация 0)

**Легенда coverage:** `full` — основные сценарии файла отражены в C++; `partial` — частично; `none` — нет близкого покрытия; `na` — функционал в порте не заявлен (см. [port_vs_original_gap.md](port_vs_original_gap.md)).

**Легенда status (закрытие итерации 7):**

| status | смысл |
|--------|--------|
| **done** | для порта есть подходящие `TEST`; соответствие C# по смыслу зафиксировано в `cpp_TEST` / примечании |
| **blocked (N)** | полный паритет ждёт **итерацию N** роадмапа ([port_parity_roadmap.md](port_parity_roadmap.md)) |
| **skip (na)** | вне скоупа ядра; явная причина в колонке |

Пути upstream от корня: `_edgar_ref/src/`.

---

## Edgar.GeneralAlgorithmsTests

| upstream_file | coverage | cpp_TEST (файл) | notes | status |
|---------------|----------|-----------------|------|--------|
| `Edgar.GeneralAlgorithmsTests/Algorithms/Common/OrthogonalLineIntersectionTests.cs` | full | `DungeonTopologyGeneratorGeometry.LineIntersection_*`, `PartitionByIntersection_*`, `RemoveIntersections_*`, `OverlapAlongLine_*` (`dungeon_topology_generator_parity_tests.cpp`); `OverlapAlongLine_*` (`dungeon_topology_generator_tests.cpp`) | Крупный файл C#; C++ разбит на множество `TEST` | done |
| `.../Algorithms/Polygons/GridPolygonOverlapTests.cs` | full | `DungeonTopologyGeneratorGeometry.Overlap_*`, `OverlapArea_*`, `PolygonsOverlap_*` (`dungeon_topology_generator_parity_tests.cpp`, `dungeon_topology_generator_tests.cpp`) | | done |
| `.../Algorithms/Polygons/GridPolygonPartitioningTests.cs` | full | `DungeonTopologyGeneratorGeometry.GridPolygonPartitioning_*` (`dungeon_topology_generator_tests.cpp`) | | done |
| `.../Algorithms/Polygons/GridPolygonUtilsTests.cs` | partial | `NormalizePolygon_*` (`dungeon_topology_generator_parity_tests.cpp`) | Узкий файл в C# | done |
| `.../Algorithms/Graphs/BipartiteCheckTests.cs` | full | `DungeonTopologyGeneratorGraphs.IsBipartite_*` (`dungeon_topology_generator_parity_tests.cpp`); `BipartiteVertexCover_*`, `BipartiteIndependentSet_*` (`dungeon_topology_generator_tests.cpp`) | | done |
| `.../Algorithms/Graphs/GraphUtilsTests.cs` | full | `IsConnected_*`, `IsTree_*`, `IsPlanar_*`, `GetCycles_*` (`dungeon_topology_generator_parity_tests.cpp`); `IsTree_pathAndTriangle` (`dungeon_topology_generator_tests.cpp`) | | done |
| `.../Algorithms/Graphs/HopcroftKarpTests.cs` | full | `DungeonTopologyGeneratorGeometry.HopcroftKarp_*` (`dungeon_topology_generator_parity_tests.cpp`) | | done |
| `.../DataStructures/Common/IntVector2Tests.cs` | full | `DungeonTopologyGeneratorUtils.Vector2Int_Transform_All8` (`dungeon_topology_generator_parity_tests.cpp`) | | done |
| `.../DataStructures/Common/OrthogonalLineTests.cs` | full | `DungeonTopologyGeneratorUtils.OrthogonalLine_*` (`dungeon_topology_generator_parity_tests.cpp`); `OrthogonalLineShrink_horizontal` (`dungeon_topology_generator_tests.cpp`) | | done |
| `.../DataStructures/Common/SimpleBitVector32Tests.cs` | none | — | Нет аналога в `dungeon_topology_generator` | skip (na): `SimpleBitVector32` не портируется в ядро |
| `.../DataStructures/Graphs/GraphTests.cs` | partial | `DungeonTopologyGeneratorGraphs` базовые (`dungeon_topology_generator_parity_tests.cpp`) | C# `IntGraph` отдельно | done |
| `.../DataStructures/Graphs/IntGraphTests.cs` | partial | косвенно через chain/generator | Полный перенос `IntGraph` не требуется для grid2d MVP | done |
| `.../DataStructures/Graphs/UndirectedAdjacencyListGraphTests.cs` | partial | `DungeonTopologyGeneratorGraphs.AddVertexDuplicate_Throws` и др. (`dungeon_topology_generator_parity_tests.cpp`) | C# файл минимальный | done |
| `.../DataStructures/Polygons/GridPolygonTests.cs` | full | `DungeonTopologyGeneratorGeometry.Polygon*` (`dungeon_topology_generator_parity_tests.cpp`) | | done |

---

## Edgar.Tests

| upstream_file | coverage | cpp_TEST (файл) | notes | status |
|---------------|----------|-----------------|------|--------|
| `Edgar.Tests/Core/ConfigurationSpaces/CSGeneratorTests.cs` | full | `DungeonTopologyGeneratorConfigSpaceCsharpParity.TwoSquares_SimpleDoorMode_ExactPoints` (`dungeon_topology_generator_parity_tests.cpp`) | Все тесты upstream закомментированы (legacy `ConfigurationSpacesGeneratorOld`); ожидаемые множества точек из комментариев портированы как численный паритет | done |
| `Edgar.Tests/Core/ConfigurationSpaces/ConfigurationSpacesGeneratorTests.cs` | full | `DungeonTopologyGeneratorConfigSpaceCsharpParity.*` (`dungeon_topology_generator_parity_tests.cpp`) | Точные множества точек КП для коридоров (vertical/horizontal/combined/length-zero/degenerated/L-shaped) и `GetRoomTemplateInstances` с дедупликацией симметрий; ignored-тест upstream не переносился | done |
| `Edgar.Tests/Core/Doors/DoorUtilsTests.cs` | full | `DungeonTopologyGeneratorDoors.MergeDoorLines_CorrectlyMerges` (`dungeon_topology_generator_parity_tests.cpp`) | | done |
| `Edgar.Tests/Core/Doors/OverlapModeHandlerTests.cs` | full | `DungeonTopologyGeneratorDoorsCsharpParity.OverlapMode_Rectangle_*` (`dungeon_topology_generator_parity_tests.cpp`) | Все 5 сценариев с точными (from,to,direction,length) | done |
| `Edgar.Tests/Core/Doors/SpecificPositionsModeHandlerTests.cs` | full | `DungeonTopologyGeneratorDoorsCsharpParity.SpecificPositions_Rectangle_*` (`dungeon_topology_generator_parity_tests.cpp`) | Точечные дверные линии, углы дают по 2 двери | done |
| `Edgar.Tests/Core/GraphDecomposition/ChainDecomposersTests.cs` | full | `DungeonTopologyGeneratorChainDecomposition.*` (`dungeon_topology_generator_tests.cpp`) | | done |
| `Edgar.Tests/Core/MapDescriptions/MapDescriptionTests.cs` | full | `DungeonTopologyGeneratorLevelDescription.*` (`dungeon_topology_generator_parity_tests.cpp`) | Grid2D API отличается от C# `MapDescription`; сценарии дубликатов/валидации коридоров покрыты | done |
| `Edgar.Tests/Grid/ConfigurationSpaceGeneratorTests.cs` | full | `DungeonTopologyGeneratorConfigSpaceCsharpParity.TwoSquares_SimpleDoorMode_ExactPoints` | Файл upstream полностью закомментирован (устаревший `ConfigSpacesGenerator`); ожидания из комментариев покрыты | done |
| `Edgar.Tests/Grid/ConfigurationSpacesTests.cs` | full | — | Файл upstream полностью закомментирован (устаревший `GetMaximumIntersection` API); активных тестов нет | done |
| `Edgar.Tests/Utils/GraphAnalysis/CycleClustersAnalyzerTests.cs` | none | — | Нет модуля в порте | skip (na): graph analysis вне скоупа ядра |
| `Edgar.Tests/Utils/GraphAnalysis/GraphAnalysisUtilsTests.cs` | none | — | Нет модуля в порте | skip (na): graph analysis вне скоупа ядра |
| `Edgar.Tests/Utils/Statistics/EntropyCalculatorTests.cs` | none | — | Нет в порте | skip (na): entropy не в порте |

---

## Edgar.IntegrationTests

| upstream_file | coverage | cpp_TEST (файл) | notes | status |
|---------------|----------|-----------------|------|--------|
| `Edgar.IntegrationTests/Core/ConfigurationSpaces/ConfigurationSpacesGeneratorTests.cs` | full | `DungeonTopologyGeneratorConfigSpaceCsharpParity.Generate_BasicTest_ShapeCountsPerNode` (`dungeon_topology_generator_parity_tests.cpp`) | Дедупликация симметричных трансформаций шаблонов (square→1, rect→2, всего 3) | done |
| `Edgar.IntegrationTests/Core/LayoutGenerators/DungeonGeneratorTests.cs` | partial | `DungeonTopologyGeneratorGenerator.*` + `DungeonTopologyGeneratorIntegration.DungeonGenerator_*` (`dungeon_topology_generator_tests.cpp`) | Нет `DungeonGenerator` 1:1; инварианты pipeline (граф → layout, нет overlap, детерминизм JSON) | done |
| `Edgar.IntegrationTests/Core/LayoutOperations/RoomShapesHandlerTests.cs` | full | `DungeonTopologyGeneratorRoomShapesCsharpParity.*` (`dungeon_topology_generator_tests.cpp`) | Все 8 сценариев (AllowRepeat/NoRepeat/NoImmediate/override/tryToFixEmpty/corridors) портированы на `RoomShapesHandlerGrid2D::possible_shapes_for_room` | done |
| `Edgar.IntegrationTests/Core/MapDescriptions/MapDescriptionMappingTests.cs` | full | `DungeonTopologyGeneratorMappingCsharpParity.*` (`dungeon_topology_generator_tests.cpp`) | Включая контрактацию коридоров в `get_stage_one_graph()` (C# `GetStageOneGraph`) | done |
| `Edgar.IntegrationTests/Utils/RoomExtensionsTests.cs` | none | — | Нет прямого аналога | skip (na): `RoomExtensions` не портируется |
| `Edgar.IntegrationTests/Utils/Statistics/EntropyCalculatorTests.cs` | none | — | Нет в порте | skip (na): entropy не в порте |

---

## Сводка по coverage (как в итерации 0)

| coverage | count (файлов *Tests.cs) |
|----------|-------------------------|
| full | 20 |
| partial | 6 |
| none | 6 |
| **total** | **32** |

Источники: `Edgar.GeneralAlgorithmsTests` — 14 файлов; `Edgar.Tests` — 12 файлов; `Edgar.IntegrationTests` — 6 файлов.

---

## Сводка по status (итерация 7)

| status | count |
|--------|------|
| done | 26 |
| skip (na) | 6 |
| **total** | **32** |

Каждая строка матрицы имеет ровно один из статусов выше (критерий роадмапа итерации 7).

Обновлять эту матрицу при добавлении значимых `TEST` в [`dungeon_topology_generator_tests.cpp`](../src/tests/dungeon_topology_generator_tests.cpp) / [`dungeon_topology_generator_parity_tests.cpp`](../src/tests/dungeon_topology_generator_parity_tests.cpp).