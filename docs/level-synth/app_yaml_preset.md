# YAML пресеты Edgar.GUI в LevelSynth (`preset_loader`)

Формат совпадает с ресурсами upstream [Edgar-DotNet](https://github.com/OndrejNepozitek/Edgar-DotNet) (`src/Resources`). Реализация: [`src/apps/main/preset_loader.cpp`](../src/apps/main/preset_loader.cpp).

## Структура каталога

- **Базовая папка** (как `resources/edgar_gui`): должны существовать подкаталоги `Maps/` и при необходимости `Rooms/`.
- **`Rooms/*.yml`**: наборы комнат (`name`, опционально `default`, `roomDescriptions` с полигонами и `doorMode`).
- **`Maps/*.yml`**: описание карты (см. ниже). Загрузчик **сканирует только файлы непосредственно в `Maps/`** (без рекурсии в подпапки вроде `Maps/Thesis/`).

## Ключи в `Maps/*.yml`

| Ключ | Поддержка |
|------|-----------|
| `roomsRange` (`from`, `to`) | да |
| `passages` (пары int) | да |
| `rooms` (переопределения комнат / `roomShapes`) | частично (см. парсер) |
| `defaultRoomShapes` (`setName`, `roomDescriptionName`, `scale`) | да |
| `customRoomDescriptionsSet` | да |
| `corridors` (`enable`, `offsets`, `corridorShapes`) | да |

Ключи, не перечисленные выше, игнорируются при разборе.

## Ошибки загрузки

Используйте [`load_preset_catalog_with_status`](../src/apps/main/preset_loader.hpp): при сбое поле `error` содержит текст (отсутствующий путь, нет `Maps/`, ошибка YAML).

## Тестовые данные

Минимальный комплект для GTest: [`test_data/gui_presets`](../test_data/gui_presets).
