# План достижения функционального паритета с Edgar-DotNet

Составлен 2026-09-29 по состоянию репозитория (после итераций 0–7 роадмапа и добавления сборки под macOS).
Источники: [port_vs_original_gap.md](port_vs_original_gap.md), [port_parity_roadmap.md](port_parity_roadmap.md), [test_matrix_iteration0.md](test_matrix_iteration0.md), [app_gui_parity.md](app_gui_parity.md), [parity_dod.md](parity_dod.md).

---

## 1. Текущее состояние

### Уже закрыто

| Направление | Статус |
|---|---|
| Геометрия (полигоны, ортогональные линии, overlap, разбиение, Clipper2) | паритет по смыслу, тесты `DungeonTopologyGeneratorGeometry.*` |
| Графы (связность, дерево, двудольность, планарность, Hopcroft–Karp) | паритет, `DungeonTopologyGeneratorGraphs.*` |
| Декомпозиция на цепи (BreadthFirst old/new, TwoStage) | паритет, `DungeonTopologyGeneratorChainDecomposition.*` |
| Констрейнты и энергия (итерация 1: Basic/Corridor/MinDistance, `ConstraintsEvaluatorGrid2D`) | done |
| SA через layout controller + configuration spaces (итерация 3, основной путь) | реализовано, legacy random-walk сохранён |
| Mapping и RoomShapesHandler (итерация 2) | реализовано **упрощённо** (без полного `IntAlias`/`TwoWayDictionary`) |
| Двери: `SimpleDoorModeGrid2D`, `ManualDoorModeGrid2D`, `SpecificPositionsMode` из YAML | реализовано, **без порта C#-тестов** |
| Жизненный цикл API (итерация 5: early stop, cancel, события SA) | done |
| Конвертер layout (итерация 6: `BasicLayoutConverterGrid2D`) | done |
| Матрица тестов + интеграция (итерация 7) | done формально: 16 done / 10 blocked / 6 skip |
| Сборка и тесты на macOS (arm64-osx) | 191/191 зелёные |

### Не закрыто относительно референса

**Ядро (10 blocked-строк матрицы):**

1. **Двери (итерация 4)** — 2 строки: `OverlapModeHandlerTests.cs`, `SpecificPositionsModeHandlerTests.cs` не портированы. Режимы дверей в коде есть, но допустимые позиции дверей не сверены с C#-ожиданиями.
2. **Configuration spaces (итерация 3)** — 5 строк со статусом `partial`: `CSGeneratorTests`, `ConfigurationSpacesGeneratorTests` (Core и Integration), `ConfigurationSpaceGeneratorTests`, `ConfigurationSpacesTests`. Покрытие КП фрагментарное.
3. **Mapping / RoomShapes (итерация 2)** — 3 строки: `MapDescriptionTests`, `RoomShapesHandlerTests`, `MapDescriptionMappingTests`. Упрощённый alias/repeat без численной сверки.

**Сквозное:**

4. **Golden-паритет seed→layout** — каталог `test_data/parity/` пустой (только README-договорённости); эталонных выгрузок из C# нет, формата сравнения нет.
5. **Производительность** — только ручной `tools/benchmark_layout_generation.ps1` (Windows-only, на Mac не работает), порога в CI нет.

**Приложение (GUI-паритет с Edgar.GUI):**

6. Сканирование `Maps/` без рекурсии — `Maps/Thesis/` и подобные недоступны из UI.
7. Нет диалога «сохранить файл» вне Windows (экспорт пишет фиксированный `layout_export.json`).
8. Нет превью-миниатюр в списке карт и отдельного диалога открытия файла (частично зафиксировано как осознанный skip).
9. Ресурсы `RandomGraphs/`, `MapDescriptions/` (не-YAML) из `resources/dungeon_topology_generator_gui` не используются.

**Осознанно вне скоупа (не трогаем без отдельного запроса):** Unity, meta-optimization, evolution sandbox, platformers, entropy/graph analysis, `DungeonGenerator` 1:1, `SimpleBitVector32`.

**Техдолг, замеченный при ревизии:** в конце [test_matrix_iteration0.md](test_matrix_iteration0.md) (строки 97–101) остался мусор от незавершённой правки агента — удалить при ближайшем коммите.

---

## 2. План по этапам

Порядок следует рекомендации роадмапа (2 → 3 → 4, затем golden): seed→layout паритет реалистичен только после них.

### Этап A. Подготовка (0.5–1 день)

