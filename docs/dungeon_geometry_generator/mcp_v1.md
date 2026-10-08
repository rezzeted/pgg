# DungeonGeometryGenerator: MCP и агентский RPC v1 (DungeonGeometryGeneratorServe + dungeon_geometry_generator_mcp)

_Нормативный документ машинной петли поверх демона. Правя
`src/apps/DungeonGeometryGeneratorServe/` или `tools/dungeon_geometry_generator_mcp/`, обновляй **этот** документ._

Цепочка: агент ↔ MCP-сервер `dungeon_geometry_generator` (`tools/dungeon_geometry_generator_mcp`, FastMCP stdio) ↔ TCP
RPC ↔ `DungeonGeometryGeneratorServe` (`src/apps/DungeonGeometryGeneratorServe`, C++) ↔ библиотеки конвейера
(F1–F11).

Зачем, если есть DungeonGeometryGeneratorCli (docs/dungeon_geometry_generator/cli_v1.md): CLI проходит петлю «проект →
экспорт» одним вызовом на шаг, и каждый вызов холодный — проект, каталог и
fill стартуют с нуля. DungeonGeometryGeneratorServe держит проекты в памяти: слот с тёплым
F8-кэшем юнитов, перечтение проекта по mtime, инкрементальный fill — правка
fill-яруса стоит доли полного прогона. Это и есть «машинная петля» из
requirements §7: правка файла → вызов → диагностика с кодом → правка.

## DungeonGeometryGeneratorServe

```
DungeonGeometryGeneratorServe [--port N] [--host 127.0.0.1] [--assets <dir>] [--smoke]
```

TCP RPC на `127.0.0.1:9879` (порт по умолчанию; env `DUNGEON_GEOMETRY_GENERATOR_SERVE_PORT`).
Построчный JSON, один запрос — одна строка, ответ — одна строка. GPU нет —
демон чисто CPU, xvfb не нужен. Занятый порт — ошибка, exit 1 (MCP в этом
случае подключается к уже работающему демону). Библиотека ассетов резолвится
один раз на старте (`dungeon_geometry_generator::find_dungeon_geometry_generator_assets`: `./assets` от cwd → вверх от
exe → вверх от проекта) или явным `--assets`; без неё `fill` отвечает
error-конвертом D100, а `asset_check` — ok:true с D100-диагностикой и hint'ом
про `--assets`.

Архитектура (по образцу PggServe, контракт — docs/pgg/serve_rpc.md):

- `ServeRpcServer` — транспорт: конверт, клиенты, deferred-ответы
  (`reply`/`replyError` из воркеров), «текущий файл» каждого TCP-клиента.
- `ServeRuntime` — слоты, LRU, пул воркеров `clamp(hardware_concurrency, 2, 4)`,
  все 12 операций.
- `ProjectSession` — состояние одного проекта (слот).

`poll()` пайплайн не исполняет: хендлер захватывает clientId, ставит задачу в
пул и отвечает deferred; воркер под мьютексом слота исполняет и шлёт ответ.
`ping`/`status` — inline в poll-потоке (быстрые, слота не касаются).

### Слоты

Ключ слота — **канонический путь проекта** (`weakly_canonical`), не агент и
не uuid. В слоте: `project`, `layoutData` (хендофф dungeon-geometry-generator-layout/0), IR,
`FillResult`, **тёплый `UnitCache` (F8)**, mtime проекта, статы последних
шагов. Потолок — 4 слота (LRU; вытесняется не-в-работе через try_lock, все
заняты → `busy`). На одном слоте работа сериализуется мьютексом; разные слоты
fill'ятся параллельно (кэши раздельные, глобального состояния в fill нет).

Каждый TCP-клиент имеет «текущий файл» — последний `load` **этого сокета**.
Слотовые ops принимают необязательный `file`; без него — текущий файл клиента,
в ответ добавляется `"note": "used the client current file"`. В ответах
слотовых ops эхо `"session": {"file": "…"}`.

### Конверт и ошибки

```json
→ {"op": "fill", "args": {"file": "…"}}
← {"ok": true, "data": {…}}
← {"ok": false, "error": {"kind": "no_file", "message": "…"}}
```

Протокольные `kind`: `bad_json`, `unknown_op`, `bad_args`, `no_file` (file не
задан и текущего нет, или слот неизвестен; hint «call load first» — в
message), `not_found` (комната/ключ provenance), `no_layout`, `no_fill`,
`io_error` (запись артефактов), `busy`, `internal`.

Падение шага пайплайна — error-конверт с `kind` = D-коду шага (реестр классов
— docs/dungeon_geometry_generator/cli_v1.md): D200 (инвариант 5.2/5.4), D300/D301 (каталог/раскладка),
D400 (слот R-A3), D500 (прогон PGG); `message` — err библиотеки дословно, со
сквозными кодами R-A3/PGG внутри.

