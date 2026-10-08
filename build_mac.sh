#!/bin/sh
# macOS: конфигурация Xcode-проекта через preset `macos` (vcpkg manifest доустановит
# зависимости под arm64-osx при первом запуске).
# Сборка: cmake --build --preset macos-debug (все таргеты: pgg, dungeon_geometry_generator, dungeon_topology_generator)
set -e
cd "$(dirname "$0")"
cmake --preset macos