- Склонировать Edgar-DotNet в `_edgar_ref/` (зафиксирован коммит `258c83a88656bd6095255a1b749232ade2b87589`, 2021-01-03); путь в `.gitignore`.
- Починить `test_matrix_iteration0.md` (мусор в конце файла удалён).
- Для этапа E потребуется .NET SDK (сейчас на машине не установлен — поставить `brew install dotnet-sdk` к началу этапа E).

**Критерий готовности:** `_edgar_ref/src` доступен, матрица чистая. ✅ (2026-09-29)

### Этап B. Mapping и RoomShapes (итерация 2, ~2–3 дня)

- Портировать сценарии `RoomShapesHandlerTests.cs`: выбор шаблона, repeat mode (allow/deny/…), смена формы при фиксированном графе.
- Портировать `MapDescriptionMappingTests.cs`: комната ↔ индекс, согласованность с графом.
- Дополнить `MapDescriptionTests`-покрытие по смыслу (`DungeonTopologyGeneratorLevelDescription.*`).
- При необходимости довести alias-логику до семантики `IntAlias`/`TwoWayDictionary` точечно (не переписывая архитектуру).

**Критерий:** 3 строки `blocked (2)` → `done`; `ctest` зелёный.

### Этап C. Configuration spaces (итерация 3, ~3–4 дня)

- Разобрать `CSGeneratorTests.cs` / `ConfigurationSpacesGeneratorTests.cs` (Core + Integration) и допортировать недостающие кейсы: merge дверей, направления, удаление пересечений, допустимые позиции для пар шаблонов.
- Новые тесты допустимости: после шага perturb позиция остаётся в объединении КП с соседями (микро-уровни из референса).
- Тест детерминизма: два прогона с одним seed → идентичная последовательность событий/layout.

**Критерий:** 5 строк `blocked (3)` → `done`; регрессия `dungeon_topology_generator_tests`/`dungeon_topology_generator_parity_tests` зелёная.

### Этап D. Двери (итерация 4, ~2–3 дня)

- Портировать `OverlapModeHandlerTests.cs` и `SpecificPositionsModeHandlerTests.cs`: те же дверные линии/полигоны, те же множества допустимых позиций.
- Дополнить `DoorUtilsTests`/`MergeDoorLines` регрессию под новые режимы.
- Интеграционный тест: генерация КП с non-simple handler на маленьком графе → валидный layout.

**Критерий:** 2 строки `blocked (4)` → `done`; матрица полностью `done`/`skip (na)`.

### Этап E. Golden-пайплайн seed→layout (~3–5 дней)

- Определить общий входной формат (JSON-описание уровня: граф, шаблоны, двери, seed) и формат эталона (позиции/контуры комнат + двери; допуски на порядок).
- Написать выгрузчик эталонов на C# (консоль над `_edgar_ref`): вход → эталонный JSON в `test_data/parity/expected/`.
- Написать C++-прогонщик (GTest или отдельный бинарь): тот же вход → сравнение с эталоном с допусками.
- Первые сценарии: эталонные из `parity_dod.md` (4-room cycle, 3-room corridor line, 6-room star).
- **Решение (2026-09-29, владелец):** сравнение **только логическое** — контуры/позиции комнат, смежность, двери. Побайтовое совпадение (RNG, PNG-отрисовка) не требуется и не проверяется.

**Критерий:** ≥3 golden-сценария зелёные в `ctest`; зафиксированы допуски и известные расхождения (если seed→layout 1:1 недостижим из-за RNG/порядка — документируем структурное сравнение вместо побайтового).

### Этап F. Приложение (GUI), опционально (~2–3 дня)

- Рекурсивный обход `Maps/` (подпапки вроде `Thesis/`) с группировкой в комбо.
- Портативный диалог открытия/сохранения (например tinyfiledialogs через vcpkg) для Export JSON на macOS/Linux.
- По запросу: миниатюры карт, использование `RandomGraphs/`.

### Этап G. Производительность (~1 день)

- Заменить/дополнить `benchmark_layout_generation.ps1` кроссплатформенным скриптом (Python) или ctest-целью.
- Зафиксировать порог времени генерации на 1–2 эталонных пресетах (9vertices, 17vertices) как регрессионный gate.

---

## 2.5. Статус выполнения (2026-09-29)

