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

Мощение — `resources/Roads/paving.pgg` (примеры `Roads/paved_roads.pgg`, `Roads/crossroad.pgg`, `Roads/crossroad_rounded.pgg`): `pave_annulus` (дуги вразбежку) / `pave_avenue` (ряды поперёк S-оси) / `pave_junction` (круглая площадка с градиентом размера), `border_ring`/`border_avenue`/`border_row`/`border_corner` (поребрики; corner — четверть дуги в центр перекрёстка, касательная к обеим линиям), перекрёстки = медальоны + `cull_discs` (выкус по облаку центров с `@jr`) либо сплошное полотно + `cull_box` (выкус прямоугольника) и сетка-заплатка `pave_cross_patch` (коробка + угловые клинья, @variant = 2), подрезка полотна по грани поребрика клипами: плоскости для прямых, `clip_arc_cut` для угловых дуг (инстансинг по кускам через `pave_realize`, @tint запекается до резки). Якоря камней — `sett(...)`: `@orient/@scale/@tint/@variant` (0 рядовой, 1 бордюр), один `instance_on_points` с `variants`; `@tint` запекать в `@Cd` после `realize`, если цвет нужен в OBJ.

## Грабли

- Вызов — **одна строка**: перевод строки внутри списка аргументов = E100 «extraneous input '\n'»; длинные `merge`/таблицы констант — одной длинной строкой или через промежуточные binding'и. Строчный `#`-комментарий при схлопывании съедает хвост строки — выносить в шапку блока.

