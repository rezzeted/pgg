# AGENTS.md

Инструкции для AI-агентов и людей, работающих с монорепозиторием PGG.
Этот файл — **единый источник правды** по тому, «как здесь работать».

Монорепозиторий объединяет три бывших standalone-проекта (история всех трёх
сохранена в git — мерджи `--allow-unrelated-histories`):

- **PGG** — язык процедурной геометрии: библиотека, CLI, вьюер, тесты,
  арт-примеры. Устройство языка и грабли реализации — `docs/pgg/`; правя код,
  обновляй `docs/pgg/implementation.md`, а не этот файл.
- **DungeonGeometryGenerator** — оркестратор подземелий поверх PGG (слот-ассеты `assets/`) и
  dungeon_topology_generator (раскладки). Нормативный документ — `docs/dungeon_geometry_generator/requirements.md`
  (§5–§9 — конвейер F1–F11, N-хвосты — сквозные требования).
- **level-synth / dungeon_topology_generator** — порт Edgar-DotNet: библиотека раскладок
  `src/libs/dungeon_topology_generator`, SDL3-вьюер `src/apps/DungeonTopologyGeneratorViewer`, данные `test_data/`,
  доки `docs/level-synth/`.

## Коммуникация и язык

- Общайся на русском, если явно не просят иначе.
- Идентификаторы и комментарии в исходниках (`src/`, `tools/`, CMake) — на английском.
- Документацию пиши на русском. Исключение: `README.md` — на английском.
- Сообщения git-коммитов (subject и тело) — на английском (конвенция базового
  репозитория pgg; история dungeon_geometry_generator на русском — наследие standalone-эпохи).
- Коммиты и PR **не подписывать агентом**: никаких `Co-authored-by` /
  `Signed-off-by` трейлеров от инструментов. Автор — человек, который попросил
  коммит. Единица коммита — один эффект (фича + тесты + доки).

## Репозиторий

- Не плодить изолированные проекты со своими `project(...)` без необходимости —
  корневой `CMakeLists.txt` один, `src/{libs,apps,tests}` подхватывают
  подкаталоги GLOB'ом.
- **Стек:** C++20, ANTLR4 4.13.2, glm, gtest; PggViewer/DungeonGeometryGeneratorViewer — Sokol +
  Dear ImGui + spdlog; dungeon_topology_generator — boost-graph, yaml-cpp, nlohmann-json, SDL3
  (вьюер). Без Qt. Не вводить тяжёлые зависимости без согласования.
- Спецификация языка — `docs/pgg/geometry_generation_language.md`
  (единственный источник правды по семантике). Реализация —
  `docs/pgg/implementation.md`.
- В каждой lib/app pgg-происхождения есть `pch.h` — подключать первым, если
  файл есть.
- Единственный git-submodule — `toolchain/vcpkg`. **Не патчить** его напрямую;
  overlay-фиксы — `vcpkg_overlays/ports` (уже подключён в `CMakePresets.json`
  через `VCPKG_OVERLAY_PORTS`).

## CMake и макросы

- Проектные CMake-скрипты и макросы — в `cmake/utils.cmake`.
- Использовать общие макросы: `nw_add_console_app(...)`, `nw_add_lib_sources(...)`
  и др. `nw_add_...`. CLI-приложения — через `nw_add_console_app`. Зависимости
  передавать через `LIBS`. У макросов общий include path на `src/libs`,
  подключается `pch.h`.
