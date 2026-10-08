# LevelSynth — ImGui + SDL3 + OpenGL3

Приложение **LevelSynth** на C++20 с Dear ImGui, SDL3 и OpenGL 3 (CMake-проект в репозитории: `ImguiPlayground`). Библиотека **edgar** — порт [Edgar-DotNet](https://github.com/OndrejNepozitek/Edgar-DotNet) для процедурной раскладки комнат. Сборка через **CMake**; зависимости задаются **манифестом vcpkg** ([`vcpkg.json`](vcpkg.json)), сам **vcpkg** — **git submodule** в [`toolchain/vcpkg`](toolchain/vcpkg).

---

## Требования

- **CMake** 3.20+
- **Git** (для submodule `toolchain/vcpkg`)
- **Компилятор** с поддержкой C++20 (на Windows — Visual Studio 2022 или новее, x64; [`build_vs.bat`](build_vs.bat) ориентирован на **Visual Studio 18 2026**)
- **OpenGL**
- После клона: `git submodule update --init --recursive` и один раз `bootstrap-vcpkg.bat` / `bootstrap-vcpkg.sh` в `toolchain/vcpkg`

---

## Сборка (vcpkg)

Каталог сборки задаётся пресетом (по умолчанию **`_build`** в корне репозитория, см. [`CMakePresets.json`](CMakePresets.json)); при ручном `cmake -B` используйте согласованный путь.

Проще всего: из корня репозитория запустить [`build_vs.bat`](build_vs.bat) — он вызывает **`cmake --preset vs2026`** и **`cmake --build --preset debug`** (все пути vcpkg, triplet и overlay заданы в [`CMakePresets.json`](CMakePresets.json)).

Ручная конфигурация (эквивалент по смыслу):

```batch
cmake -S . -B _build -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%CD%\toolchain\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_OVERLAY_PORTS=%CD%\toolchain\vcpkg-overlay\ports
cmake --build _build --config Debug
```

Либо пресеты [`CMakePresets.json`](CMakePresets.json): `cmake --preset vs2026`, затем `cmake --build --preset release` (triplet **`x64-windows-static`**, toolchain и overlay в пресете). Для Ninja без VS: `cmake --preset default`. Для VS 2022: пресет `vs2022` и build `debug-vs2022` / `release-vs2022`.

## Сборка (macOS)

Требования: Xcode Command Line Tools (Apple Clang с C++20), CMake 3.21+, Ninja (`brew install ninja`). После клона: `git submodule update --init --recursive` и один раз `./toolchain/vcpkg/bootstrap-vcpkg.sh`.

```sh
cmake --preset macos            # Ninja, triplet arm64-osx, Debug (каталог _build)
cmake --build --preset debug-macos
cmake --preset macos-release    # Release в отдельном каталоге _build-release
cmake --build --preset release-macos
```

Исполняемый файл: `_build/bin/main` (Release: `_build-release/bin/main`), тесты — `_build/bin/edgar_tests` и др.; запуск тестов: `ctest --test-dir _build` (или `_build-release`). Бенчмарк генерации: `python3 tools/benchmark_layout_generation.py --check` (использует `_build-release`, fallback `_build`; кроссплатформенная замена `tools/benchmark_layout_generation.ps1`).

Особенности macOS: OpenGL линкуется как системный фреймворк (не XQuartz libGL); контекст запрашивается **OpenGL 3.2 Core** (macOS не поддерживает Core 3.0), GLSL `#version 150`; путь к exe определяется через `_NSGetExecutablePath`. Диалога сохранения на macOS нет — экспорт JSON пишет `layout_export.json` рядом с рабочим каталогом.


Исполняемый файл приложения: `_build/bin/<Config>/main.exe`. Тесты: `_build/bin/<Config>/edgar_tests.exe`, `edgar_parity_tests.exe`, `preset_loader_tests.exe`, `generation_diagnostic_test.exe`.

Пакеты из манифеста устанавливаются в каталог **`vcpkg_installed/`** рядом с билдом (в `.gitignore`).

### Clipper2

Пересечение полигонов в edgar использует **Clipper2** той же версии, что и порт vcpkg (**2.0.1**), но библиотека **собирается из исходников** через FetchContent в [`src/libs/edgar/CMakeLists.txt`](src/libs/edgar/CMakeLists.txt), чтобы статический бинарник совпадал с вашим MSVC (предсобранный `Clipper2.lib` из vcpkg на другой машине может давать `LNK2019 __std_rotate` при смешении версий toolset).

---

## Зависимости (vcpkg.json)

| Порт | Назначение |
|------|------------|
| nlohmann-json, fmt, stb | edgar: JSON, PNG |
| spdlog | логи (edgar/диагностика; тест `generation_diagnostic_test`) |
| yaml-cpp | пресеты карт и YAML в приложении (`preset_loader`) |
| boost-graph | edgar: планарные грани, проверка планарности (Boost.Graph) |
| gtest | тесты |
| sdl3, imgui (+ docking, sdl3, opengl3) | окно и ImGui |

---

## Структура проекта

```
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── toolchain/vcpkg/          # git submodule vcpkg
├── thirdparty/CMakeLists.txt # find_package + imgui_impl INTERFACE
├── src/libs/edgar/           # библиотека edgar (генерация уровней)
├── src/libs/drui/            # темы, тосты, иконки поверх ImGui
├── src/apps/main/
├── src/tests/
├── test_data/                # сценарии для ручных/parity-прогонов (см. test_data/parity/)
├── resources/edgar_gui/      # копия из референса (см. ниже)
└── docs/
    port_vs_original_gap.md
    port_parity_roadmap.md
    parity_dod.md
    test_matrix_iteration0.md
    …
```

### Приложение LevelSynth (main) и YAML

Паритет сценариев с Edgar.GUI (ресурсы, экспорт): [`docs/app_gui_parity.md`](docs/app_gui_parity.md). Схема ключей YAML пресетов: [`docs/app_yaml_preset.md`](docs/app_yaml_preset.md).

- **Корень ресурсов по умолчанию:** при старте ищется каталог `resources/edgar_gui`, содержащий подпапки **`Maps/`** и **`Rooms/`**: обход вверх от каталога `main.exe` (удобно при запуске из `_build/bin/...` в клоне репозитория). Рядом с exe CMake **копирует** `resources/edgar_gui` из репозитория.
- **Каталог карт:** в выпадающем списке показываются только **`.yml`/`.yaml` непосредственно в `Maps/`** (без рекурсии в подпапки). Подпись в UI указывает на `<repo>/resources/edgar_gui/Maps/`.
- **Панель Map:** комбо выбора карты, отображение текущего пути ресурсов, кнопка **Reload catalog**. Экспорт JSON — через меню **File → Export JSON** (на Windows — диалог сохранения).
- **Перетаскивание на окно:** можно сбросить **папку** с `Maps/` и `Rooms/` (корень `edgar_gui`) или отдельный файл карты — каталог обновится соответственно.
- **CLI:** опциональный аргумент — путь к файлу **`*.yml` / `*.yaml`** карты; загружается эта карта, база ресурсов выводится из пути (см. [`preset_loader.cpp`](src/apps/main/preset_loader.cpp)).

### Ресурсы Edgar.GUI (копия из референса)

Каталог [`resources/edgar_gui/`](resources/edgar_gui) — это **не** самостоятельные ассеты проекта, а **копия** дерева **`src/Resources`** из upstream [Edgar-DotNet](https://github.com/OndrejNepozitek/Edgar-DotNet) (WinForms-проект `Edgar.GUI`: `MapDescriptions`, `Maps`, `Images`, `Rooms`, `RandomGraphs` и т.д.). У себя их можно заново скопировать из локального клона референса, например:

```batch
robocopy %CD%\_edgar_ref\src\Resources %CD%\resources\edgar_gui /E
```

При сборке `main` CMake **копирует** `resources/edgar_gui` рядом с `main.exe` в `resources/edgar_gui/`, чтобы пути относительно исполняемого файла совпадали с ожидаемой раскладкой папок.

---

## Технические особенности приложения

### Статическая линковка SDL3 на Windows

- Точка входа — свой `main()`, не SDL_main.
- Перед любыми включениями SDL задаётся **`SDL_MAIN_HANDLED`**.
- Подключается **`<SDL3/SDL_main.h>`** и перед `SDL_Init()` вызывается **`SDL_SetMainReady()`**.
- В SDL3 **`SDL_Init()`** при успехе возвращает **`true`**, при ошибке — **`false`**.

### Окно и OpenGL

- Окно: `SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY`.
- HiDPI: `ImGuiConfigFlags_DpiEnableScaleFonts` и `ImGuiConfigFlags_DpiEnableScaleViewports`.
- Контекст OpenGL: Core Profile 3.0.

### Шрифты

- Windows: Segoe UI / Arial из `C:\Windows\Fonts\`.
- Linux: DejaVu / Liberation в типичных путях.

---

## Тесты

- `enable_testing()` в корневом CMake, цели в [`src/tests/`](src/tests/).
- Исполняемые файлы: **`edgar_tests`**, **`edgar_parity_tests`**, **`preset_loader_tests`**, **`generation_diagnostic_test`** (в `bin/<Config>/`).
- Запуск: `ctest -C Debug` (или `Release`) из каталога сборки.

---

## Стиль и документация

- Код на **C++20**.
- **Порт edgar:** генерация и ограничения (энергия, конфигурационные пространства, двери) приводятся к соответствию с Edgar-DotNet; подробности — [`docs/port_vs_original_gap.md`](docs/port_vs_original_gap.md), roadmap — [`docs/port_parity_roadmap.md`](docs/port_parity_roadmap.md). Итерация 0 (агент): [`docs/iteration_0_agent_brief.md`](docs/iteration_0_agent_brief.md). Критерии parity: [`docs/parity_dod.md`](docs/parity_dod.md), матрица тестов: [`docs/test_matrix_iteration0.md`](docs/test_matrix_iteration0.md).

---

## Лицензия

Проект **LevelSynth** распространяется под лицензией **MIT**.
