# DrUI — UI Widget Library

Переносимая библиотека виджетов и тем поверх [Dear ImGui](https://github.com/ocornut/imgui).
Содержит кастомные виджеты, систему цветовых тем, тост-уведомления,
конфигурацию шрифтов и иконки Font Awesome.

## Зависимости

| Библиотека | Версия | Примечание |
|---|---|---|
| Dear ImGui | 1.92+ | Публичная зависимость (через CMake `PUBLIC`) |
| GLFW + OpenGL3 | — | Нужны приложению-хосту, DrUI их не линкует |

## Quick Start

### 1. Подключение через CMake

Скопируйте папку `src/libs/drui/` в ваш проект и добавьте:

```cmake
add_subdirectory(path/to/drui)
target_link_libraries(my_app PRIVATE drui)
```

### 2. Единый include

```cpp
#include <drui/drui.h>
```

Это подключит все модули: `theme.h`, `widgets.h`, `toast.h`, `icons.h`, `fonts.h`.

### 3. Инициализация

```cpp
// После ImGui::CreateContext() и до начала рендеринга:
ImFont* font = DrUI::SetupFonts(io, dpi_scale);
if (font) io.FontDefault = font;

DrUI::ApplyTheme(DrUI::ThemeId::Dark, dpi_scale);
```

### 4. Рендеринг тостов (в конце кадра)

```cpp
DrUI::ToastAnchor anchor{canvas_pos, canvas_size};
DrUI::DrawToasts(anchor);
```

## Темы

Встроенные темы:

| ID | Название |
|---|---|
| `ThemeId::Dark` | Тёмная |
| `ThemeId::Light` | Светлая |
| `ThemeId::Olive` | Оливковая |
| `ThemeId::Nord` | Nord |
| `ThemeId::Cherry` | Вишнёвая |

Переключение в рантайме:

```cpp
DrUI::ApplyTheme(DrUI::ThemeId::Nord, dpi_scale);
```

### Как добавить свою тему

1. Добавьте значение в `enum class ThemeId` (перед `COUNT`).
2. Добавьте `case` в `ThemeName()` и `SetThemeColors()`.
3. Задайте все `Colors::*` переменные.

### Палитра `DrUI::Colors`

Основные группы:

- **Background**: `BackgroundPrimary`, `BackgroundSecondary`, `BackgroundTertiary`
- **Accent**: `AccentPrimary`, `AccentSecondary`, `AccentHover`
- **Status**: `Success`, `Warning`, `Error`, `Info`
- **Text**: `TextPrimary`, `TextSecondary`, `TextDisabled`
- **Frame**: `FrameBackground`, `Border`, `FrameBorder`, `Selection`
- **Card**: `CardBg`, `CardBorder`
- **Canvas**: `CanvasBg`, `CanvasDot`, `CanvasDotMajor`, `CanvasLabel`

Хелперы для получения `ImU32`:

```cpp
DrUI::Colors::Accent(alpha);        // AccentPrimary -> ImU32
DrUI::Colors::AccentBright(factor); // осветлённый акцент
DrUI::Colors::AccentDim(factor);    // затемнённый акцент
```

## Справочник виджетов

### Button

```cpp
// Авто-обнаружение иконки FA в начале строки
if (DrUI::Button(ICON_FA_PLAY " Запуск")) { ... }

// Иконка + текст раздельно
if (DrUI::Button(ICON_FA_STOP, "Стоп")) { ... }

// С заданным размером
if (DrUI::Button("OK", ImVec2(100, 40))) { ... }
```

### IconButton

```cpp
if (DrUI::IconButton(ICON_FA_COG)) { ... }
if (DrUI::IconButton(ICON_FA_SEARCH, 32.0f)) { ... } // кастомный размер
```

### IconToggle

```cpp
static bool grid_on = false;
DrUI::IconToggle(ICON_FA_TH, &grid_on);            // дефолтный размер
DrUI::IconToggle(ICON_FA_EYE, &grid_on, 32.0f);    // кастомный размер
```

Квадратная кнопка-переключатель с иконкой:
- **Вкл**: акцентный фон, яркая иконка.
- **Выкл**: серый фон, приглушённая иконка.

### ToggleSwitch

```cpp
static bool enabled = true;
DrUI::ToggleSwitch("my_toggle", &enabled);
```

### SearchInput

```cpp
static char buf[128] = "";
DrUI::SearchInput("##search", buf, sizeof(buf));
```

### SliderFloat / SliderInt

Визуально как прогресс-бар с перетаскиваемым ползунком:

```cpp
static float val = 0.5f;
DrUI::SliderFloat("Значение", &val, 0.0f, 1.0f);

static int steps = 50;
DrUI::SliderInt("Шаги", &steps, 0, 100);
```

### InputInt / InputFloat

Поля с кнопками-степперами `+` / `-`:

```cpp
static int count = 42;
DrUI::InputInt("Count", &count, 1, 10, 80.0f);

static float rate = 3.14f;
DrUI::InputFloat("Rate", &rate, 0.1f, 1.0f, "%.2f", 80.0f);
```

### ProgressBar

```cpp
DrUI::ProgressBar(0.65f, ImVec2(-1, 0), "65%");
```

### Spinner

```cpp
DrUI::Spinner("##loading", 8.0f, 2.5f);
```

### CardBegin / CardEnd

Контейнер с фоном, рамкой и опциональным сворачиванием:

```cpp
DrUI::CardBegin(ICON_FA_COG " Настройки");
{
    // виджеты...
}
DrUI::CardEnd();

// Без сворачивания:
DrUI::CardBegin("Заголовок", false);
```

### Badge

Бейдж-счётчик в правом верхнем углу последнего виджета:

```cpp
DrUI::IconButton(ICON_FA_BOLT);
DrUI::Badge(5);
```

### GradientSeparator

```cpp
DrUI::GradientSeparator();
```

### CollapsingHeader

Без рамки:

```cpp
if (DrUI::CollapsingHeader("Раздел")) { ... }
```

### AccentBar / PanelBorder

Декоративные хелперы:

```cpp
DrUI::AccentBar(draw_list, pos, size, /*vertical=*/true);
DrUI::PanelBorder(draw_list, min, max, /*active=*/false);
```

### GradientBorder

Анимированная градиентная рамка — опорные цвета вращаются по периметру
скруглённого прямоугольника, создавая эффект переливания:

```cpp
static const ImVec4 colors[] = {
    {0.90f, 0.30f, 0.50f, 1.0f},  // розовый
    {0.30f, 0.80f, 0.95f, 1.0f},  // голубой
    {0.95f, 0.75f, 0.20f, 1.0f},  // золотой
    {0.40f, 0.90f, 0.50f, 1.0f},  // зелёный
};
DrUI::GradientBorder(draw_list, pos, size, colors, 4,
                     (float)ImGui::GetTime(),
                     /*speed=*/0.2f, /*thickness=*/2.0f,
                     /*rounding=*/12.0f, /*segments=*/200);
```

## Тост-уведомления

```cpp
DrUI::ShowToast("Операция завершена", DrUI::ToastType::Success);
DrUI::ShowToast("Внимание!", DrUI::ToastType::Warning, 5.0f);
DrUI::ShowToast("Ошибка подключения", DrUI::ToastType::Error);
DrUI::ShowToast("Информация", DrUI::ToastType::Info);
```

Типы: `Info`, `Success`, `Warning`, `Error`.

Рисовать каждый кадр:

```cpp
DrUI::ToastAnchor anchor{panel_pos, panel_size};
DrUI::DrawToasts(anchor);
```

## Иконки

Доступны через макросы `ICON_FA_*` (Font Awesome 7 Free Solid):

```cpp
ICON_FA_PLAY, ICON_FA_STOP, ICON_FA_COG, ICON_FA_SAVE,
ICON_FA_FOLDER, ICON_FA_FILE, ICON_FA_SEARCH, ICON_FA_BOLT,
ICON_FA_CHECK, ICON_FA_TIMES, ICON_FA_WARNING, ...
```

Полный список — в `icons.h`.

## Шрифты

`DrUI::SetupFonts()` загружает системный шрифт с кириллицей (Segoe UI / Arial / Tahoma)
и мержит Font Awesome иконки. Поддерживает DPI-масштабирование и лестницу размеров
для `PushFont`.

Файл шрифта иконок (`fa-solid-900.otf`) ищется в:
- `<exe_dir>/resources/fonts/`
- `resources/fonts/` (относительно CWD)
- `../../../resources/fonts/`

## Структура файлов

```
src/libs/drui/
  CMakeLists.txt   — сборка библиотеки
  drui.h           — единый фасад
  theme.h          — ThemeId, Colors::*, ApplyTheme()
  widgets.h        — все виджеты
  toast.h          — тост-уведомления
  icons.h          — ICON_FA_* макросы
  fonts.h          — SetupFonts() (декларация)
  fonts.cpp        — SetupFonts() (реализация)
  README.md        — эта документация
```
