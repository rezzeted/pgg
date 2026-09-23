# MCP-серверы

Все серверы объявлены в `.mcp.json` и в копии `.cursor/mcp.json` (Cursor читает только этот путь). Файлы должны совпадать.

| Сервер | Назначение | Подробности |
|---|---|---|
| `pgg` | агентский цикл отладки PGG-графов (`pgg_*` поверх TCP RPC :9878) | `docs/pgg/serve_rpc.md` |

Один сервер на все ОС: Cursor стартует `python3 -m tools.pgg_mcp.launch` (venv, затем stdio MCP). На Windows, если в PATH нет `python3`, в той же записи замените `command` на `python` — вторую запись заводить не нужно. Дальше `PggSession` сам поднимает `PggServe` или отвечает `need_build`; после пересборки свой `PggServe` перезапускается на следующем вызове (`restarted:true`, слоты перезагружаются), чужой — помечается `stale_binary` (`docs/pgg/serve_rpc.md` § MCP). После смены контракта (PggViewer `--serve` → `PggServe`) в уже открытом Cursor нажмите Reload у сервера `pgg`: stdio-процесс кэширует старый Python.

В `mcp.json` не задавайте `PGG_REPO_ROOT=${workspaceFolder}`. Если папка открыта как `~/...`, Cursor кладёт эту тильду в env как есть (cwd при этом уже абсолютный). `Path.resolve()` тильду не раскрывает, путь склеивается с cwd и `chdir` падает — сервер в панели красный. Корень launch берёт из пути модуля. Явный `PGG_REPO_ROOT` с `~` раскрывается; если каталога нет, старт не обрывается, берётся checkout, где лежит `launch.py`.

Обёртки `tools/run_pgg_mcp_server.sh` / `.ps1` — только для запуска из терминала. Env: `PGG_REPO_ROOT`, `PGG_SERVE` (опционально путь к бинарю).
