# AGENTS.md

Инструкции для AI-агентов и людей, работающих с репозиторием PGG.
Этот файл — **единый источник правды** по тому, «как здесь работать».
Устройство языка и грабли реализации живут в `docs/pgg/`; правя код, обновляй `docs/pgg/implementation.md`, а не этот файл.

## Коммуникация и язык

- Общайся на русском, если явно не просят иначе.
- Идентификаторы и комментарии в исходниках (`src/`, `tools/`, CMake) — на английском.
- Документацию пиши на русском. Исключение: `README.md` — на английском.
- Сообщения git-коммитов (subject и тело) — на английском.

## Репозиторий

- Это **standalone-репозиторий языка PGG**: библиотека, CLI, вьюер, тесты, арт-примеры.
- Не плодить изолированные проекты со своими `project(...)` без необходимости.
- **Стек:** C++20, ANTLR4 4.13.2, glm, gtest; PggViewer — Sokol + Dear ImGui + spdlog. Без Qt.
- Не вводить тяжёлые зависимости без согласования.
- Спецификация — `docs/pgg/geometry_generation_language.md` (единственный источник правды по семантике). Реализация — `docs/pgg/implementation.md`.
- В каждой lib/app есть `pch.h` — подключать первым, если файл есть.

## CMake и макросы

- Проектные CMake-скрипты и макросы — в `cmake/utils.cmake`.
- Использовать общие макросы: `nw_add_console_app(...)`, `nw_add_lib_sources(...)` и др. `nw_add_...`.
- CLI-приложения — через `nw_add_console_app`. Зависимости передавать через `LIBS`. У макросов общий include path на `src/libs`, подключается `pch.h`.
- Include path на `src/libs` нужен приложениям и тестам: внутренние заголовки ядра подключаются как `pgg/src/eval/…`.

## Сборка (Windows, CMake + vcpkg)

Основной Windows-флоу — Visual Studio 2022 + CMake Presets.

- Сгенерировать solution: `generate_vs.bat` (при необходимости бутстрапит vcpkg).
  - Solution: `_intermediate_64\pgg.sln`.
  - Без обёртки: `cmake --preset vs2022`.
- Сборка из CLI: `cmake --build --preset debug --target PggViewer`.
- Бинарные директории: `_intermediate_64` (Windows и macOS), `_b-em` (Emscripten).
- **Не использовать** `build.sh` для Windows-флоу.

## Сборка (macOS, CMake + vcpkg)

macOS-флоу — Xcode generator + CMake Presets, та же `_intermediate_64`, триплет `arm64-osx`.

- Конфигурация: `./build_mac.sh` (обёртка над `cmake --preset macos`). Первая конфигурация собирает vcpkg-зависимости.
- Сборка из CLI: `cmake --build --preset macos-debug --target PggViewer`.
- Бинарники: `_intermediate_64/src/apps/<App>/Debug/<App>`.
- Smoke-проверки: `PggViewer --smoke`; юнит-тесты — `_intermediate_64/src/tests/Debug/pgg_tests`.
- Платформенные особенности порта — `docs/BUILD.md`.

### vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.
- Сетевой binary cache подключён во всех пресетах (`docs/VCPKG_CACHE.md`, `docs/BUILD.md`): для push нужен `export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"`, чтение работает и без него.
- Индексация для clangd (`compile_commands.json`, пресет `ide-index`, `generate_compile_commands.bat`) — `docs/BUILD.md`.

## Сборка (Linux, CMake + vcpkg)

Linux-флоу — Ninja (single-config) + CMake Presets, триплет `x64-linux`, бинарная директория `_int_linux` (**не** `_intermediate_64` — та под win/mac-кэш).

