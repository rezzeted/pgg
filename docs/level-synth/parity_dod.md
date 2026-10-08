# Definition of Done (итерация 0, паритет с C#)

Чек-лист проверяемых утверждений для порта **до** глубокого архитектурного паритета. Каждый критерий должен иметь тест в CI (`ctest`) или явное **TBD** с номером будущей итерации roadmap.

| ID | Критерий | Проверка (C++ тест) |
|----|----------|---------------------|
| **D1** | При фиксированном seed генератор даёт **воспроизводимый** результат на эталонных сценариях | `EdgarGolden.DeterministicGeneration_SameSeedSameOutput`, `DeterministicGeneration_DifferentSeedDifferentOutput`, `Xorshift64star_DeterministicSequence` в `edgar_tests.cpp` |
| **D2** | Graph-based генерация **без overlap** комнат на эталонных графах (цикл, звезда, коридор) | `EdgarGenerator.FourRoomCycle`, `FourRoomCycle_stripBackend`, `Chain_threeRoomsWithCorridor_lineGraph`, `SixRoomStarGraph_noOverlap`, `CorridorWithDoors_noOverlapAndValidLayout` в `edgar_tests.cpp` |
| **D3** | События оркестрации SA **срабатывают** при заданных настройках (restart, stage two failure, out of iterations, stream) | `EdgarSA.RandomRestart_triggersOnHighFailures`, `StageTwoFailure_incrementsInStream`, `OutOfIterations_emittedWhenNoLayoutFound`, `EdgarGenerator.StreamMode_OnEachLayoutGenerated_countsEvents`, `Chain_yieldStream_matchesSingleAndCountsEvents` в `edgar_tests.cpp` |
| **D4** | Энергия: **нулевой** суммарный штраф при корректных непересекающихся позициях; **инвариант** суммы incident vs total | `EdgarEnergy.ConstraintsEvaluator_noOverlapZeroPenalty`, `Incident_to_room_sumMatchesTwiceTotal` в `edgar_tests.cpp` |
| **D5** | **Configuration spaces:** непустота КП / совместимые позиции в базовых кейсах | `EdgarConfigSpaces.ConfigurationSpacesGenerator_nonEmptyForMatchingSquares`, `CompatibleNonOverlapping_twoRects` в `edgar_tests.cpp`; блок `EdgarConfigSpace::*` в `edgar_parity_tests.cpp` |
| **D6** | **Точечный численный контакт** с C# (одна из геометрических процедур) | `EdgarGeometry.OverlapAlongLine_TwoRectsMatchCsharp` в `edgar_tests.cpp` |
| **D7** | Публичный `LayoutGrid2D` собирается через **конвертер** из `Grid2DLayoutState` (идемпотентность, опционально двери) | `EdgarLayoutConverter.BasicLayoutConverter_*` в `edgar_tests.cpp` |
| **D8** | **Матрица** C#→C++ ([`test_matrix_iteration0.md`](test_matrix_iteration0.md)) закрыта статусами; интеграционный pipeline без overlap / детерминизм JSON | колонка `status` в матрице; `EdgarIntegration.DungeonGenerator_*` в `edgar_tests.cpp` |

## Эталонные сценарии для D2/D3 (имена `TEST`)

- Четырёхкомнатный цикл с двумя шаблонами комнат: `FourRoomCycle`, `FourRoomCycle_stripBackend`.
- Линия из трёх комнат с коридором: `Chain_threeRoomsWithCorridor_lineGraph`.
- Звезда из шести комнат: `SixRoomStarGraph_noOverlap`.
- Коридор с дверями: `CorridorWithDoors_noOverlapAndValidLayout`.

## TBD (вне итерации 0)

- Полное покрытие дверных режимов overlap / specific (C# `OverlapModeHandlerTests`) — итерация 4 roadmap.
- Паритет seed→layout с исполняемым C# — опционально после golden-пайплайна (`test_data/parity/`).

## Итерация 5 — события и жизненный цикл (сопоставление с C#)

| C# (GraphBasedGenerator / evolver) | C++ API |
|-------------------------------------|---------|
| `OnSimulatedAnnealingEvent` | `set_on_simulated_annealing_event` + `LayoutYieldInfo`; дублирует по смыслу часть данных stream-колбэка |
| `OnValid` | `set_on_valid` после валидного полного layout в конце успешного restart |
| `OnPartialValid` | `set_on_partial_valid` при нулевом overlap в SA до шага Metropolis |
| `OnPerturbed` | `set_on_perturbed` после принятого perturb (Metropolis accept) |
| `SetCancellationToken` / ранняя остановка | `request_cancel` / `reset_cancellation` и `early_stop_*` в конфиге (взаимное исключение см. §2 gap) |

Тесты: `GraphBasedGenerator_*` в `edgar_tests.cpp` (см. roadmap итерации 5).

## Итерация 6 — конвертер layout

| C# | C++ |
|----|-----|
| `BasicLayoutConverterGrid2D.Convert(ILayout, addDoors)` | `BasicLayoutConverterGrid2D::convert` / `convert(..., add_doors, rng)`; без полного паритета по alias/случайным трансформациям из mapping |

Тесты: `EdgarLayoutConverter::*` в `edgar_tests.cpp`.

## Итерация 7 — интеграция и матрица

| C# | C++ |
|----|-----|
| `Edgar.IntegrationTests` / `DungeonGeneratorTests` (инварианты pipeline) | `EdgarIntegration.DungeonGenerator_*`; полного класса `DungeonGenerator` в порте нет |

Ручной замер времени (не CI-gate): `tools/benchmark_layout_generation.ps1`.

См. также: [test_matrix_iteration0.md](test_matrix_iteration0.md), [port_parity_roadmap.md](port_parity_roadmap.md).
