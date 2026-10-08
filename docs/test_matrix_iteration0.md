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
| `Edgar.GeneralAlgorithmsTests/Algorithms/Common/OrthogonalLineIntersectionTests.cs` | full | `EdgarGeometry.LineIntersection_*`, `PartitionByIntersection_*`, `RemoveIntersections_*`, `OverlapAlongLine_*` (`edgar_parity_tests.cpp`); `OverlapAlongLine_*` (`edgar_tests.cpp`) | Крупный файл C#; C++ разбит на множество `TEST` | done |
| `.../Algorithms/Polygons/GridPolygonOverlapTests.cs` | full | `EdgarGeometry.Overlap_*`, `OverlapArea_*`, `PolygonsOverlap_*` (`edgar_parity_tests.cpp`, `edgar_tests.cpp`) | | done |
| `.../Algorithms/Polygons/GridPolygonPartitioningTests.cs` | full | `EdgarGeometry.GridPolygonPartitioning_*` (`edgar_tests.cpp`) | | done |
| `.../Algorithms/Polygons/GridPolygonUtilsTests.cs` | partial | `NormalizePolygon_*` (`edgar_parity_tests.cpp`) | Узкий файл в C# | done |
| `.../Algorithms/Graphs/BipartiteCheckTests.cs` | full | `EdgarGraphs.IsBipartite_*` (`edgar_parity_tests.cpp`); `BipartiteVertexCover_*`, `BipartiteIndependentSet_*` (`edgar_tests.cpp`) | | done |
| `.../Algorithms/Graphs/GraphUtilsTests.cs` | full | `IsConnected_*`, `IsTree_*`, `IsPlanar_*`, `GetCycles_*` (`edgar_parity_tests.cpp`); `IsTree_pathAndTriangle` (`edgar_tests.cpp`) | | done |
| `.../Algorithms/Graphs/HopcroftKarpTests.cs` | full | `EdgarGeometry.HopcroftKarp_*` (`edgar_parity_tests.cpp`) | | done |
| `.../DataStructures/Common/IntVector2Tests.cs` | full | `EdgarUtils.Vector2Int_Transform_All8` (`edgar_parity_tests.cpp`) | | done |
| `.../DataStructures/Common/OrthogonalLineTests.cs` | full | `EdgarUtils.OrthogonalLine_*` (`edgar_parity_tests.cpp`); `OrthogonalLineShrink_horizontal` (`edgar_tests.cpp`) | | done |
| `.../DataStructures/Common/SimpleBitVector32Tests.cs` | none | — | Нет аналога в `edgar` | skip (na): `SimpleBitVector32` не портируется в ядро |
| `.../DataStructures/Graphs/GraphTests.cs` | partial | `EdgarGraphs` базовые (`edgar_parity_tests.cpp`) | C# `IntGraph` отдельно | done |
| `.../DataStructures/Graphs/IntGraphTests.cs` | partial | косвенно через chain/generator | Полный перенос `IntGraph` не требуется для grid2d MVP | done |
| `.../DataStructures/Graphs/UndirectedAdjacencyListGraphTests.cs` | partial | `EdgarGraphs.AddVertexDuplicate_Throws` и др. (`edgar_parity_tests.cpp`) | C# файл минимальный | done |
| `.../DataStructures/Polygons/GridPolygonTests.cs` | full | `EdgarGeometry.Polygon*` (`edgar_parity_tests.cpp`) | | done |

---

## Edgar.Tests