- Заголовки ядра pgg подключаются как `pgg/src/eval/…` (include path на
  `src/libs` выставлен у таргета `pgg` PUBLIC из корневого CMakeLists).
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` →
  прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.

## Сборка (Windows, CMake + vcpkg)

Основной Windows-флоу — Visual Studio 2022 + CMake Presets.

- Сгенерировать solution: `generate_vs.bat` (при необходимости бутстрапит vcpkg).
  - Solution: `_intermediate_64\pgg.sln`.
  - Без обёртки: `cmake --preset vs2022`.
- Сборка из CLI: `cmake --build --preset debug --target PggViewer`.
- Бинарные директории: `_intermediate_64` (Windows и macOS).
- **Не использовать** `build.sh` для Windows-флоу.

## Сборка (macOS, CMake + vcpkg)

macOS-флоу — Xcode generator + CMake Presets, та же `_intermediate_64`, триплет `arm64-osx`.

- Конфигурация: `./build_mac.sh` (обёртка над `cmake --preset macos`). Первая конфигурация собирает vcpkg-зависимости.
- Сборка из CLI: `cmake --build --preset macos-debug --target PggViewer`.
- Бинарники: `_intermediate_64/src/apps/<App>/Debug/<App>`.
- Smoke-проверки: `PggViewer --smoke`; `PggServe --smoke`; юнит-тесты — `_intermediate_64/src/tests/Debug/pgg_tests`.
- Платформенные особенности порта — `docs/BUILD.md`.

### vcpkg и зависимости

- Зависимости — через `vcpkg.json`.
- Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым
  с актуальным Dear ImGui из vcpkg (imgui собран с фичами
  `docking-experimental`, `sdl3-binding`, `opengl3-binding` — нужны вьюеру
  level-synth; sokol-биндинги идут через overlay).
- Сетевой binary cache подключён во всех пресетах (`docs/VCPKG_CACHE.md`,
  `docs/BUILD.md`): для push нужен `export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"`, чтение работает и без него.
- Индексация для clangd (`compile_commands.json`, пресет `ide-index`,
  `generate_compile_commands.bat`) — `docs/BUILD.md`.

## Сборка (Linux, CMake + vcpkg)

Linux-флоу — Ninja (single-config) + CMake Presets, триплет `x64-linux`, бинарная директория `_int_linux` (**не** `_intermediate_64` — та под win/mac-кэш).

- Конфигурация: `./build_linux.sh` (обёртка над `cmake --preset linux`). Первая конфигурация собирает vcpkg-зависимости.
- Сборка из CLI: `cmake --build --preset linux-debug` (все таргеты) или точечно `--target PggViewer` / `--target DungeonGeometryGeneratorServe`.
- Релизная сборка: пресет `linux-release` (`CMAKE_BUILD_TYPE=Release`, отдельная бинарная директория `_int_linux_release`, наследует `linux`).
- Бинарники: `_int_linux/src/apps/<App>/Debug/<App>` (app-макросы кладут exe в подпапку `$<CONFIG>`).
- Smoke-проверки и юнит-тесты: `PggViewer --smoke`, `PggServe --smoke`,
  `_int_linux/src/tests/pgg_tests` (ctest: `ctest --test-dir _int_linux --output-on-failure`).
- Системные пакеты и особенности порта — `docs/BUILD.md`.

## Тестирование

- **pgg:** unit-тесты (gtest) — `src/tests/pgg/<name>_test.cpp`, подхватываются
  GLOB'ом; бинарь `pgg_tests`. Корпус эталонов — `src/tests/pgg/corpus/`;
  голдены фингерпринтов — `src/tests/pgg/goldens/` (перезапись:
  `PggTool run <file> --update-goldens` из корня репо).
- **dungeon_geometry_generator:** быстрые сьюты (секунды) — `dungeon_geometry_generator_ir_test`, `dungeon_geometry_generator_project_test`,
  `dungeon_geometry_generator_layout_test`, `dungeon_geometry_generator_topo_test`, `dungeon_geometry_generator_d0_test`, `dungeon_geometry_generator_assets_test`,
  `dungeon_geometry_generator_export_test` (~26 с, один frozen fill) — запускать всегда. Smoke:
  `DungeonGeometryGeneratorViewer_smoke_layout` (~3 с), `DungeonGeometryGeneratorCli_smoke_*` (~25 с),
  `DungeonGeometryGeneratorServe_smoke` (~12 с) и `DungeonGeometryGeneratorServe_rpc_py`; DI-юниты
  `python3 -m unittest tools.dungeon_geometry_generator_mcp.test_session tools.dungeon_geometry_generator_mcp.test_pgg_layer`.
  Медленные (Debug + PGG, минуты — это норма): `dungeon_geometry_generator_fill_test` (~4 мин),
  `dungeon_geometry_generator_check_test` (~1–2 мин; `DungeonGeometryGeneratorCheck.PassFrozen` — отдельно на
  незагруженной машине). Детерминизм (N1) и стабильный порядок ключей (N6) —
  проверять тестами; чужой `format` отклонять с подсказкой (N7).
