# Галерея

Геройские кадры арт-примеров для корневого `README.md` (превью на GitHub).

Папка названа `docs/gallery/`: коротко, рядом с остальной документацией, относительные ссылки из README стабильны на GitHub.

| Файл | Сцена |
|---|---|
| `spire_house.png` | викторианский дом со шпилем (`resources/AmberEstate/spire_house.pgg`) |
| `cottage.png` | фахверковый коттедж (`resources/AmberEstate/cottage.pgg`) |
| `inn_hotel.png` | угловой отель streamline moderne (`resources/pgg/inn_hotel.pgg`) |
| `stone_arch.png` | арка из тёсаных блоков (`resources/AmberEstate/stone_arch.pgg`) |

## Как снято

Окно PggViewer 1440×900, превью на весь кадр (`--chrome=off`), кроп до картинки модели (`--shot-frame=preview`), MSAA превью 8×. Цвета `@Cd`, шейдинг `auto`. Орбита — ¾ перспективы, не орто: yaw / pitch в градусах, третий компонент — множитель fit-дистанции (`1` = вписать bounding-sphere). После съёмки `tools/pgg/crop_gallery.py` обрезает PNG в квадрат вокруг силуэта (фон = clear-цвет превью, запас 10 %), чтобы на GitHub модель не терялась в широком кадре.

| PNG | Узел | `--preview-orbit` | Зачем такой ракурс |
|---|---|---|---|
| `spire_house.png` | `house` | `38,18,1.02` | фасад, крыльцо, эркер и шпиль в одном ¾ |
| `cottage.png` | `house` | `28,14,1.00` | дверь на +Z, фахверк, фронтон и дым |
| `inn_hotel.png` | `hotel` | `45,28,1.08` | ракурс референса: круглый угол в (+X,+Z) |
| `stone_arch.png` | `scene` | `25,14,0.82` | чуть сбоку, чтобы читались клинья и проём |

## Как переснять

Нужен собранный `PggViewer` (лучше **Release** — Debug на этих сценах в разы медленнее), `xvfb-run` либо `DISPLAY`, и Python 3 с Pillow (как у MCP: `python3 -c "from PIL import Image"`).

```sh
# Linux Release
cmake --build --preset linux-release --target PggViewer
./tools/pgg/regen_gallery.sh
```

Другой бинарь: `PGG_VIEWER=/path/to/PggViewer ./tools/pgg/regen_gallery.sh`.

Скрипт перезаписывает четыре PNG в этой папке. После правки `.pgg` достаточно прогнать его и закоммитить картинки вместе с моделью.

Один кадр вручную:

```sh
PggViewer resources/AmberEstate/cottage.pgg \
    --preview=house --preview-orbit=28,14,1.00 \
    --chrome=off --shot-frame=preview \
    --shot=docs/gallery/cottage.png
python3 tools/pgg/crop_gallery.py docs/gallery/cottage.png
```