| Этап | Статус | Коммит |
|---|---|---|
| A Подготовка | ✅ `_edgar_ref` @ `258c83a`, матрица почищена | `Port RoomShapesHandler/MapDescriptionMapping...` |
| B Mapping/shapes (ит. 2) | ✅ 10 новых тестов (`DungeonTopologyGeneratorRoomShapesCsharpParity`, `DungeonTopologyGeneratorMappingCsharpParity`); контрактация коридоров в `get_stage_one_graph` | тот же |
| C Configuration spaces (ит. 3) | ✅ точечные C#-множества КП; point-двери как в C#; `get_room_template_instances` с дедупликацией симметрий; degenerate-направления линий | `Close configuration-spaces parity...` |
| D Двери (ит. 4) | ✅ все 7 сценариев Overlap/SpecificPositions с точными (from,to,dir,length) | `Port door mode handler tests...` |
| E Golden-пайплайн | ✅ 3 сценария × оба движка, логические инварианты зелёные (`parity_golden_test`, `tools/parity_runner_cs`) | `Add golden parity pipeline...` |
| F GUI-фичи | не начат (опционально) | — |
| G Перфоманс-gate | ✅ `benchmark_layout` + `tools/benchmark_layout_generation.py`, smoke-gate в ctest | см. ниже |

**Матрица тестов закрыта полностью:** 26 done / 6 skip (na), blocked не осталось. Тестов всего: 225 (Release, macOS).

### Этап H. Производительность на плотных/коридорных картах — **основная часть закрыта (2026-09-29)**

Что было сделано:

1. **Кэш конфигурационных пространств** (content-addressed, thread-local) — C# предвычисляет КП
   на пары шаблонов; порт пересчитывал на каждую попытку размещения.
2. **Точечные множества КП** с ленивым построением (`ConfigurationSpaceGrid2D::points`) — проверка
   принадлежности O(1) вместо сканирования линий.
3. **Быстрый overlap** через кэшированное разбиение на прямоугольники вместо Clipper2 BooleanOp
   на каждую проверку (`polygons_overlap_area` → `polygons_overlap_via_partitions`).
4. **Критический баг-ловушка:** `try_complete_chain` мог жить вечно — «прогресс» (смена позиции без
   улучшения энергии) сбрасывал счётчик no-progress. Добавлен жёсткий лимит sweep-ов.
5. **Точное пересечение КП** (`maximum_intersection_lines`, порт C# `GetMaximumIntersection`
   с релаксацией подмножеств) вместо вероятностного сэмплинга до 160 кандидатов.
6. **Graceful degradation:** провал начального размещения больше не бросает через
   `generate_layout` (41vertices падал с исключением) — считается неудачным рестартом;
   в GUI добавлен бюджет времени на layout (по умолчанию 15 с, 0 = без лимита).
7. Калибровка теста `Chain_yieldStream` (коридор со всеми трансформациями, stage 2 как в C#,
   бюджет stage-two failures 4 → 16) — старая конфигурация проходила случайно по сиду.

Итоги (Release, медиана 5 прогонов): tutorial_basic 8 мс, 9vertices 4 мс (было: ∞),
tutorial_corridors 1.4 с (было: ∞), dragonAge 3.2 с 5/5 (было: падения). Референс C# на
идентичном уровне 9vertices: ~1.7 с user-time (порт быстрее референса на этом входе).

**Этап H2 (2026-09-29, закрыт по сути):** цель — сходимость 17/41vertices.

Ключевые расхождения с C#, найденные и исправленные:

1. **`RoomTemplateRepeatMode` по умолчанию** — в C# `LevelDescriptionGrid2D` это `NoRepeat`,
   у нас был `AllowRepeat` → SA тонул в одинаковых формах. Исправлено на C#-дефолт.
2. **Декомпозиция цепей по умолчанию** — C#: `TwoStageChainDecomposition(BreadthFirst(new))`,
   у нас `breadth_first_old`. Дефолт сменён на `two_stage`.
3. **Семантика рестартов SA** — C# (default `RestartSuccessPlace.OnValidAndDifferent`) считает
   цикл «неудачным», если не родился валидный layout; у нас неудача = отсутствие Metropolis-accept
   (почти никогда) → рестарты не срабатывали вообще. Исправлено.
4. **Инкрементальная сборка цепей** — C# эволюционирует цепь i на частичном layout (только цепи
   0..i); у нас SA работал сразу на всех комнатах. Добавлена маска активных комнат в `evolve`
   (неактивные «паркуются» 1x1 далеко + фильтрованный граф) и префиксные маски в `generate`.