- **dungeon_topology_generator:** `dungeon_topology_generator_tests`, `dungeon_topology_generator_parity_tests`, `preset_loader_tests`,
  `generation_diagnostic_test`, `parity_golden_test` (данные — `test_data/`),
  `benchmark_layout` (smoke-гейт).
- Продуктовые/арт-примеры pgg — `resources/pgg/` (`lib/` и сцены) и
  минипроекты `resources/AmberEstate/`, `resources/ManorHouse/`,
  `resources/Mansion/`, `resources/Roads/`; не путать с тестовым корпусом.

## PGG

- Спецификация — `docs/pgg/geometry_generation_language.md` (ТЗ: текст-first
  нодовый граф для LLM-агентов + нодовая проекция; этапы и критерии — §15,
  история — §19).
- Заметки по реализации (этапы E0–E8 по файлам, грабли ANTLR/ядра,
  PggTool/PggViewer/PggServe CLI, корпус и сьюты) — `docs/pgg/implementation.md`.
  **Правя `src/libs/pgg`, `src/apps/PggTool`, `src/apps/PggViewer`,
  `src/apps/PggServe` или корпус, обновляй его, а не этот файл.**
- Коротко: `src/libs/pgg` (ANTLR4 4.13.2; сгенерированный парсер коммитится в
  `parser_gen/`, после правок `grammar/Pgg.g4` — `tools/pgg/regen_parser.sh`),
  ядро исполнения `src/libs/pgg/src/eval/`, тесты `src/tests/pgg/*_test.cpp` +
  корпус `src/tests/pgg/corpus/`, CLI `PggTool` (`check`/`fmt`/`ast`/`run`/`docs`),
  вьювер `PggViewer` (нодовая проекция + превью, `--smoke`, без TCP), демон
  `PggServe` (слоты по `.pgg`, RPC `:9878`).
- Перед grep по спеке и ядру: `pgg_docs("<name>")` / `PggTool docs builtins` /
  `docs/pgg/cheatsheet.md`.
- Арт-итерации: правка общего def → рендер **всех** потребителей (grep имени в
  `resources/pgg`, `resources/AmberEstate` и `resources/Roads`); сравнение «как
  у дома» — один кадр, где обе детали рядом; числа (`pgg_measure` / `bbox`) до
  картинки; новые грабли — сразу в cheatsheet §«Грабли»; виды — в
  `<stem>.views.json`, не в чат; коммит-единица — один визуальный эффект. С нуля
  по референсу: масса (`pgg_reference`) раньше деталей; возможности рендерера —
  до проектирования (прозрачности нет); форма раньше палитры/эмиссии; «вижу не
  то» — сначала `render_state` / явные args, потом модель. Численный симптом в
  общем коде («мало якорей») — сначала разбивка по элементам
  (`pgg_probe(specs=[…"hist[attr=@island_id]"…])`) и чтение def'а библиотеки,
  потом обходы в сцене; после численной приёмки — один крупный план (числа не
  видят перекрытий).
