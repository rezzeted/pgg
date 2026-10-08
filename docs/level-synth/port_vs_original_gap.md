# Расхождение порта LevelSynth и оригинала Edgar-DotNet

Документ описывает **текущее** состояние: что в C++-порте библиотеки `edgar` совпадает с оригиналом по смыслу, что упрощено, чего нет. Оригинал — репозиторий [Edgar-DotNet](https://github.com/OndrejNepozitek/Edgar-DotNet) (локально копия в `_edgar_ref`, коммит `258c83a`). Построчный diff не предполагается; сравнение логическое.

Статус на конец этапа H4: матрица тестов итерации 0 закрыта полностью (26 done / 6 skip (na), blocked не осталось), 225 тестов GTest зелёные в Debug и Release, бенчмарк-гейты `tools/benchmark_layout_generation.py --check` проходят, сходимость достигнута на всех bundled-картах (включая 41vertices), время генерации сопоставимо с C#-референсом.

---

## 1. Продукт и окружение

| Аспект | Оригинал | Порт |
|--------|----------|------|
| Стек | C# / .NET, решение `EdgarDotNet.sln` | C++20, CMake, vcpkg; сборки macOS (Debug/Release пресеты), Windows, Linux |
| Демо / редактор | WPF `Edgar.GUI`, примеры, песочницы | Одно приложение SDL3 + ImGui (`src/apps/main`) |
| Unity | Проект `Edgar.UnityBuild` | Нет |
| Производительность | `Edgar.PerformanceTests` (BenchmarkDotNet, ручной запуск) | `tools/benchmark_layout_generation.py` с гейтами `--check` по bundled-картам |
| Дополнительно в порте | — | YAML-пресеты, экспорт layout в JSON, свой `layout_json` |

---

## 2. Архитектура

**Оригинал** явно разделяет:

- `Legacy` (configuration spaces, doors, chain decomposition, layout evolvers, generators),
- `GraphBasedGenerator` (mapping уровня, констрейнты, `RoomShapesHandler`, `LayoutController`, конвертер layout),
- `GeneralAlgorithms` (геометрия, графы).

Много интерфейсов, инъекция `Random` в несколько компонентов, **early stopping** и **CancellationToken**, события генератора (`OnValid`, `OnPerturbed`, и т.д.).

**Порт** консолидирует основной поток в `src/libs/edgar/include/edgar/generator/grid2d/`: `ChainBasedGeneratorGrid2D`, `LayoutControllerGrid2D`, сводный `ConstraintsEvaluatorGrid2D`. Отдельных **плагинов** констрейнтов как в C# нет — штрафы считаются в одном месте.

**Жизненный цикл:** в `GraphBasedGeneratorConfiguration` — опциональные `early_stop_max_total_iterations` и `early_stop_max_elapsed` плюс инжектируемые `steady_clock_now`; у `GraphBasedGeneratorGrid2D` — `request_cancel` / `reset_cancellation` (совместимость с early-stop по правилам C#: при лимитах итераций/времени отмена недоступна и `request_cancel` бросает `std::logic_error`), колбэки `set_on_valid`, `set_on_partial_valid`, `set_on_perturbed`, `set_on_simulated_annealing_event`. Бюджет и отмена проверяются в цепочке (`ChainGenerateContext::poll_abort`), в SA и в цикле strip-pack. При досрочном выходе цепочка возвращает **пустой** `LayoutGrid2D`, если состояние ещё не приведено к полной конвертируемой раскладке; иначе — текущий снимок.

**Планировщик цепей (этап H1):** `ChainBasedGeneratorGrid2D` перебирает варианты цепей DFS-обходом дерева вариантов — аналог C# `GeneratorPlanner` / `ChainTree`; при пустой выдаче evolve пробует следующую ветку, лимит рестартов 256. Начальное размещение — worklist-очередь (как C# `InitialLayout`), не бросает исключение на плотных картах (раньше падало на 41vertices).

**Mapping и формы комнат (итерация 2, доведено до паритета):** `LevelDescriptionMappingGrid2D` и `RoomShapesHandlerGrid2D` (repeat/weights/alias) покрыты портированными C#-тестами (`EdgarMappingCsharpParity`, `EdgarRoomShapesCsharpParity`). Дефолт `room_template_repeat_mode_default = NoRepeat`, как в C#. Внутренние C#-типы (`IntAlias`, `TwoWayDictionary`) 1:1 не воспроизводятся — поведение совпадает на уровне тестов.

**События C# → C++ (кратко):** `OnSimulatedAnnealingEvent` — `LayoutYieldInfo` через `on_simulated_annealing_event`; `OnValid` — после успешного прохода с `penalty <= 0` в конце restart-цикла; `OnPartialValid` — при нулевом overlap после шага perturb в SA до Metropolis; `OnPerturbed` — после принятого шага Metropolis; прежний поток раскладок — `set_layout_yield_callback` + `LayoutStreamMode`.

В порт добавлен **альтернативный бэкенд** `strip_packing` в `GraphBasedGeneratorGrid2D` — горизонтальная укладка без SA; в оригинале как отдельный основной путь не выделен.

**Интеграция и матрица тестов:** для каждого файла `*Tests.cs` из матрицы итерации 0 зафиксирован статус (`done`, `skip (na)` для вне скоупа ядра) в [`test_matrix_iteration0.md`](test_matrix_iteration0.md) — все строки закрыты. Интеграционные сценарии в духе `Edgar.IntegrationTests` / `DungeonGeneratorTests` покрываются suite `EdgarIntegration` в `edgar_tests.cpp` (полный класс `DungeonGenerator` из C# не портируется 1:1). Замер производительности — `tools/benchmark_layout_generation.py` с порогами для CI (`--check`).

**Конвертер layout:** `BasicLayoutConverterGrid2D` в `basic_layout_converter_grid2d.hpp` — граница между внутренним `Grid2DLayoutState` и публичным `LayoutGrid2D`: `convert(state)`; опционально `convert(state, add_doors, rng)` через `compute_layout_doors`. Полный C#-конвертер также учитывает IntAlias/случайные трансформации из mapping — в порте это не воспроизведено (поведение покрыто mapping-тестами на уровне mapping, а не конвертера).

---

## 3. Алгоритмы и логика

### 3.1 Совпадает по постановке задачи

- Сеточная геометрия: полигоны, ортогональные линии, пересечения, overlap, разбиение; часть реализации через **Clipper2** (семантика площади/касания сохраняется).
- Графы: связность, дерево, двудольность, циклы, планарность (K5), matching (Hopcroft–Karp) — на `UndirectedAdjacencyListGraph` + `graph_algorithms`.
- Генерация **configuration spaces** (merge дверей, направления, удаление пересечений) — та же цепочка шагов, что и `ConfigurationSpacesGenerator` в Grid2D; множества точек КП и `RoomTemplateInstances` сверены с C# поблочно (`EdgarConfigSpaceCsharpParity`).
- **Декомпозиция на цепи**: `BreadthFirst` (old/new), `TwoStageChainDecomposition` (дефолт, как в C#) — те же алгоритмы на int-графе комнат.
- **Двери:** представление приведено к C# — точечные дверные линии с `degeneratedDirection` у `OrthogonalLineGrid2D`; `SimpleDoorModeGrid2D` (overlap), `ManualDoorModeGrid2D` (specific positions) покрыты портированными C#-тестами (`EdgarDoorsCsharpParity`).

### 3.2 Упрощено или иная архитектура

- **Энергия и ограничения:** в C# — `BasicConstraint`, `CorridorConstraint`, `MinimumDistanceConstraint` и общий `ConstraintsEvaluator`. В порте — та же **композиция вкладов** (basic/corridor/min-distance) через фасад `ConstraintsEvaluatorGrid2D`; флаг `optimize_corridor_constraints` и масштабирование `BasicEnergyUpdater` (по смыслу `10 * averageSize` в цепочке SA).
- **Simulated annealing:** общая идея (schedule, Metropolis, циклы/триалы) совпадает; основной path идёт через **layout controller + точное пересечение КП с релаксацией подмножеств** (как C# `LayoutOperations`). Реализован **инкрементальный SA по цепям**: маска `active_rooms` паркует неактивные комнаты (1x1 на y=1<<20), пертурбируются только комнаты активной цепи, `simulated_annealing_max_branching = 5` ограничивает ветвление вариантов — аналог C#-поведения. Коридоры исключены из SA-пертурбации. `SimulatedAnnealingEvolverGrid2D::evolve` сохранён как legacy random-walk режим.
- **Производительность (этапы H2–H4):** content-addressed кэш configuration spaces (thread_local, 200k записей), ленивый point-set у `ConfigurationSpaceGrid2D`, кэш разбиений полигонов (`cached_partition`) с bbox-префильтром, кэш дверных линий, инкрементальное состояние дверей `doors_tab_state`. По времени генерации паритет с C#-референсом достигнут (41vertices ~8 с у обоих).
- **Выбор формы комнаты** и repeat mode: в C# — `RoomShapesHandler` и mapping; в порте — `RoomShapesHandlerGrid2D`, поведение сверено тестами, без полного parity по внутренним C# типам.

### 3.3 Отсутствует в порте (ядро)

- **Двери:** отсутствует отдельный runtime-реестр обработчиков уровня `DoorHandler` из Legacy-слоя C# (для текущего Grid2D-пайплайна не блокер; режимы дверей покрыты паритет-тестами).
- **Legacy-утилиты:** статистика (`EntropyCalculator`), meta-optimization, evolution sandbox, отдельный `DungeonGenerator` / platformers — **не перенесены** (осознанно, вне скоупа).
- **Структуры GeneralAlgorithms:** например `SimpleBitVector32`, полные alias-словари — **нет** в портовом дереве (поведение покрыто без них).

---

## 4. Тесты

| Оригинал | Порт |
|----------|------|
| `Edgar.GeneralAlgorithmsTests`, `Edgar.Tests`, `Edgar.IntegrationTests`, `Edgar.PerformanceTests` | Два бинарника GTest: `edgar_tests.cpp`, `edgar_parity_tests.cpp` (225 тестов) |
| Покрытие модулей по слоям + интеграции (dungeon, room shapes, mapping) | Регрессия C++ + портированные C#-юниты (geometry, graphs, КП, doors, mapping, room shapes) + интеграция graph-based генератора |
| Производительность — BenchmarkDotNet, ручной запуск | `tools/benchmark_layout_generation.py --check` — гейт по времени/сходимости на bundled-картах |
| Глобальный паритет с одним репозиторием | **Логический паритет на уровне сценариев**: `parity_golden_test` сравнивает выдачу C++ и C# (через `tools/parity_runner_cs`) на трёх golden-картах (размеры/структура layout), `.cs.json` эталоны закоммичены |

Имя файла `edgar_parity_tests` **не** означает автоматический прогон против .NET — это массовые юниты по геометрии/графам/КП в духе GeneralAlgorithms. Реальное сравнение с C# делают suite `*CsharpParity` (портированные тесты) и `parity_golden_test` (логическое сравнение сценариев, не побайтовое).

---

## 5. Краткий итог

Порт закрывает **основной** контур генерации раскладок на сетке (цепи, КП, SA, энергия, коридоры, двери, mapping/формы комнат) с **другой** модульной структурой и **без** полного набора функций и API оригинала (Unity, meta-optimization, platformers, статистика — осознанно вне скоупа). Сходимость достигнута на всех bundled-картах, время генерации сопоставимо с C#-референсом. Для **плана приведения к соответствию** см. [port_parity_roadmap.md](port_parity_roadmap.md); история этапов производительности — в [parity_next_steps_plan.md](parity_next_steps_plan.md) (секции H–H4).
