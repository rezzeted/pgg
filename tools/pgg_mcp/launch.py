"""Bootstrap the PGG MCP venv (stdlib only) and exec the stdio server.

Cursor (and OpenCode) start this module as the single MCP server ``pgg``:

    python3 -m tools.pgg_mcp.launch

Interpreter discovery, venv, and ``pip install`` live here so every OS shares
one bootstrap. Optional terminal wrappers: ``tools/run_pgg_mcp_server.sh`` /
``.ps1``.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional


def _repo_root() -> Path:
    env = os.environ.get("PGG_REPO_ROOT")
    if env:
        return Path(env).resolve()
    return Path(__file__).resolve().parent.parent.parent


def _python_is_310(exe: str) -> bool:
    if not exe:
        return False
    try:
        proc = subprocess.run(
            [exe, "-c", "import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    except OSError:
        return False
    return proc.returncode == 0


def _windows_py_launcher() -> Optional[str]:
    try:
        proc = subprocess.run(
            ["py", "-3", "-c", "import sys; print(sys.executable)"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    path = (proc.stdout or "").strip()
    return path or None


def pick_bootstrap_python() -> str:
    """Python ≥ 3.10 to create/use the venv. Prefer the current interpreter."""
    candidates: list[str] = []
    if sys.executable:
        candidates.append(sys.executable)
    for name in ("python3", "python"):
        found = shutil.which(name)
        if found:
            candidates.append(found)
    for path in (
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3",
    ):
        candidates.append(path)
    py_exe = _windows_py_launcher()
    if py_exe:
        candidates.append(py_exe)

    seen: set[str] = set()
    for exe in candidates:
        try:
            resolved = str(Path(exe).resolve())
        except OSError:
            continue
        if resolved in seen:
            continue
        seen.add(resolved)
        if os.path.isfile(resolved) and _python_is_310(resolved):
            return resolved
    raise SystemExit(
        "tools.pgg_mcp.launch: no suitable python found (need python3 >= 3.10)"
    )


def _venv_python(venv: Path) -> Path:
    if os.name == "nt":
        return venv / "Scripts" / "python.exe"
    return venv / "bin" / "python"


def _mcp_importable(py: Path) -> bool:
    proc = subprocess.run(
        [str(py), "-c", "import mcp"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return proc.returncode == 0


def ensure_venv(root: Path, bootstrap_py: str) -> Path:
    venv = root / "tools" / "pgg_mcp" / ".venv"
    py = _venv_python(venv)
    req = root / "tools" / "pgg_mcp" / "requirements.txt"
    if not py.is_file():
        print(f"tools.pgg_mcp.launch: creating venv {venv} with {bootstrap_py}",
              file=sys.stderr)
        subprocess.check_call([bootstrap_py, "-m", "venv", str(venv)])
    if not _mcp_importable(py):
        subprocess.check_call(
            [str(py), "-m", "pip", "install", "-q", "-r", str(req)]
        )
    return py


def main() -> None:
    root = _repo_root()
    os.chdir(root)
    os.environ["PGG_REPO_ROOT"] = str(root)
    os.environ.setdefault("PYTHONUTF8", "1")
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    existing = os.environ.get("PYTHONPATH", "")
    os.environ["PYTHONPATH"] = (
        str(root) if not existing else str(root) + os.pathsep + existing
    )

    # This file must parse on macOS system Python 3.9: we re-exec 3.10+ here.
    if sys.version_info < (3, 10):
        bootstrap = pick_bootstrap_python()
        if os.path.realpath(bootstrap) == os.path.realpath(sys.executable or ""):
            raise SystemExit(
                "tools.pgg_mcp.launch: no suitable python found (need python3 >= 3.10)"
            )
        os.execv(bootstrap, [bootstrap, "-m", "tools.pgg_mcp.launch"] + sys.argv[1:])

    bootstrap = pick_bootstrap_python()
    py = ensure_venv(root, bootstrap)
    os.execv(str(py), [str(py), "-m", "tools.pgg_mcp"] + sys.argv[1:])


if __name__ == "__main__":
    main()