- Слои: ядро (`src/libs/pgg`, builtin'ы) домен-нейтрально — геометрия,
  топология, поля, запросы; коды, группы и конвенции домена (`K_*`, `R_*`,
  группа `roof`) — в `lib/<domain>` поверх `lib/layout` (сейчас `lib/arch`;
  дальше техника, растительность, ландшафт). Builtin — когда механизм нужен
  нескольким доменам или в `.pgg` непосилен (BVH, скелет); доменная разметка
  поверх него — library def.
- Язык пишет LLM: грабли вида «пиши иначе» — дефект языка или текста ошибки,
  чинить ядро/подсказку; в cheatsheet §«Грабли» — только настоящая семантика.

## DungeonGeometryGenerator

- Раскладка: `src/libs/dungeon_geometry_generator` (проект/граф/IR), `src/libs/dungeon_geometry_generator_layout` (F2/F3),
  `src/libs/dungeon_geometry_generator_fill` (F6), `src/libs/dungeon_geometry_generator_check` (F11),
  `src/libs/dungeon_geometry_generator_export` (F7), `src/libs/dungeon_geometry_generator_d0` (D0, замороженный IR);
  приложения `src/apps/DungeonGeometryGeneratorViewer` (F9), `src/apps/DungeonGeometryGeneratorCli` (F10, машинная
  петля — `docs/dungeon_geometry_generator/cli_v1.md`) и `src/apps/DungeonGeometryGeneratorServe` (RPC-демон с тёплыми
  слотами — `docs/dungeon_geometry_generator/mcp_v1.md`); MCP-сервер `dungeon_geometry_generator` — `tools/dungeon_geometry_generator_mcp/`
  (Python, FastMCP → DungeonGeometryGeneratorServe, pgg-слой к PggServe); тесты
  `src/tests/dungeon_geometry_generator_*_test.cpp` + данные `src/tests/data`; слот-ассеты `assets/`;
  доки `docs/dungeon_geometry_generator/`; демо-проекты `projects/` (превью в DungeonGeometryGeneratorViewer, см.
  `projects/demo/README.md`).
- Доки dungeon_geometry_generator — русские. Форматы версионируются (`dungeon-geometry-generator-ir/2`,
  `dungeon-geometry-generator-layout/0`); смена схемы = bump версии + N7-хинт для старой.
- PGG-ассеты dungeon_geometry_generator используют общую библиотеку `resources/pgg/lib/` — правки
  общих def'ов проверять рендерами всех потребителей (см. «PGG → Арт-итерации»).

## level-synth / dungeon_topology_generator

- `src/libs/dungeon_topology_generator` — библиотека раскладок (порт Edgar-DotNet); Clipper2 тянется
  FetchContent'ом при configure (нужна сеть). `src/libs/drui` — ImGui-helpers
  вьюера. `src/apps/DungeonTopologyGeneratorViewer` — SDL3 + ImGui вьюер.
- Паритет с оригинальным C# — `docs/level-synth/`, данные `test_data/parity`
  (регенерируемые `actual/*.cpp.json` в .gitignore), раннер `tools/parity_runner_cs`.

## MCP

Два сервера в `.mcp.json` / `.cursor/mcp.json`:

- `pgg` (`python3 -m tools.pgg_mcp.launch`) — сам поднимает `PggServe`
  (RPC `127.0.0.1:9878`) или отвечает `need_build` с командами сборки. Слот =
  канонический путь `.pgg`; на `render`/`probe` передавать `file=`, если в этом
  ходе не было `load`. Контракт — `docs/pgg/serve_rpc.md`. Env: `PGG_REPO_ROOT`,
  `PGG_SERVE`.
- `dungeon_geometry_generator` (`python3 -m tools.dungeon_geometry_generator_mcp.launch`) — проектные слоты DungeonGeometryGeneratorServe +
  pgg-слой для отладки слот-ассетов (корень PggServe — тот же монорепо).
  Контракт — `docs/dungeon_geometry_generator/mcp_v1.md`. Env: `DUNGEON_GEOMETRY_GENERATOR_REPO_ROOT`, `PGG_SERVE_PORT`.

## Где что искать

- `README.md` — что это за проект и как собрать.
- `docs/BUILD.md` — платформенные особенности сборки, vcpkg/binary cache
  (`docs/VCPKG_CACHE.md`), индексация для clangd.
- `docs/pgg/README.md` — индекс документации языка.
- `docs/gallery/` — геройские кадры арт-примеров для корневого README;
  пересъёмка — `tools/pgg/regen_gallery.sh`.
- `docs/pgg/geometry_generation_language.md` — спецификация.
- `docs/pgg/implementation.md` — заметки по реализации.
- `docs/pgg/serve_rpc.md` — RPC PggServe и MCP.
- `docs/mcp_servers.md` — MCP-серверы репозитория.
- `docs/dungeon_geometry_generator/requirements.md` — нормативный документ dungeon_geometry_generator.
- `docs/level-synth/README.md` — level-synth: что это и история порта.
