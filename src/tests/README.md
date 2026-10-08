# C++ tests (Edgar parity / port)

Тесты собираются как цели CMake в этом каталоге. Основные исполняемые файлы:

| Target | Источник | Назначение |
|--------|-----------|------------|
| `edgar_tests` | `edgar_tests.cpp` | Юнит-тесты на C++ (Graph, GridPolygon, IntVector2, …) через Catch2 |
| `edgar_parity_tests` | `edgar_parity_tests.cpp` | Тесты, ориентированные на паритет с `_edgar_ref` и точечные C# сценарии |

## Сборка и запуск (Windows, MSVC)

Из корня репозитория (после конфигурации CMake; в репозитории часто используется каталог `_build`, см. корневой [README.md](../../README.md)):

```bat
cmake --build _build --config Debug
ctest --test-dir _build -C Debug --output-on-failure
```

Или только тестовые бинарники:

```bat
_build\bin\Debug\edgar_tests.exe
_build\bin\Debug\edgar_parity_tests.exe
```

## Definition of Done и матрица C# → C++

- **DoD итерации 0** (SA, штрафы, КП, overlap на эталонах): [docs/parity_dod.md](../../docs/parity_dod.md)
- **Матрица** всех `*Tests.cs` в трёх проектах `_edgar_ref` → покрытие в C++: [docs/test_matrix_iteration0.md](../../docs/test_matrix_iteration0.md)

Итерация 0 по документам считается закрытой, когда матрица и DoD актуальны, заготовка `test_data/parity/` на месте и **`ctest` (Debug) без падений**.

## Golden data

Заготовка каталога для эталонных данных: [test_data/parity/README.md](../../test_data/parity/README.md). Отдельный **скрипт** smoke golden на итерации 0 **не обязателен** — достаточно договорённостей в `test_data/parity/` и зелёного `ctest`.

## CMake

Цели и линковка описаны в `CMakeLists.txt` в этом каталоге.