| upstream_file | coverage | cpp_TEST (файл) | notes | status |
|---------------|----------|-----------------|------|--------|
| `Edgar.Tests/Core/ConfigurationSpaces/CSGeneratorTests.cs` | full | `EdgarConfigSpaceCsharpParity.TwoSquares_SimpleDoorMode_ExactPoints` (`edgar_parity_tests.cpp`) | Все тесты upstream закомментированы (legacy `ConfigurationSpacesGeneratorOld`); ожидаемые множества точек из комментариев портированы как численный паритет | done |
| `Edgar.Tests/Core/ConfigurationSpaces/ConfigurationSpacesGeneratorTests.cs` | full | `EdgarConfigSpaceCsharpParity.*` (`edgar_parity_tests.cpp`) | Точные множества точек КП для коридоров (vertical/horizontal/combined/length-zero/degenerated/L-shaped) и `GetRoomTemplateInstances` с дедупликацией симметрий; ignored-тест upstream не переносился | done |
| `Edgar.Tests/Core/Doors/DoorUtilsTests.cs` | full | `EdgarDoors.MergeDoorLines_CorrectlyMerges` (`edgar_parity_tests.cpp`) | | done |
| `Edgar.Tests/Core/Doors/OverlapModeHandlerTests.cs` | full | `EdgarDoorsCsharpParity.OverlapMode_Rectangle_*` (`edgar_parity_tests.cpp`) | Все 5 сценариев с точными (from,to,direction,length) | done |
| `Edgar.Tests/Core/Doors/SpecificPositionsModeHandlerTests.cs` | full | `EdgarDoorsCsharpParity.SpecificPositions_Rectangle_*` (`edgar_parity_tests.cpp`) | Точечные дверные линии, углы дают по 2 двери | done |
| `Edgar.Tests/Core/GraphDecomposition/ChainDecomposersTests.cs` | full | `EdgarChainDecomposition.*` (`edgar_tests.cpp`) | | done |
| `Edgar.Tests/Core/MapDescriptions/MapDescriptionTests.cs` | full | `EdgarLevelDescription.*` (`edgar_parity_tests.cpp`) | Grid2D API отличается от C# `MapDescription`; сценарии дубликатов/валидации коридоров покрыты | done |
| `Edgar.Tests/Grid/ConfigurationSpaceGeneratorTests.cs` | full | `EdgarConfigSpaceCsharpParity.TwoSquares_SimpleDoorMode_ExactPoints` | Файл upstream полностью закомментирован (устаревший `ConfigSpacesGenerator`); ожидания из комментариев покрыты | done |
| `Edgar.Tests/Grid/ConfigurationSpacesTests.cs` | full | — | Файл upstream полностью закомментирован (устаревший `GetMaximumIntersection` API); активных тестов нет | done |
| `Edgar.Tests/Utils/GraphAnalysis/CycleClustersAnalyzerTests.cs` | none | — | Нет модуля в порте | skip (na): graph analysis вне скоупа ядра |
| `Edgar.Tests/Utils/GraphAnalysis/GraphAnalysisUtilsTests.cs` | none | — | Нет модуля в порте | skip (na): graph analysis вне скоупа ядра |
| `Edgar.Tests/Utils/Statistics/EntropyCalculatorTests.cs` | none | — | Нет в порте | skip (na): entropy не в порте |

---

## Edgar.IntegrationTests

| upstream_file | coverage | cpp_TEST (файл) | notes | status |
|---------------|----------|-----------------|------|--------|
| `Edgar.IntegrationTests/Core/ConfigurationSpaces/ConfigurationSpacesGeneratorTests.cs` | full | `EdgarConfigSpaceCsharpParity.Generate_BasicTest_ShapeCountsPerNode` (`edgar_parity_tests.cpp`) | Дедупликация симметричных трансформаций шаблонов (square→1, rect→2, всего 3) | done |
| `Edgar.IntegrationTests/Core/LayoutGenerators/DungeonGeneratorTests.cs` | partial | `EdgarGenerator.*` + `EdgarIntegration.DungeonGenerator_*` (`edgar_tests.cpp`) | Нет `DungeonGenerator` 1:1; инварианты pipeline (граф → layout, нет overlap, детерминизм JSON) | done |
| `Edgar.IntegrationTests/Core/LayoutOperations/RoomShapesHandlerTests.cs` | full | `EdgarRoomShapesCsharpParity.*` (`edgar_tests.cpp`) | Все 8 сценариев (AllowRepeat/NoRepeat/NoImmediate/override/tryToFixEmpty/corridors) портированы на `RoomShapesHandlerGrid2D::possible_shapes_for_room` | done |
| `Edgar.IntegrationTests/Core/MapDescriptions/MapDescriptionMappingTests.cs` | full | `EdgarMappingCsharpParity.*` (`edgar_tests.cpp`) | Включая контрактацию коридоров в `get_stage_one_graph()` (C# `GetStageOneGraph`) | done |
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

Обновлять эту матрицу при добавлении значимых `TEST` в [`edgar_tests.cpp`](../src/tests/edgar_tests.cpp) / [`edgar_parity_tests.cpp`](../src/tests/edgar_parity_tests.cpp).