- Тернарник **не** выбирает `geo` — `select(cond, a, b)` или отдельные def. Обратно: `select` — **только** geo (на vec3/числах E204), там — тернарник. `select` ленив лишь при константном условии; при вычисленном строятся **обе** ветки — `expect` в «невзятой» ветки тоже стреляет (не собирать разнотипные узлы под один `select`: kind-диспетчер с `win_lancet` упал на expect от подвальных размеров).
- Дуга в плане: `plan.arc_shell`/`arc_ring` — это sweep-профиль, и на вертикальном пути профиль X→мировой Z, Y→мировой X: точка дуги `(r·sin a, r·cos a)`, т.е. **a=0 → +Z, a=90 → +X** (у `plan.arc_anchors` — наоборот, a=0 → +X: в одном узле не смешивать). Вертикальная арочная полоса/тимпан из `arc_shell` НЕ собирается — брать торус `fixtures.ring` (или `ironwork.spool`-диск) + 2 клипа по пятам: плоскости через центр окружности, нормали (∓cos(half_a), sin(half_a), 0), `half_a = asin(w/2R)`.
- `pgg_params` переживает `pgg_load`, но НЕ авто-reload по mtime: после правки файла значения сбрасываются в дефолт файла — переключатели детализации выставлять заново после каждой правки.
- `clip(g, origin, normal)` оставляет сторону, **куда** указывает normal ((P−origin)·n > 0) — инверсия нормали = пустой меш без ошибок. Крышки `cap_group` наследуют атрибуты первой резаной грани — цвет (`@tint` → `@Cd`) запекать **до** клипа. Дуга из клипов — сектора по идиоме `lib.masonry clip_wedge_out` (2 секущие через центр + касательная на биссектрисе с cap, merge секторов).
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
- `expect` — только в начале тела def, до первого binding'а. Вычисляемые величины (поля/зазоры) инлайнить в условие expect, а не выносить в binding выше.
- `value(@index)` в zone-контексте даёт **0** (виртуальная колонка) — индекс передавать аргументом: `helper(v = v, i = @piece_index)` (идиома `split_child` в `lib/layout/split`).
- `remove_attr(geo, name)` — параметра `domain` нет. `split_rng(parent, key)` — `key: any` (counter-варианта нет; per-элементный rng — `key = @piece_index` в зоне).
- Def-параметр не принимает field-значение (`helper(i = @index)` — E204 «expects int, got field»): поле вычисляется в контексте геометрии узла (`set_position(pos = vec3(<тернарники от @index>)`) или передаётся value-контекстом зоны (`@piece_index`).
- Вычитание выпуклого объёма клипами — **объединение** кусков «снаружи каждой плоскости» (`merge` по плоскостям, идиома `masonry.clip_*_hole`); цепочка `clip(clip(…))` даёт ∩outside — пустой меш.
- `foreach` по мешу не может вернуть точки: итерироваться по вспомогательной points-модели (напр. eave-плоскостям), кусок меша выбирать `delete(g, where = @island_id != isl, domain = faces)`.
- `value()` на однограневом меше падает E601 (точек много) — читать face-атрибуты через `max_of`/`min_of`.
- `select` строит обе ветки: строители веток обязаны терпеть «чужие» параметры (в диспетчере `roof_rect` hip/mansard с w < d — кламп/поворот рамки, иначе expect невзятой ветки стреляет).
- enum-литерал vs одноимённый binding (v1.29): в сравнениях с enum-операндом литерал побеждает любой нестроковый binding (`gable = <mesh>` не ломает `kind == gable`); строковый binding по-прежнему побеждает.
- `value()` на открытой схеме — **провизорный f32** (§8.10): vec3-атрибут модели, который def'ы ниже по конвейеру читают через `value()`, не доезжает (`dot(value(@size, on = s), (1,0,0))` — статический E204). Размеры моделей — f32-колонками (`@w/@h/@d`, не vec3 `@size`; идиома `lib/layout/scope`); `@P` читается всегда (встроенный).
- Пустой списочный литерал `[]` **не парсится** (ни дефолт параметра, ни аргумент): «variants по умолчанию» — отдельные def'ы (`stamp` / `stamp_variants` в `lib/layout/scope`).
- Имя параметра не может быть ключевым словом (`repeat`/`foreach`/`in`/…): вызов `split(..., repeat = true)` — E100. Флаг повтора в `lib/layout/split` называется `cycle`.
- Дефолты параметров — **только литералы**: `= vec3(0, 0, 1)` не парсится (E100 каскадом), писать `= (0, 0, 1)`.
- Закрывающая `}` зоны — **на своей строке**: stmt внутри зоны требует NEWLINE, `{ s = f() }` в одну строку — E100 «unclosed block».
- Пустая модель со схемой: `set(empty_points(), "size", 0.0, domain = points)` × все колонки — иначе первый читающий агрегат (`sum_of(@size, on = pat)`) падает E302 (идиома `pattern_empty` из `lib/layout/split`).
- enum-параметры (v1.28): `param roof_kind: enum {hip, gable} = hip`, сравнение `kind == hip` работает в теле def и на top-level; launch-привязка строкой (`--param roof_kind=gable`, `pgg_params`); чужое значение — E206, определённый binding литералом не перечитывается.
- `@index` после `merge` перенумерован **глобально** (0..n−1) — дедуп одинаковых точек: `foreach p in all { earlier = delete(all, where = @index >= @piece_index); dup = delete(earlier, where = length(@P - value(@P, on = p)) > eps); p = select(count(dup) > 0, a = empty_points(), b = p) }` (идиома `edge_ends` в `lib/layout/edges`).
- У ребра `@pitch` — уклон ската-владельца, **не** наклон ребра (у горизонтальных рёбер мансарды pitch ≠ 0); наклон самого ребра — `@tilt` (`place_edge` учитывает). Выступ терминала на ребре — в локальный +X: чтобы он оказался снаружи (`@out`), ход ребра p0→p1 — **против** локального +X фасада (идиома `band_edges`).
- Агрегат по возможно-пустому набору: подмешать фиктивный элемент-подпорку (`max_of(@y1, on = merge(ridges, pad))` — без коньков вернётся значение подпорки; идиома `chimney_rule` в `lib/arch/elements`).
- `plan_rect`: рёбра по индексам 0/1/2/3 = лево/фронт(+Z)/право/тыл; локальный +X фасада: фронт +X, тыл −X, лево +Z, право −Z — симметричный паттерн это переживает, слот на конкретной оси (дверь на +wing_x) считать руками. `edges_of_polygon` идёт CCW: выступ терминала в локальный +X окажется ВНУТРИ — для выступающих терминалов (карнизы) рёбра брать из `band_edges` (ход против +X фасада); симметричным профилям (`rail_run`) направление безразлично.
- Пояс-периметр с фаской (`parts.piece`) НЕ раскладывается на ребровые отрезки: фаска `0.12·min(size)` у отрезка считается от выноса (не от высоты ступени) и торцы дают фаски-швы на углах — силуэт меняется. Поэтому `belt_edges` (lib/arch/elements) держит плиты целиком и читает из модели рёбер только размах (прямоугольный контур; ломаный — `cornice_run` без фаски).
- Раскладка «крылья + центр»: один `pattern_add`-ряд «поле → окна×n (repeat) → flex-стена центра → окна×n → поле» (flex-стена absorbs остаток — идиома `wing_pattern` в `spire_house/windows.pgg`); дверь — слот `K_DOOR` в том же ряду (терминал двери ставится отдельно), n_wing-параметр — repeat-зоны по `n_wing − 1`.

## MCP (одна строка)

`pgg_docs("clip")` · `pgg_render(node="house", view="front", wire=False, chrome="off", file=…)` · `pgg_render(..., compare="prev"|"baseline", png=False)` · `pgg_contact_sheet(views="*")` · `pgg_measure(node="house", a="group:stone", b="group:brick", axis="x")` · `pgg_probe("house:bbox[group=stone]")` · `pgg_reference(image=..., node="hotel")` · `pgg_status()` → `kind=need_build` → собрать PggServe из `configure`/`build` и retry

## Обвязка

`PggTool check` — корневой `.pgg` (нет `--lib`; подмодуль без локального `lib/` раньше был E501). `resources/pgg` подмешивается как запасной import root, поэтому `import lib.*` работает и из `resources/AmberEstate/`. Аргументы: `pgg_docs(symbol=…)`, `pgg_reference(image=…)`. Слот = файл: на `render`/`probe` передавайте `file=`, если в этом ходе не было `pgg_load`. `fmt --check` на закоммиченных примерах может расходиться — канон не enforced.
