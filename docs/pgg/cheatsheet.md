# PGG cheatsheet (арт-итерации)

Каталог билтинов **не копировать**: `PggTool docs builtins | rg -i <имя>`, карточка — `pgg_docs("clip")` или `pgg_docs("builtin:clip")` (def загруженного файла побеждает одноимённый builtin). Перед grep по спеке/ядру — эта страница, потом §8.

## Выражения и атрибуты

`sin`/`cos`/`atan2` — **радианы**; градусы → `radians()`. `mix`/`clamp`/`smoothstep`, `vec3()`, касты. `@P`/`@N`/`@index`/`@iteration`/`@tint`/`@Cd`; маска группы — `ingroup("brick")`.

## Идиомы

```
c = ingroup("brick") ? brick_col * @tint : c0
acc = repeat (empty_mesh(), iterations = n) |acc| { acc = merge(acc, piece_k) }
inst = realize(instance_on_points(pts, source = a0, variants = [a0, a1]))
punched = clip(g, origin = o, normal = n, cap_group = "brick")
edged = compute_normals(bevel(box(size), width = w, bevel_group = "edge"), mode = flat)
tube = sweep(mesh_line(count = 2, length = h, dir = (0, 1, 0)), profile = ring)
```

Массив вокруг Y: `shapes.polar_array(g, n = 6)`. Четыре стены ящика: `shapes.around_box(facade, w, d)`. Многоугольник: `shapes.ngon_frustum` — радиусы **апофемы**. Кладка: сердцевина `blockwork.core_prism` + `brick_veneer*` + `masonry.clip_*_hole`. Скруглённый план: `plan.plan_prism` / `plan.arc_shell`.

Топология поселения — `lib.settlement` (пример `resources/pgg/hamlet.pgg`): `plaza_outline` → `spoke_model` → `spoke_roads`/`cross_paths` (ленты), `plaza_anchors`/`spoke_anchors` (якоря с `@orient/@scale/@variant/@ring/@t` — здания инстансит сцена), `voronoi_ground` (ячейки по сайтам через `repeat`, группы meadow/field/hedge), `hedges`, `tree_sites`. Другая деревня = seed / `n` / радиусы. Согласование «вершина площади ↔ спица ↔ сосед» — через одинаковые `(rng, key, counter)` в поле и в `value(random(...), on = row)`; соседний элемент — `counter = (@index + 1) % n` (+ `alias_rng`).

## Грабли

- Вызов — **одна строка**: перевод строки внутри списка аргументов = E100 «extraneous input '\n'»; длинные `merge`/таблицы констант — одной длинной строкой или через промежуточные binding'и. Строчный `#`-комментарий при схлопывании съедает хвост строки — выносить в шапку блока.

