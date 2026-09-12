# PGG

PGG — язык процедурной генерации геометрии: текст-first нодовый граф для LLM-агентов и нодовая проекция для человека.

Раньше жил внутри [Neverwhere](https://github.com/icecampus/neverwhere); этот репозиторий — самостоятельная реализация, CLI, вьюер, тесты и арт-примеры.

| Путь | Что это |
|---|---|
| `src/libs/pgg` | Библиотека: парсер (ANTLR4 4.13.2), AST, ядро исполнения |
| `src/apps/PggTool` | CLI: `check` / `fmt` / `ast` / `run` / `docs` / `diff` |
| `src/apps/PggViewer` | Нодовая проекция + превью геометрии, `--serve` RPC |
| `src/tests/pgg` | gtest + корпус `corpus/` и голдены `goldens/` |
| `resources/pgg` | Продуктовые/арт-примеры (cottage, spire_house, inn_hotel, lib, …) |
| `docs/pgg` | Спецификация, реализация, cheatsheet, контракт RPC |

## Сборка

Сборка — CMake + vcpkg, как в Neverwhere: submodule `toolchain/vcpkg`, пресеты в `CMakePresets.json`, overlay `vcpkg_overlays/ports`. Первая конфигурация бутстрапит зависимости из манифеста `vcpkg.json`.

```sh
git submodule update --init toolchain/vcpkg
./build_linux.sh                          # cmake --preset linux
cmake --build --preset linux-debug --target pgg_tests
cmake --build --preset linux-debug --target PggTool
cmake --build --preset linux-debug --target PggViewer
ctest --test-dir _int_linux --output-on-failure
```

Windows: `generate_vs.bat` → `_intermediate_64\pgg.sln`, либо `cmake --build --preset debug --target pgg_tests`.
macOS: `./build_mac.sh`, затем `cmake --build --preset macos-debug --target pgg_tests`.

Бинарники: `_int_linux/src/apps/<App>/Debug/<App>` (Ninja кладёт exe в подпапку `$<CONFIG>`).

Платформенные грабли, binary cache и `compile_commands.json` — `docs/BUILD.md`.

После правок `src/libs/pgg/grammar/Pgg.g4` — `tools/pgg/regen_parser.sh` (сгенерированный парсер коммитится в `parser_gen/`).

## Инструменты

```sh
PggTool check resources/pgg/cottage.pgg
PggTool docs builtins
PggViewer resources/pgg/spire_house.pgg
PggViewer --serve                          # RPC 127.0.0.1:9878
```

MCP `pgg` / `pgg-win` — `.mcp.json` / `.cursor/mcp.json`, код `tools/pgg_mcp/`. Контракт: `docs/pgg/viewer_rpc.md`. Переменные: `PGG_REPO_ROOT`, `PGG_VIEWER`.

Инструкции для агентов — `AGENTS.md`.
