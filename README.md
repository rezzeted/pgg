# PGG

PGG is a procedural geometry language: a text-first node graph for LLM agents, with a node projection for humans.

It used to live inside [Neverwhere](https://github.com/icecampus/neverwhere). This repository is a standalone implementation: library, CLI, viewer, tests, and art examples.

| Path | What it is |
|---|---|
| `src/libs/pgg` | Library: parser (ANTLR4 4.13.2), AST, execution core |
| `src/apps/PggTool` | CLI: `check` / `fmt` / `ast` / `run` / `docs` / `diff` |
| `src/apps/PggViewer` | Node projection + geometry preview, `--serve` RPC |
| `src/tests/pgg` | gtest + `corpus/` fixtures and `goldens/` fingerprints |
| `resources/pgg` | Product / art examples (cottage, spire_house, inn_hotel, lib, …) |
| `docs/pgg` | Language spec, implementation notes, cheatsheet, RPC contract |

## Build

Build is CMake + vcpkg, same as Neverwhere: submodule `toolchain/vcpkg`, presets in `CMakePresets.json`, overlay `vcpkg_overlays/ports`. The first configure bootstraps dependencies from `vcpkg.json`.

```sh
git submodule update --init toolchain/vcpkg
./build_linux.sh                          # cmake --preset linux
cmake --build --preset linux-debug --target pgg_tests
cmake --build --preset linux-debug --target PggTool
cmake --build --preset linux-debug --target PggViewer
ctest --test-dir _int_linux --output-on-failure
```

Windows: `generate_vs.bat` → `_intermediate_64\pgg.sln`, or `cmake --build --preset debug --target pgg_tests`.
macOS: `./build_mac.sh`, then `cmake --build --preset macos-debug --target pgg_tests`.

Binaries: `_int_linux/src/apps/<App>/Debug/<App>` (Ninja puts the exe under `$<CONFIG>`).

Platform notes, binary cache, and `compile_commands.json` — `docs/BUILD.md`.

After changing `src/libs/pgg/grammar/Pgg.g4`, run `tools/pgg/regen_parser.sh` (the generated parser is committed under `parser_gen/`).

## Tools

```sh
PggTool check resources/pgg/cottage.pgg
PggTool docs builtins
PggViewer resources/pgg/spire_house.pgg
PggViewer --serve                          # RPC 127.0.0.1:9878
```

MCP servers `pgg` / `pgg-win` — `.mcp.json` / `.cursor/mcp.json`, code in `tools/pgg_mcp/`. Contract: `docs/pgg/viewer_rpc.md`. Env: `PGG_REPO_ROOT`, `PGG_VIEWER`.

Agent instructions: `AGENTS.md`.