- Тернарник **не** выбирает `geo` — `select(cond, a, b)` или отдельные def.
- Вертикальный `sweep`: `@profile_scale = (depth, width)`, не `(width, depth)`.
- `bevel` на `sweep` с несколькими кольцами пути даёт канавку на каждом кольце — клёпка читается стопкой. Швы досок — зазор между экземплярами + `compute_normals(auto)`.
- `parts.cbox` фаска `k` от **min(размер)**; на этаже — `k_abs` или голый `box`.
- Нейтраль merge/repeat: `empty_mesh()` / `empty_points()` (`mesh_line(count = 0)` — точки).
- `orient_from_euler(vec3)` — градусы, GLM `quat(radians)`: pitch X, yaw Y, roll Z.
- Апофема/описанный: `shapes.ngon_circumradius(apothem, sides)`; вынос пояса — **по нормали к грани**.
- Нахлёст черепицы — только вдоль ската (`pv < tl`); отступ по нормали константа (`lift`), иначе верхние ряды отрываются.
- Шпиль: полый `sweep`-настил без крышки + наконечник изнутри = колодец. Латунный конус — те же грани, что черепица (апофема `a0*(h−y)/h`, чуть уже); юбка — тонкий карниз на стыке, не площадка; коньки заходят под юбку, не до оси.
- RPC `render` **stateless**, кроме `orbit`/`distance`. Опущенный `wire`/`ortho`/`target`/`chrome` сбрасывается. Форму — с `wire:false`; «вижу не то» — сначала `render_state`.
- Сварка вершин — `merge_by_distance`; `weld` — параметр `mirror`.
- `grid()` → `geo<mesh>`, `instance_on_points` хочет points: якоря — `plan.point_grid` / `mesh_line`+`foreach`. `grid` уже в XZ, нормали +Y.
- Обёртка `def clip_arch_hole` вокруг `masonry.clip_arch_hole` (то же имя) допустима: индексы `foo[k]` общие на имя, не на модуль.
- Булевых вычитаний меша нет: не строить тело в проёме или резать `clip`/`masonry.clip_*`.
- Имя param def'а = top-level param → E102/E105; top-level с префиксом (`body_w`).
- `parts.piece`/`cbox` дополнительно помечают фаски в `edge`.
- Красить только реальные группы (`stats.groups` / чтение def'а); иначе E305.
- `@tint` points + faces в одном `merge` → E609.
- `bake_ao` гасит эмиссию — `separate` светящихся **до** бейка, `merge` после. Эмиссия ≤ 1.0.
- Def, пишущий атрибут по `@P` (ramp формы по высоте), — **после** `transform` на место, иначе читает координаты до переноса (`scarecrow.pgg`: spine сначала translate, потом `coat_shape`).
- `set("profile_scale", vec2(r, r))` без поля/`domain = points` уходит в detail — `sweep` не масштабирует профиль (цилиндр радиуса `circle`, не `r`). Нужен `@index` или явный `domain = points`.
- Чтение `@attr` резолвит имя в порядке **points → corners → faces** и интерполирует (среднее). `promote` **не** удаляет исходную колонку, поэтому после `promote(..., to = faces, mode = first)` чтение `@cell` на гранях всё равно берёт points-версию средним — «first» невидим. Нужна faces-версия — другое имя (`set(..., domain = faces)`) или `remove_attr` источника. Граница ячеек по углам — дисперсия: `mean(id²) − mean(id)² > 0`.
- `fbm` **знаковый** (≈ −0.7…0.8, медиана 0): в `mix(a, b, fbm)` подавать `0.5 + 0.5 * fbm`; порог «треть площади» — `fbm > 0.15`, не `> 0.5`.
- `distance_to` считается только на **points**: в плотность `distribute_points` (грани) — сначала `set(g, "d", distance_to(...), domain = points)` → `promote(to = faces)` → `@d`.
- Кортеж `(a, b)` из идентификаторов не парсится (только литералы) — `vec2(a, b)`; `grid(res = vec2(f32(n), f32(n)))`.
- Вложенный `foreach`: целевое имя внутреннего цикла **не** должно совпадать с портом внешнего (`row = foreach s in ...` → E204 «never bound»); `pins = foreach ...` и затем `row = pins`.
- `select(cond, a, b)` в теле `foreach` принимает value-bool из `value(random(...), on = row) < p` — так делаются пропуски (`b = empty_points()` / `empty_mesh()`).
- `expect` — только в начале тела def, до первого binding'а.

## MCP (одна строка)

`pgg_docs("clip")` · `pgg_render(node="house", view="front", wire=False, chrome="off", file=…)` · `pgg_render(..., compare="prev"|"baseline", png=False)` · `pgg_contact_sheet(views="*")` · `pgg_measure(node="house", a="group:stone", b="group:brick", axis="x")` · `pgg_probe("house:bbox[group=stone]")` · `pgg_reference(image=..., node="hotel")` · `pgg_status()` → `kind=need_build` → собрать PggServe из `configure`/`build` и retry

## Обвязка

`PggTool check` — корневой `.pgg` (нет `--lib`; подмодуль без локального `lib/` раньше был E501). `resources/pgg` подмешивается как запасной import root, поэтому `import lib.*` работает и из `resources/AmberEstate/`. Аргументы: `pgg_docs(symbol=…)`, `pgg_reference(image=…)`. Слот = файл: на `render`/`probe` передавайте `file=`, если в этом ходе не было `pgg_load`. `fmt --check` на закоммиченных примерах может расходиться — канон не enforced.