5. **Бэктрекинг по цепям** (упрощённый C# `GeneratorPlanner` с `maximumBranching=5`): ретраи
   цепи из снапшота (≤4) и откат на цепь назад (до 64) вместо перезапуска всего layout.
6. **Порядок размещения при two_stage:** узлы без размещённых соседей откладываются
   (worklist), узлы без соседей получают случайную свободную позицию (раньше — throw /
   деградация в 1x1 в начале координат).

Итоги (Release, 3 прогона, бюджет 90 с): tutorial_basic 0.9 мс, 9vertices ~27 мс,
tutorial_corridors 1.6 с 3/3, dragonAge 0.9 с 3/3, 17vertices 3/3 (9–49 с), 41vertices
сходится часто (min 1.6 с), худший случай — graceful stop по бюджету. Референс C#:
9vertices ~1.7 с, 41vertices ~8 с — порт на большинстве карт быстрее, на 41vertices медленнее.

**Этап H3 (2026-09-30, закрыт по сути):** порт C# `GeneratorPlanner` и связанных деталей.

Реализовано:

1. **Дерево вариантов цепей (DFS)**: `evolve` собирает до `simulated_annealing_max_branching`
   (= 5, как в C#) валидных вариантов цепи через колбэк `on_variant` (аналог `Evolve(count)`),
   `generate()` обходит их в глубину с бэктрекингом; ретраи evolve при пустой выдаче
   (RNG продолжается — каждый прогон расходится).
2. **`completion_mask`**: внутренний `try_insert_corridors` / `try_complete_chain` в режиме
   инкрементальной сборки не трогают неактивные (припаркованные) комнаты.
3. **Коридоры не участвуют в SA-пертурбации** (C# `PerturbLayout` пертурбирует только
   не-коридорные узлы цепи); `try_complete_chain` получил опциональную маску активных комнат.

Итоги (Release, 3 прогона): tutorial_basic ~0.5 мс, 9vertices ~3 мс, tutorial_corridors 0.6 с 3/3,
dragonAge 0.3 с 3/3, 17vertices 0.85 с 3/3 (ранее 9–49 с!), 41vertices 2/3 (2.4–30 с).
Референс C#: 41vertices ~8 с.

**Этап H4 (2026-09-30, закрыт):** стабильная сходимость 41vertices.

Узкие места по профилю и их устранение:

1. **Кэш `get_doors`** для `SimpleDoorModeGrid2D` — дверные линии пересобирались для всех комнат
   на каждый триал SA (самая горячая функция в профиле).
2. **Инкрементальный `doors_tab`** в evolve: обновляется только для пертурбированной комнаты
   вместо полной пересборки на триал.
3. **BBox-префильтр** в `polygons_overlap_via_partitions` — дешёвое отсечение далёких кандидатов
   до обращения к разбиениям.
4. **Лимит рестартов layout 128 → 256** — анализ показал, что часть успешных прогонов 41vertices
   обрывалась лимитом (per-restart вероятность успеха × 256 достаточна).

Итоги (Release, 3 прогона, budget 90–120 с): **все bundled-карты сходятся 3/3** —
tutorial_basic 0.2 мс, 9vertices 11 мс, tutorial_corridors 0.2 с, dragonAge 0.1 с,
17vertices 0.4 с, 41vertices ~8 с (медиана), overlapping = 0 везде. Референс C# на 41vertices:
~8 с — паритет по времени достигнут.

## 3. Ориентировочная оценка (исходная, до выполнения)

| Этап | Содержание | Оценка |
|---|---|---|
| A | Подготовка, `_edgar_ref` | 0.5–1 день |
| B | Mapping/shapes (ит. 2) | 2–3 дня |
| C | Configuration spaces (ит. 3) | 3–4 дня |
| D | Двери (ит. 4) | 2–3 дня |
| E | Golden seed→layout | 3–5 дней |
| F | GUI-фичи (опционально) | 2–3 дня |
| G | Перфоманс-gate | 1 день |
| **Итого ядро (A–E)** | | **~2–2.5 недели** |

## 4. Риски

- **RNG-паритет:** решено (2026-09-29) — golden сравнивает только логическую структуру (контуры, смежность, двери), не байты/RNG.
- **Регрессии при B–D:** каждый этап завершать зелёным `ctest` и отдельным коммитом. ✅ соблюдалось
- **Зависимость от `_edgar_ref`:** без локального клона референса этапы B–E не стартуют. ✅ клон зафиксирован на `258c83a`
- **Производительность (этап H):** плотные/коридорные карты не сходятся за разумное время — блокер для полноценной работы приложения на bundled-пресетах.
