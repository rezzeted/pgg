# Golden parity (итерация 0 → этап E, реализовано)

Каталог **golden-пайплайна** логического сравнения C++-порта с референсным C# (локальный клон `_edgar_ref`, коммит `258c83a`).

**Принцип (решение владельца, 2026-09-29):** сравнение **только логическое** — состав комнат/коридоров, отсутствие пересечений, двери на каждом ребре графа, контуры только из разрешённых шаблонов. Побайтовое совпадение layout/PNG не требуется: потоки RNG у реализаций разные.

## Макет

```
test_data/parity/
  README.md           (этот файл)
  scenarios/*.json    (входы: граф, шаблоны rect+simple-doors, seed, ожидаемые счётчики)
  actual/             (результаты прогонов: <name>.cs.json / <name>.cpp.json; генерируются)
```

## Сценарии

- `four_room_cycle` — цикл из 4 комнат, 2 шаблона (квадрат 8, прямоугольник 6×10).
- `three_room_corridor_line` — линия из 3 комнат с коридором посередине (коридор — stage-2 комната, как в C#).
- `six_room_star` — звезда из 6 комнат.

## Как прогнать

1. **C#-сторона** (нужен .NET SDK и клон `_edgar_ref` в корне репозитория):

```sh
dotnet build tools/parity_runner_cs -c Release
for s in four_room_cycle three_room_corridor_line six_room_star; do
  dotnet run --project tools/parity_runner_cs -c Release --no-build -- \
    test_data/parity/scenarios/$s.json test_data/parity/actual/$s.cs.json
done
```

2. **C++-сторона** — тест `parity_golden_test` (suite `EdgarGoldenParity`): генерирует layout по тому же сценарию, пишет `actual/<name>.cpp.json` и проверяет логические инварианты у обоих движков. Если `<name>.cs.json` отсутствует — тест **skip** (валидность C++ всё равно проверяется).

```sh
ctest --test-dir _build -R EdgarGoldenParity --output-on-failure
```

## Проверяемые инварианты (обе реализации)

- число комнат и коридоров соответствует сценарию;
- ни одна пара комнат не пересекается по площади;
- каждое ребро графа реализовано дверью в layout;
- контур каждой комнаты (bbox) принадлежит пулу шаблонов её типа (basic/corridor), с учётом поворотов.

## Связь с тестами

- C++ тесты: [src/tests/README.md](../../src/tests/README.md), `edgar_tests`, `edgar_parity_tests`, `parity_golden_test`.
- Матрица покрытия C#: [docs/test_matrix_iteration0.md](../../docs/test_matrix_iteration0.md) (все строки закрыты: done или skip(na)).