- Конфигурация: `./build_linux.sh` (обёртка над `cmake --preset linux`). Первая конфигурация собирает vcpkg-зависимости.
- Сборка из CLI: `cmake --build --preset linux-debug --target PggViewer`.
- Релизная сборка: пресет `linux-release` (`CMAKE_BUILD_TYPE=Release`, отдельная бинарная директория `_int_linux_release`, наследует `linux`): конфигурация `cmake --preset linux-release`, сборка `cmake --build --preset linux-release --target PggViewer`.
- Бинарники: `_int_linux/src/apps/<App>/Debug/<App>` (app-макросы кладут exe в подпапку `$<CONFIG>`).
- Smoke-проверки и юнит-тесты: `PggViewer --smoke`, `_int_linux/src/tests/pgg_tests` (ctest: `ctest --test-dir _int_linux --output-on-failure`).
- Системные пакеты и особенности порта — `docs/BUILD.md`.

## Тестирование

- Unit-тесты (gtest) — `src/tests/pgg/<name>_test.cpp`, подхватываются GLOB'ом; бинарь `pgg_tests`.
- Корпус эталонов — `src/tests/pgg/corpus/`; голдены фингерпринтов — `src/tests/pgg/goldens/` (перезапись: `PggTool run <file> --update-goldens` из корня репо).
- Продуктовые/арт-примеры — `resources/pgg/`, не путать с тестовым корпусом.

## PGG

- Спецификация — `docs/pgg/geometry_generation_language.md` (ТЗ: текст-first нодовый граф для LLM-агентов + нодовая проекция; этапы и критерии — §15, история — §19).
- Заметки по реализации (этапы E0–E8 по файлам, грабли ANTLR/ядра, PggTool/PggViewer CLI, корпус и сьюты) — `docs/pgg/implementation.md`. **Правя `src/libs/pgg`, `src/apps/PggTool`, `src/apps/PggViewer` или корпус, обновляй его, а не этот файл.**
- Коротко: `src/libs/pgg` (ANTLR4 4.13.2; сгенерированный парсер коммитится в `parser_gen/`, после правок `grammar/Pgg.g4` — `tools/pgg/regen_parser.sh`), ядро исполнения `src/libs/pgg/src/eval/`, тесты `src/tests/pgg/*_test.cpp` + корпус `src/tests/pgg/corpus/` (арт-примеры — `resources/pgg/`), CLI `PggTool` (`check`/`fmt`/`ast`/`run`/`docs`), вьювер `PggViewer` (нодовая проекция + превью, `--smoke`).
- Перед grep по спеке и ядру: `pgg_docs("<name>")` / `PggTool docs builtins` / `docs/pgg/cheatsheet.md`.
- Арт-итерации: правка общего def → рендер **всех** потребителей (grep имени в `resources/pgg`); сравнение «как у дома» — один кадр, где обе детали рядом; числа (`pgg_measure` / `bbox`) до картинки; новые грабли — сразу в cheatsheet §«Грабли»; виды — в `<stem>.views.json`, не в чат; коммит-единица — один визуальный эффект. С нуля по референсу: масса (`pgg_reference`) раньше деталей; возможности рендерера — до проектирования (прозрачности нет); форма раньше палитры/эмиссии; «вижу не то» — сначала `render_state` / явные args, потом модель.

## MCP

PggViewer поднимает TCP RPC на `127.0.0.1:9878` (`--serve`; `src/apps/PggViewer/ViewerRpcServer.cpp`). MCP-обёртка — `pgg` / `pgg-win` (`tools/pgg_mcp/`, инструменты `pgg_*`; viewer поднимается автоматически, если порт не отвечает). Контракт — `docs/pgg/viewer_rpc.md`. Env: `PGG_REPO_ROOT`, `PGG_VIEWER`.

## Где что искать

- `README.md` — что это за проект и как собрать.
- `docs/BUILD.md` — платформенные особенности сборки, vcpkg/binary cache (`docs/VCPKG_CACHE.md`), индексация для clangd.
- `docs/pgg/README.md` — индекс документации языка.
- `docs/pgg/geometry_generation_language.md` — спецификация.
- `docs/pgg/implementation.md` — заметки по реализации.
- `docs/pgg/viewer_rpc.md` — RPC вьюера и MCP.
- `docs/mcp_servers.md` — MCP-серверы репозитория.
