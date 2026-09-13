# Сборка: платформенные особенности, зависимости, индексация

_Команды configure/build/test по платформам — в `AGENTS.md` → «Сборка». Здесь — грабли портов, системные пакеты, binary cache, индексация для clangd._

## macOS: платформенные особенности порта

- **CLion:** пресеты `macos-clion` (Ninja, `_int_clion`, Debug + compile_commands) и `macos-clion-release` (`_int_clion_release`, Release с оптимизациями) — CLion не поддерживает Xcode-генератор основного пресета. `macos-index` (`_intermediate_ide`) оставлен под индексацию clangd, в IDE его не используем.
- **sokol_app на macOS** требует Objective-C++: `main.cpp` standalone-приложений компилируется как ObjC++ через макрос `nw_configure_sokol_app(target)` в `cmake/utils.cmake` (там же линковка `Cocoa/QuartzCore/Metal/MetalKit`; на Windows — `d3d11/dxgi`). Все новые standalone sokol-приложения — через этот макрос.
- **Sokol-overlay несёт патч `macos-framebuffer-readback.patch`** (`vcpkg_overlays/ports/sokol`): `CAMetalLayer.framebufferOnly = false`, иначе текстуру drawable нельзя прочитать и `PggViewer --shot` на Metal не работает (readback — blit → shared-текстура → `getBytes`).
- **glad нельзя включать в TU с `SOKOL_IMPL`:** sokol_gfx тянет системные GL-заголовки, которые конфликтуют с glad-макросами.
- После обновления Xcode **сбрасывать CMake-кэш** (`cmake -U LIBRESOLV` или чистый `_intermediate_64`): `find_library` кэширует пути внутрь старого SDK.

## vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.

### Binary cache (сетевой)

Все configure-пресеты в `CMakePresets.json` читают зависимости из общего HTTP binary cache (`https://cache.blackbox9.cc:9443`, readwrite). Авторизация **не хранится в репозитории**: пресет подставляет `$env{NEVERWHERE_VCPKG_CACHE_AUTH}`:

```bash
export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"
```

Переменная не установлена — не страшно: конфигурация не падает; чтение из кэша работает и без авторизации (см. `docs/VCPKG_CACHE.md`), но push не пройдёт. Подстановка `$env{}` работает только при запуске configure через пресет (`build_mac.sh`/`build_linux.sh`/`cmake --preset ...`).

## Индексация для IDE/clangd

Основной Windows-флоу — Visual Studio generator, который **не** умеет эмиттить `compile_commands.json`. Для clangd есть отдельный Ninja-preset, который только конфигурирует (не собирает).

- **Preset:** `ide-index` в `CMakePresets.json` — Ninja generator, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, отдельный `binaryDir = _intermediate_ide`.
- **Обёртка:** `generate_compile_commands.bat` — вызывает `vcvars64.bat`, конфигурит, копирует/симлинкает `compile_commands.json` в корень репо.
- **Запуск:** `generate_compile_commands.bat` (или `--clean` для полного ребилда кэша).
- **Когда перегенерировать:** после добавления/удаления исходников, изменения compile-флагов или `vcpkg.json`.
- `_intermediate_ide/` и `compile_commands.json` в `.gitignore`.

## Linux: системные пакеты и особенности порта

- Системные зависимости (Ubuntu) для сборки и Sokol/X11: `build-essential pkg-config ninja-build python3 python3-venv libx11-dev libxi-dev libxcursor-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev`.
- **sokol_app на Linux** идёт через X11 (на Wayland-сессии — XWayland): макрос `nw_configure_sokol_app(target)` линкует `X11 Xi Xcursor GL dl pthread m`.
- `compile_commands.json` в корне — симлинк на `_int_linux/compile_commands.json` (preset сам эмиттит, отдельный index-preset не нужен).
- **PggServe / PggViewer `--shot` без DISPLAY:** `xvfb-run -a _int_linux/src/apps/PggServe/Debug/PggServe`; `xvfb-run -a _int_linux/src/apps/PggViewer/Debug/PggViewer --shot=…`.
