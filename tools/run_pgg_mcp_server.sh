#!/bin/sh
# Optional terminal wrapper for the PGG MCP server (the Cursor entry is
# ``python3 -m tools.pgg_mcp.launch``). Finds a python, then execs launch.
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"
export PGG_REPO_ROOT="$REPO_ROOT"
export PYTHONIOENCODING="${PYTHONIOENCODING:-utf-8}"
export PYTHONUTF8="${PYTHONUTF8:-1}"

_pick_python() {
    for candidate in \
        /opt/homebrew/bin/python3 \
        /usr/local/bin/python3 \
        "$(command -v python3 || true)" \
        /usr/bin/python3 \
        "$(command -v python || true)"; do
        [ -n "$candidate" ] || continue
        [ -x "$candidate" ] || continue
        echo "$candidate"
        return 0
    done
    return 1
}

BASE_PY="$(_pick_python)" || {
    echo "run_pgg_mcp_server.sh: no python3/python found" >&2
    exit 1
}

exec "$BASE_PY" -m tools.pgg_mcp.launch
