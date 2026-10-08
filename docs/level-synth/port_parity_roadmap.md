# Roadmap: приведение порта к соответствию с Edgar-DotNet

Цель **паритета** здесь — **архитектура и логика**, близкие к `GraphBasedGenerator` + Legacy layout-стеку C#: те же роли компонентов, порядок решений и проверяемое поведение. Побайтовое совпадение кода не требуется; сравнение с референсом логическое (портированные тесты, golden-сценарии). Полное совпадение выхода при фиксированном seed — **не** цель.

**Подробное описание текущих расхождений:** [port_vs_original_gap.md](port_vs_original_gap.md).

---

## Текущий статус (финал этапа H4)

- **Итерации 0–7 закрыты.** Матрица итерации 0 полностью разрешена: 26 done / 6 skip (na), blocked не осталось — см. [`test_matrix_iteration0.md`](test_matrix_iteration0.md).
- **Этапы H1–H4 (сходимость и производительность) закрыты:** планировщик вариантов цепей (DFS, аналог C# `GeneratorPlanner`), worklist-начальное размещение, инкрементальный SA по цепям (`active_rooms`), кэши КП / разбиений полигонов / дверных линий. Детали — в [parity_next_steps_plan.md](parity_next_steps_plan.md), секции H–H4.
- **225 тестов GTest зелёные** в Debug и Release; бенчмарк-гейты `python3 tools/benchmark_layout_generation.py --check` проходят.
- **Сходимость и скорость:** все bundled-карты сходятся (tutorial_basic 0.2 мс, 9vertices ~11 мс, tutorial_corridors ~0.2 с, dragonAge ~0.1 с, 17vertices ~0.4 с, 41vertices ~8 с), время сопоставимо с C#-референсом (41vertices ~8 с у обоих).
- Секции итераций ниже оставлены как исторический план; формулировки «Тесты после итерации» описывают то, что уже сделано.

---

## Итерация 0 — Базовая линия

**Статус:** done — матрица [`test_matrix_iteration0.md`](test_matrix_iteration0.md), golden-пайплайн `parity_golden_test` (3 сценария, логическое сравнение с C# через `tools/parity_runner_cs`, `.cs.json` эталоны в репозитории).

**План работ для агента (пошагово):** [iteration_0_agent_brief.md](iteration_0_agent_brief.md).

- Зафиксировать **Definition of Done** (например: события SA, суммы штрафов на эталонных уровнях, допустимые позиции из КП для пары шаблонов).
- Матрица тестов: `Edgar.Tests` / `GeneralAlgorithmsTests` / `IntegrationTests` (референс `_edgar_ref`) → существующие или новые `TEST` в C++.
- Опционально: golden-пайплайн (общий вход описания уровня → сравнение структуры layout C# и C++ с допусками).

**Тесты после итерации:** таблица «файл/класс C# → существующий `TEST` или TODO» в репозитории (например `docs/` или `src/tests/README`); чек-лист критериев DoD; при наличии скрипта — один smoke-прогон golden (пусть даже с ручным эталоном).

---

## Итерация 1 — Архитектура ограничений и энергии

**Статус:** done — фасад `ConstraintsEvaluatorGrid2D` (basic/corridor/min-distance), `optimize_corridor_constraints`, масштабирование `BasicEnergyUpdater` (`10 * averageSize`); тесты на констрейнты и инвариант суммы штрафов в `dungeon_topology_generator_tests.cpp`.

- Вынести констрейнты в **композицию** классов: Basic, Corridor, MinimumDistance + общий `ConstraintsEvaluator` + `BasicEnergyUpdater` с масштабом как в `GraphBasedGeneratorGrid2D` (например `10 * averageSize`), флаги вроде `OptimizeCorridorConstraints`.
- Расширить тесты на инварианты энергии по типам штрафов.

**Тесты после итерации:**

- Юнит-тесты на **каждый** констрейнт в изоляции (минимальные полигоны/позиции): вклад в `EnergyData` совпадает с ожиданием для overlap-only, corridor-only, min-distance-only.
- Инвариант **сумма по `incident_to_room` = 2 × total** (как сейчас `Incident_to_room_sumMatchesTwiceTotal`) — сохранить и расширить при раздельных констрейнтах.
- Сравнение **масштаба** энергии с эталоном (фиксированные формы и `averageSize`): регрессия численных значений `total_penalty` при тех же входах.
- Если в `_edgar_ref` есть прямые аналоги по смыслу — ориентир: сценарии из интеграционных тестов, где проверяются штрафы коридора/дистанции (см. использование `ConstraintsEvaluator` в C#).

*См. разделы «Архитектура» и «3.2 Упрощено» в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Итерация 2 — Mapping и RoomShapesHandler

**Статус:** done — `LevelDescriptionMappingGrid2D`, `RoomShapesHandlerGrid2D` (repeat/weights/alias), дефолт `NoRepeat` как в C#; портированные тесты `DungeonTopologyGeneratorMappingCsharpParity` (2) и `DungeonTopologyGeneratorRoomShapesCsharpParity` (8).

- Явный слой `LevelDescriptionMapping` (комната ↔ узел, описание, шаблоны).
- Логика `RoomShapesHandlerGrid2D`: repeat mode, веса `WeightedShape`, alias по смыслу как `IntAlias` / `TwoWayDictionary` в C#.
- Сконцентрировать разрозненную логику в именованных компонентах.

**Тесты после итерации:**

- Портировать **сценарии** из `Edgar.IntegrationTests` / `Core/LayoutOperations/RoomShapesHandlerTests.cs` (если есть в референсе): выбор шаблона, repeat mode, смена формы при фиксированном графе.
- Тесты на **маппинг**: `MapDescriptionMappingTests` (или эквивалент) — комната ↔ индекс, согласованность с `LevelDescriptionGrid2D` и графом.
- Тесты на **веса и alias**: один узел — несколько инстансов шаблона; после выбора alias корректная связь с `WeightedShape` / энергией (если применимо).

*См. «2. Архитектура» и «3.2» в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Итерация 3 — Simulated Annealing

**Статус:** done (этапы 3 + H1/H3) — основной путь через `LayoutControllerGrid2D` и **точное пересечение КП с релаксацией подмножеств**; инкрементальный SA по цепям (`active_rooms`, парковка неактивных), `simulated_annealing_max_branching = 5`; детерминизм и допустимость позиций покрыты тестами.

- Основной путь **perturbation** — через контроллер и **configuration spaces**, как в C#, а не только `max_perturbation_radius`.
- Единый согласованный эволютор в публичном API.

**Тесты после итерации:**

- Регрессия существующих `TEST` в `dungeon_topology_generator_tests.cpp` (цепочка, коридоры, SA-события, детерминизм) — **все зелёные** после изменения perturb.
- Новые тесты: **детерминизм** при фиксированном RNG и одинаковом порядке инъекций в контроллер/КП/эволютор (как минимум два прогона с одним seed дают идентичный layout или идентичную последовательность событий).
- Тесты на **допустимость позиций**: после шага perturb позиция остаётся в объединении КП с соседями (выборка на нескольких микро-уровнях из референса).
- Опционально: расширить **точечные** численные совпадения с C# (в духе `OverlapAlongLine_TwoRectsMatchCsharp`) для этапа SA, если появится общий входной формат.

*См. «3.2 Упрощено» (SA) в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Итерация 4 — Двери

**Статус:** done — `SimpleDoorModeGrid2D` (overlap), `ManualDoorModeGrid2D` (specific positions) с C#-представлением точечных линий (`degeneratedDirection` у `OrthogonalLineGrid2D`); портированные тесты `DungeonTopologyGeneratorDoorsCsharpParity` (7) из `OverlapModeHandlerTests` / `SpecificPositionsModeHandlerTests`. Отдельный legacy-реестр `DoorHandler` не переносится (не блокер).

- Реализовать стратегии **overlap** и **specific positions** (и при необходимости manual), по тестам `OverlapModeHandlerTests`, `SpecificPositionsModeHandlerTests`.
- Связка с генерацией КП как у `DoorHandler` в C#.

**Тесты после итерации:**

- Портировать кейсы из **`OverlapModeHandlerTests.cs`** и **`SpecificPositionsModeHandlerTests.cs`** (`_edgar_ref/src/Edgar.Tests/Core/Doors/`) — те же входные дверные линии/полигоны и ожидаемые множества допустимых позиций или дверей.
- Регрессия **`DoorUtilsTests`** / `MergeDoorLines` — уже частично в `dungeon_topology_generator_parity_tests`; дополнить под новые режимы.
- Интеграционный тест: генерация КП с **не-simple** handler'ом и проверка успешного layout на маленьком графе.

*См. «3.3 Отсутствует» (двери) в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Итерация 5 — Жизненный цикл API генератора

**Статус:** реализовано в `GraphBasedGeneratorConfiguration` / `GraphBasedGeneratorGrid2D`, `ChainGenerateContext`, `LayoutControllerGrid2D`, strip path; контракт и отличия от C# — §2 в [port_vs_original_gap.md](port_vs_original_gap.md).

- Ранняя остановка (итерации / время), отмена (аналог `CancellationToken`).
- Выравнивание событий с `GraphBasedGeneratorGrid2D` (`OnValid`, `OnPartialValid`, `OnPerturbed`, `OnSimulatedAnnealingEvent`).

**Тесты после итерации:**

- **Early stop:** при лимите итераций генерация завершается без исключения; при превышении лимита времени (мок часов или таймера) — отмена/стоп.
- **Отмена:** после `cancel()` (или аналога) очередной шаг генерации не выполняется; состояние корректно.
- **События:** таблица соответствия «тип события C# → callback в C++» покрыта тестами (порядок и минимум один вызов на эталонном уровне).
- Регрессия: существующие тесты стриминга (`Chain_yieldStream_*`, `RandomRestart_*`) остаются зелёными.

Покрытие в `dungeon_topology_generator_tests.cpp`: `GraphBasedGenerator_earlyStopMaxIterations_chain`, `GraphBasedGenerator_earlyStopElapsed_mockClock_chain`, `GraphBasedGenerator_cooperativeCancel_thenReset`, `GraphBasedGenerator_cancelExclusiveWithEarlyStop`, `GraphBasedGenerator_lifecycleCallbacks_chain`, `GraphBasedGenerator_strip_earlyStopElapsed_partialLayout`.

*См. «2. Архитектура» в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Итерация 6 — Конвертер layout

**Статус:** реализовано — `BasicLayoutConverterGrid2D` в [`basic_layout_converter_grid2d.hpp`](../src/libs/dungeon_topology_generator/include/dungeon_topology_generator/generator/grid2d/basic_layout_converter_grid2d.hpp), делегирование из `Grid2DLayoutState::to_layout_grid`, `make_room` для strip; экспорт через [`dungeon_topology_generator.hpp`](../src/libs/dungeon_topology_generator/include/dungeon_topology_generator/dungeon_topology_generator.hpp).

- Выделить `BasicLayoutConverterGrid2D`: граница между внутренним состоянием цепи/конфигураций и публичным `LayoutGrid2D`.

**Тесты после итерации:**

- Юнит-тесты конвертера: **round-trip** или «внутренний layout → `LayoutGrid2D`» с фиксированными мок-данными; сравнение полей `LayoutRoomGrid2D` (outline, position, doors при наличии).
- Тест на **идемпотентность** или стабильность: повторная конвертация того же внутреннего состояния даёт тот же публичный layout.
- Интеграция: один сценарий из `dungeon_topology_generator_tests` проходит через публичный API с выделенным конвертером без регрессии JSON/room count.

Покрытие в `dungeon_topology_generator_tests.cpp`: `DungeonTopologyGeneratorLayoutConverter.BasicLayoutConverter_matchesToLayoutGrid` (в т.ч. сравнение JSON), `BasicLayoutConverter_idempotent`, `BasicLayoutConverter_addDoors_matchesStandaloneCompute`, `BasicLayoutConverter_makeRoom_stripParity`.

---

## Итерация 7 — Интеграция и перфоманс

**Статус:** done — матрица итерации 0 закрыта полностью (26 done / 6 skip (na)) в [`test_matrix_iteration0.md`](test_matrix_iteration0.md); интеграционные инварианты pipeline в `DungeonTopologyGeneratorIntegration.DungeonGenerator_*` ([`dungeon_topology_generator_tests.cpp`](../src/tests/dungeon_topology_generator_tests.cpp)); производительность — [`tools/benchmark_layout_generation.py`](../tools/benchmark_layout_generation.py) с CI-гейтами `--check`, паритет по времени с C#-референсом подтверждён на bundled-картах (см. [parity_next_steps_plan.md](parity_next_steps_plan.md), секция H4).

- Портировать ключевые `Edgar.IntegrationTests` по мере необходимости.
- Опционально: слой performance-тестов на эталонных картах.
- Закрыть матрицу из итерации 0.

**Тесты после итерации:**

- Перенос **ключевых** `Edgar.IntegrationTests`: `DungeonGeneratorTests`, сценарии с полным pipeline (по возможности — те же входные `MapDescription`/уровни).
- Закрытие **матрицы** из итерации 0: все строки «C# тест → C++ тест» имеют статус done или явный `SKIP` с причиной.
- **Performance (опционально):** отдельная цель или скрипт — время генерации на 1–2 эталонных пресетах не хуже базового порога (регрессия при оптимизациях).

Покрытие: `DungeonTopologyGeneratorIntegration.DungeonGenerator_pathGraph_pipelineNoOverlap`, `DungeonGenerator_branchGraph_pipelineNoOverlap`, `DungeonGenerator_sameSeedDeterministicLayoutJson`.

---

## Вне скоупа паритета ядра (по умолчанию)

Meta-optimization, evolution sandbox, Unity build, platformers generator, backtracking prototype, entropy / graph analysis — только при отдельном продуктовом запросе. Эти направления осознанно не переносятся.

*См. «3.3» и «5» в [port_vs_original_gap.md](port_vs_original_gap.md).*

---

## Порядок работ и риски

- Все итерации 0–7 и этапы H1–H4 завершены; документ сохранён как исторический план.
- Крупные рефакторинги (1–3) ломали тесты — зелёный прогон `dungeon_topology_generator_tests` / `dungeon_topology_generator_parity_tests` поддерживался после каждой итерации и остаётся обязательным гейтом (225/225, обе конфигурации).
- Паритет **seed → layout** (побитовое совпадение выхода при фиксированном seed) не достигнут и не является целью; критерий паритета — логическое совпадение поведения (портированные тесты + golden-сценарии + сопоставимые время/сходимость).