`load`/`validate`/`check`/`asset_check` — **всегда ok:true**: результат в
`data.diagnostics` (массив `{code, message, hint?, warning?}` через
`dungeon_geometry_generator::diags_to_json`) + `data.has_errors`. Это принципиально: красная
диагностика — штатный ответ петли, а не сбой транспорта.

### Операции

| op | args | data |
|---|---|---|
| `ping` | — | `{pong, app:"DungeonGeometryGeneratorServe", protocol:1}` |
| `status` | — | `{slots:[{file,has_layout,has_ir,has_fill}], uptime_s, port, assets_dir, workers}` |
| `load` | `path` | `{file, diagnostics, has_errors}` — создать слот + проверить проект; при `has_errors` слот не создаётся. Ставит текущий файл клиента. Явный `load` существующего слота сохраняет его F8-кэш |
| `validate` | `file?` | перечитать проект в копию (слот read-only), `{diagnostics, has_errors}` |
| `layout` | `file?, seed?, attempts?` | `{seed_used, attempt_used, ms}`; `seed` применяется к локальной копии проекта. Заменяет layoutData, сбрасывает IR/fill, кэш сохраняется |
| `ir` | `file?, out?` | `{format:"dungeon-geometry-generator-ir/3", text}` или `{wrote}` при `out` |
| `fill` | `file?, threads?` | `{rooms, bodies, facings, nodes, doors, lamps, occupied, reused, reran, ms}` (`reused`/`reran` — счётчики кэша F8; `occupied` — занятые цилиндры реестра C4) |
| `check` | `file?, unit?` | `{errors, diagnostics:[{code:"D600",…}], has_errors}` (F11; fill обеспечивается автоматически). Повторный check на тёплом слоте реплеит elements-вердикты reused-юнитов из F8-кэша (B3) — секунды. `unit` — подстрока id юнита: только per-unit проверка elements по совпавшим юнитам (быстрый предчек арт-итерации; глобальные проверки пропускаются; ноль совпадений — ошибка; в ответе `units` — число совпавших) |
| `export` | `file?, out, name?, split_groups?` | `{written:[…]}`; `out` обязателен; `name` по умолчанию — stem проекта (голый `project.json` → имя его каталога) |
| `units` | `file?` | `{units:[{id, slot, mesh:[begin,end], anchors:[begin,end]}]}` из последнего fill, иначе `no_fill` |
| `provenance` | `file?, room, key?` | `{room, entries:{key: format_prov(chain)}}` по `ir.rooms[i].prov`; нет комнаты/ключа → `not_found` |
| `asset_check` | `file?, slot, asset` | `{diagnostics, has_errors}` — `dungeon_geometry_generator::check_asset` + contract lint `dungeon_geometry_generator::lint_asset` (при чистой статике: один прогон с синтетическими входами слота + строгая схема выхода; коды `dungeon_geometry_generator/slot` и `dungeon_geometry_generator/lint`; падение синтетического прогона — warning «inconclusive»); import-roots = проектные `asset_roots` + библиотека ассетов dungeon_geometry_generator; относительный `asset` ищется в библиотеке, затем в проектных roots |

### Семантика fill (тёплая петля)

1. Нет `layoutData` в слоте → авто-layout с дефолтами (первый `fill`
   холодный: каталог + dungeon_topology_generator + полный fill).
2. Проект dungeon-geometry-generator-project/0 (нет layout-яруса) → `no_layout` с hint'ом:
   frozen-IR ввод serve v1 не принимает — собери dungeon-geometry-generator-project/1 или иди
   через DungeonGeometryGeneratorCli.
3. mtime файла проекта изменился → проект перечитывается (ошибка →
   error-конверт D1xx). Ассеты перечитывать не нужно: контентные ключи F8
   инвалидируют юниты сами.
4. IR пересобирается **всегда** из сохранённого `layoutData`
   (Refill-семантика вьювера — дёшево относительно fill).
5. `fill_level` с тёплым `UnitCache` слота: правка fill-яруса (высота двери,
   плотность декора, стили) даёт частичную инвалидацию — `reused>0`,
   `reran` только затронутые юниты.
6. **Правка layout-яруса** (граф, комнаты, каталог, seed) требует явного
   `layout`: `fill` раскладку не перегенерирует и использует сохранённую.
   Это задокументированная консервативная семантика, а не упущение.

## MCP-сервер `dungeon_geometry_generator` (`tools/dungeon_geometry_generator_mcp`)

Регистрация — `.mcp.json` и `.cursor/mcp.json` в корне репо (одинаковая
запись):

```json
"dungeon_geometry_generator": {"type": "stdio", "command": "python3",
          "args": ["-m", "tools.dungeon_geometry_generator_mcp.launch"], "cwd": "${workspaceFolder}",
          "env": {"PYTHONUTF8": "1", "PYTHONIOENCODING": "utf-8",
                  "PYTHONUNBUFFERED": "1"}}
```

