# MCP-серверы

Все серверы объявлены в `.mcp.json` и в копии `.cursor/mcp.json` (Cursor читает только этот путь). Файлы должны совпадать.

| Сервер | Назначение | Подробности |
|---|---|---|
| `pgg` / `pgg-win` | агентский цикл отладки PGG-графов (`pgg_*` поверх TCP RPC :9878; PggViewer `--serve` поднимается автоматически) | `docs/pgg/viewer_rpc.md` |

Лаунчеры: `tools/run_pgg_mcp_server.sh` / `.ps1`. Env: `PGG_REPO_ROOT`, `PGG_VIEWER`.
