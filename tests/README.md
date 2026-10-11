# Тесты

Все C++-сьюты (gtest), тестовые данные и test-only утилиты монорепозитория.
Собираются как цели CMake из этого каталога (`add_subdirectory(tests)` в
корневом `CMakeLists.txt`).

## Макет

| Путь | Что это |
|------|---------|
| `pgg/` | Сьюта `pgg_tests` (`*_test.cpp`), корпус `corpus/`, голдены фингерпринтов `goldens/` |
| `dungeon_geometry_generator_*_test.cpp` | Сьюты DungeonGeometryGenerator (project, layout, ir, topo, assets, d0, fill, check, export) |
| `data/` | Фикстуры сьют DungeonGeometryGenerator (`DUNGEON_GEOMETRY_GENERATOR_TEST_DATA`) |
| `dungeon_topology_generator*_tests.cpp`, `preset_loader_tests.cpp`, `generation_diagnostic_test.cpp`, `parity_golden_test.cpp` | Сьюты level-synth (порт Edgar-DotNet) |
| `test_data/` | Данные level-synth: `gui_presets/` (карты для preset_loader/бенчмарка), `parity/` (golden-паритет с C#, см. `test_data/parity/README.md`) |
| `benchmark_layout.cpp` | Бенчмарк раскладок; smoke-гейт `BenchmarkLayout.Smoke` в ctest, пороги потоньше — `tools/benchmark_layout_generation.py` |
| `parity_runner_cs/` | C#-раннер регенерации эталонов parity (нужен локальный клон `_edgar_ref`); собирается dotnet'ом, в CMake не входит |

## Сборка и запуск

Из корня репозитория (пресеты и платформенные детали — `docs/BUILD.md`):

```sh
cmake --build --preset linux-debug          # или debug (VS) / macos-debug (Xcode)
ctest --test-dir _int_linux --output-on-failure
```

Бинарники ложатся в `<бинарная директория>/tests/<suite>` (например
`_int_linux/tests/pgg_tests`).

Перезапись голденов pgg — из корня репозитория:

```sh
PggTool run tests/pgg/corpus/<file>.pgg --update-goldens
```

## level-synth: паритет с Edgar-DotNet

- **DoD итерации 0** (SA, штрафы, КП, overlap на эталонах): [docs/level-synth/parity_dod.md](../docs/level-synth/parity_dod.md)
- **Матрица** всех `*Tests.cs` в трёх проектах `_edgar_ref` → покрытие в C++: [docs/level-synth/test_matrix_iteration0.md](../docs/level-synth/test_matrix_iteration0.md)
- Golden-пайплайн (логическое сравнение с C#): [test_data/parity/README.md](test_data/parity/README.md)