`launch.py` — stdlib-bootstrap: python ≥ 3.10, venv `tools/dungeon_geometry_generator_mcp/.venv`,
`pip install -r requirements.txt` (единственная зависимость `mcp>=1.0,<2.0`),
`os.execv` в venv-python `-m tools.dungeon_geometry_generator_mcp`. Корень репо — от `__file__`;
env `DUNGEON_GEOMETRY_GENERATOR_REPO_ROOT` (с expanduser) перекрывает — в регистрации не задаётся.

**DungeonGeometryGeneratorServe поднимается автоматически** (`session.py`, `DungeonGeometryGeneratorSession`): если
порт не отвечает, ищется бинарь по рецептам ОС (macOS
`_int_clion/src/apps/DungeonGeometryGeneratorServe/{Debug,Release}`; Windows
`_intermediate_64/.../DungeonGeometryGeneratorServe.exe`; Linux `_int_linux/...`),
`Popen([serve, "--port", N, "--host", H])`, ожидание порта, retry ×2.
GPU нет — никакого xvfb. Бинаря нет → MCP **не** собирает: любой инструмент
отвечает конвертом `{"ok": false, "error": {"kind": "need_build", "message",
…, "build": [{"argv": […], "cwd": …}, …], "target", "platform", "expected",
"candidates", "hint"}}` с готовыми командами сборки под текущую ОС — агент
выполняет их и повторяет вызов.

**Пересобранный бинарь подхватывается сам**: MCP помнит mtime бинаря, с
которым поднял свой DungeonGeometryGeneratorServe. Бинарь новее → аккуратный перезапуск и
replay: заново `load` всех известных проектов (ответ с `restarted:true`,
`restart:{binary, reloaded_slots, failed_slots?}`). Чужой процесс на порту не
трогается: если он старше последней сборки (по `uptime_s` из `status`), к
ответам добавляется `stale_binary` — остановите его, MCP поднимет новый.

**Одно TCP-соединение на in-flight вызов** (не сокет на процесс MCP): два
чата через FastMCP иначе смешают JSON. Python-сессия помнит `last_file`
последнего успешного `dungeon_geometry_generator_load` и подставляет его fallback'ом в `file=`;
два чата на одном MCP могут гонять этот fallback — **передавайте `file=`**,
если в этом ходе не было `dungeon_geometry_generator_load`.

Инструменты (тонкие прокси; конверты `error`/`need_build`/`stale_binary`
возвращаются как есть, не бросаются):

`dungeon_geometry_generator_status` · `dungeon_geometry_generator_load(path)` · `dungeon_geometry_generator_validate(file?)` ·
`dungeon_geometry_generator_layout(file?, seed?, attempts?)` · `dungeon_geometry_generator_ir(file?, out?)` ·
`dungeon_geometry_generator_fill(file?, threads?)` · `dungeon_geometry_generator_check(file?, unit?)` ·
`dungeon_geometry_generator_export(out, file?, name?, split_groups?)` · `dungeon_geometry_generator_units(file?)` ·
`dungeon_geometry_generator_provenance(room, file?, key?)` · `dungeon_geometry_generator_asset_check(slot, asset, file?)`

## pgg-слой (отладка ассетов)

