# MCP-серверы

Все серверы объявлены в `.mcp.json` и в копии `.cursor/mcp.json` (Cursor читает только этот путь). Файлы должны совпадать.

| Сервер | Назначение | Подробности |
|---|---|---|
| `pgg` | агентский цикл отладки PGG-графов (`pgg_*` поверх TCP RPC :9878) | `docs/pgg/viewer_rpc.md` |

Один сервер на все ОС: Cursor стартует `python3 -m tools.pgg_mcp.launch` (venv, затем stdio MCP). На Windows, если в PATH нет `python3`, в той же записи замените `command` на `python` — вторую запись заводить не нужно. Дальше `PggSession` сам поднимает `PggViewer --serve` или отвечает `need_build`.

Обёртки `tools/run_pgg_mcp_server.sh` / `.ps1` — только для запуска из терминала. Env: `PGG_REPO_ROOT`, `PGG_VIEWER`.