В том же MCP — прокси к PggServe (порт 9878, env `PGG_SERVE_PORT`; инстансия
`PggSession` с рецептами бинаря монорепо, корень pgg-репо —
env `PGG_REPO_ROOT` или корень монорепо; бинарь — env `PGG_SERVE`
или кандидаты пресетов pgg). Подъём ленивый — pgg-процесс не стартует до
первого вызова `pgg_*`; на Linux PggServe стартует с `--headless` (GLX
pbuffer, окна нет — gpuReady синхронно), без DISPLAY — поверх через
`xvfb-run -a` (нет xvfb → `unreachable` с hint'ом). `need_build`
— с командами сборки pgg от корня монорепо (см. AGENTS.md).
Контракт ops — docs/pgg/serve_rpc.md.

Инструменты: `pgg_status` · `pgg_load(path, lib_roots?)` ·
`pgg_params(params, file?)` · `pgg_render(node, file?, out?, size?, ortho?,
target?, orbit?, zoom?)` · `pgg_probe(file?, spec?, specs?)` ·
`pgg_docs(symbol, file?)`.

Зачем: `dungeon_geometry_generator_asset_check` отвечает «слот принял/отверг ассет и почему», но
не показывает геометрию. Когда ассет надо чинить, агент идёт в pgg-слой:
`pgg_probe` (числа: bbox, счётчики, hist по атрибутам) и `pgg_render` (кадр)
по тому же `.pgg` — без полного fill уровня; PggServe сам перечитывает
правки .pgg по mtime (F4).

Слот-ассеты dungeon_geometry_generator объявляют **входы слота без дефолтов** (`seg`/`cuts`/
`zones` у facing и т.п.) — `pgg_load` на них отвечает E604, это штатно:
перед probe/render входы связываются `pgg_params` фикстурами рядом с ассетом
(значение `"@<файл>"` грузит pgg-points/1 относительно каталога .pgg:
`{"seg": "@facing_v1.seg.points.json", …}`). Выходы слот-ассетов — узлы
`mesh` и `anchors` (контракт R-A3): `pgg_probe(spec="mesh:stats")`,
`pgg_render(node="mesh")`.

`lib_roots` по умолчанию (если не задан) — из dungeon-geometry-generator-контекста: каталог самого
ассета + `<dungeon_geometry_generator>/assets` (покрывает `import codes` / `import patterns` —
единственные импорты dungeon-geometry-generator-ассетов); при загруженном dungeon-geometry-generator-проекте добавляются
`project.dir` и его `asset_roots`. Сверху PggServe дописывает свой
`resources/pgg`.

Биндинги `pgg_params` переживают перечитывание файла (явный `pgg_load` и F4
auto-reload по mtime) — PggServe восстанавливает значение по имени параметра;
неизвестные имена отвечают `unknown` + `suggestions` (did-you-mean по
объявленным параметрам). Относительный `out=` у `pgg_render` резолвится от
корня dungeon-geometry-generator-репо (не pgg), `None` — серверный дефолт `tmp/pgg_rpc_shots/`
pgg-репо.

## Петля агента

```
dungeon_geometry_generator_load(path)                # слот + статическая проверка
dungeon_geometry_generator_fill()                    # холодный: авто-layout + полный fill
# правка fill-яруса проекта на диске (высота двери, декор, стили…)
dungeon_geometry_generator_fill()                    # тёплый: проект перечитан по mtime, reused>0
dungeon_geometry_generator_check()                   # F11: errors == 0
dungeon_geometry_generator_export(out="out/")        # obj + anchors + units + ir
dungeon_geometry_generator_units() / dungeon_geometry_generator_provenance(room="hall")   # инспекция результата
# отладка слот-ассета без полного fill:
dungeon_geometry_generator_asset_check(slot="facing", asset="walls/facing_v1.pgg")
pgg_load → pgg_params(фикстуры входов) → pgg_probe / pgg_render  # тот же .pgg
# правка графа/комнат/каталога:
dungeon_geometry_generator_layout() → dungeon_geometry_generator_fill()   # layout явно, дальше та же петля
```

## Тесты

- `DungeonGeometryGeneratorServe_smoke` (ctest, ~12 с): полный сценарий из 16 шагов — протокол,
  D100/D101 при load, авто-layout, тёплый fill (`reran==0`), частичная
  инвалидация после правки fill-яруса (`reused>0` и `reran>0`), check,
  export (4 артефакта), units, provenance, asset_check (clean + dungeon_geometry_generator/slot),
  два клиента параллельно, no_file.
- `DungeonGeometryGeneratorServe_rpc_py` (ctest, python3 stdlib): живой демон на свободном
  порту, ping/status/load/validate по сырому сокету — проверка связки
  «python ↔ DungeonGeometryGeneratorServe» без venv и mcp-пакета.
- `python3 -m unittest tools.dungeon_geometry_generator_mcp.test_session tools.dungeon_geometry_generator_mcp.test_pgg_layer`
  — DI-юниты (67 шт., без venv; `mcp` импортирует только `server.py`):
  need_build, автоподъём, stale_binary, restart+replay, retry, ленивость
  pgg-слоя, авто `lib_roots`, xvfb, форварды pgg-инструментов.

## Грабли

- `fill` не перечитывает layout-ярус: после правки графа/комнат — явный
  `layout`, иначе петля едет по старой раскладке молча.
- Четвёртый+ проект вытесняет LRU-слот вместе с тёплым кэшем — повторный
  fill вытесненного проекта снова холодный.
- `check`/`export` без предварительного `fill` честно обеспечивают fill —
  первый такой вызов может быть холодным (десятки секунд в Debug).
- Тестовый `src/tests/data/d2_project.json` не проходит F4 по 5.2
  (коридорные `wall_t` против проектных) — для smoke-клиентов используй
  `src/apps/DungeonGeometryGeneratorViewer/smoke_project.json`.
- Занятый 9879: второй DungeonGeometryGeneratorServe не стартует (exit 1); MCP подключится к
  уже работающему. Если на порту чужой старый демон — см. `stale_binary`.
- `load` с `has_errors:true` слот не создаёт: последующий `fill` по этому
  пути ответит `no_file`, пока проект не починен и не загружен заново.
