"""Cross-platform PggViewer lifecycle + RPC proxy for the MCP server.

The MCP process is Python and is the same on every OS. This module finds or
starts ``PggViewer --serve`` and forwards JSON ops. If the binary is missing
it returns a structured ``need_build`` error with configure/build argv for
the current platform — the MCP never runs cmake itself.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping, Optional

from tools.pgg_mcp.rpc_client import PggRpcClient, PggRpcError, port_open, wait_for_port

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9878
_VIEWER_REL = "src/apps/PggViewer"
_VIEWER_NAME = "PggViewer"


def detect_platform(sys_platform: Optional[str] = None) -> str:
    """Map ``sys.platform`` to ``linux`` / ``macos`` / ``windows``."""
    p = (sys_platform or sys.platform).lower()
    if p.startswith("linux"):
        return "linux"
    if p == "darwin":
        return "macos"
    if p.startswith("win") or p.startswith("cygwin"):
        return "windows"
    return "linux"


def _bin(build_dir: str, config: str) -> str:
    return f"{build_dir}/{_VIEWER_REL}/{config}/{_VIEWER_NAME}"


@dataclass(frozen=True)
class ViewerRecipe:
    """Where PggViewer lives and how an agent should build it on this OS."""

    platform: str
    candidates: tuple[str, ...]
    expected: str
    configure: tuple[str, ...]
    build: tuple[str, ...]
    debug_build: tuple[str, ...]
    hint: str


def viewer_recipe(platform: str) -> ViewerRecipe:
    """Canonical search paths + build argv for ``linux`` / ``macos`` / ``windows``."""
    if platform == "windows":
        return ViewerRecipe(
            platform="windows",
            candidates=(
                _bin("_intermediate_64", "Release"),
                _bin("_intermediate_64", "Debug"),
            ),
            expected=_bin("_intermediate_64", "Release") + ".exe",
            configure=("generate_vs.bat",),
            build=("cmake", "--build", "--preset", "release", "--target", "PggViewer"),
            debug_build=("cmake", "--build", "--preset", "debug", "--target", "PggViewer"),
            hint=(
                "Run configure and build from the repo root. "
                "Retry the MCP tool afterwards; it starts PggViewer itself. "
                "Override the binary with env PGG_VIEWER."
            ),
        )
    if platform == "macos":
        return ViewerRecipe(
            platform="macos",
            candidates=(
                _bin("_int_clion_release", "Release"),
                _bin("_intermediate_64", "Release"),
                _bin("_int_clion", "Debug"),
                _bin("_intermediate_64", "Debug"),
                _bin("_int_mac", "Release"),
                _bin("_int_mac", "Debug"),
            ),
            expected=_bin("_intermediate_64", "Release"),
            configure=("./build_mac.sh",),
            build=("cmake", "--build", "--preset", "macos-release", "--target", "PggViewer"),
            debug_build=("cmake", "--build", "--preset", "macos-debug", "--target", "PggViewer"),
            hint=(
                "build_mac.sh configures the Xcode preset (Debug). "
                "macos-release is preferred (much faster). "
                "CLion: presets macos-clion / macos-clion-release. "
                "Retry the MCP tool afterwards; it starts PggViewer itself. "
                "Override the binary with env PGG_VIEWER."
            ),
        )
    return ViewerRecipe(
        platform="linux",
        candidates=(
            _bin("_int_linux_release", "Release"),
            _bin("_int_linux", "Debug"),
            _bin("_int_clion_release", "Release"),
            _bin("_int_clion", "Debug"),
        ),
        expected=_bin("_int_linux_release", "Release"),
        configure=("./build_linux.sh",),
        build=("cmake", "--build", "--preset", "linux-release", "--target", "PggViewer"),
        debug_build=("cmake", "--build", "--preset", "linux-debug", "--target", "PggViewer"),
        hint=(
            "build_linux.sh configures the Debug preset (linux). "
            "linux-release is preferred (much faster). First configure fetches vcpkg. "
            "Retry the MCP tool afterwards; it starts PggViewer itself. "
            "Override the binary with env PGG_VIEWER."
        ),
    )


def _existing_file(path: str) -> Optional[str]:
    for cand in (path, path + ".exe"):
        if os.path.isfile(cand):
            return cand
    return None


def find_viewer_binary(
    repo_root: str,
    platform: Optional[str] = None,
    environ: Optional[Mapping[str, str]] = None,
) -> Optional[str]:
    """First existing PggViewer wins. ``PGG_VIEWER`` first, then Release, then Debug."""
    env = environ if environ is not None else os.environ
    plat = platform or detect_platform()
    env_path = env.get("PGG_VIEWER")
    if env_path:
        found = _existing_file(env_path)
        if found:
            return found
        # Fall through and search presets: a stale env should not hide a real binary.

    recipe = viewer_recipe(plat)
    others = [p for p in ("linux", "macos", "windows") if p != plat]
    seen: list[str] = []
    for rel in list(recipe.candidates) + [
        c for p in others for c in viewer_recipe(p).candidates
    ]:
        if rel in seen:
            continue
        seen.append(rel)
        abs_path = rel if os.path.isabs(rel) else os.path.join(repo_root, rel)
        found = _existing_file(abs_path)
        if found:
            return found
    return None


def need_build_error(
    repo_root: str,
    platform: Optional[str] = None,
    environ: Optional[Mapping[str, str]] = None,
    *,
    message: Optional[str] = None,
) -> dict[str, Any]:
    """Structured ``ok=false`` envelope: agent should build, then retry."""
    env = environ if environ is not None else os.environ
    plat = platform or detect_platform()
    recipe = viewer_recipe(plat)
    env_path = env.get("PGG_VIEWER")
    extra = ""
    if env_path and not _existing_file(env_path):
        extra = f" PGG_VIEWER is set but not a file: {env_path}."
    return {
        "ok": False,
        "error": {
            "kind": "need_build",
            "message": (
                message
                or (
                    "PggViewer binary not found."
                    + extra
                    + " Build it from the repo root, then retry this tool."
                )
            ),
            "target": "PggViewer",
            "platform": plat,
            "cwd": repo_root,
            "configure": list(recipe.configure),
            "build": list(recipe.build),
            "debug_build": list(recipe.debug_build),
            "expected": recipe.expected,
            "candidates": list(recipe.candidates),
            "hint": recipe.hint,
            "viewer": "missing",
        },
    }


def error_envelope(kind: str, message: str, **extra: Any) -> dict[str, Any]:
    err: dict[str, Any] = {"kind": kind, "message": message}
    err.update(extra)
    return {"ok": False, "error": err}


def default_repo_root(environ: Optional[Mapping[str, str]] = None) -> str:
    env = environ if environ is not None else os.environ
    override = env.get("PGG_REPO_ROOT")
    if override:
        return override
    return str(Path(__file__).resolve().parent.parent.parent)


@dataclass
class PggSession:
    """Long-lived proxy: auto-start PggViewer, then TCP JSON-RPC."""

    repo_root: str = field(default_factory=default_repo_root)
    host: str = DEFAULT_HOST
    port: int = DEFAULT_PORT
    platform: Optional[str] = None
    environ: Optional[Mapping[str, str]] = None
    port_open_fn: Callable[..., bool] = port_open
    wait_for_port_fn: Callable[..., bool] = wait_for_port
    popen_fn: Callable[..., Any] = subprocess.Popen
    which_fn: Callable[[str], Optional[str]] = shutil.which
    client_factory: Optional[Callable[[], PggRpcClient]] = None

    _client: Optional[PggRpcClient] = field(default=None, init=False, repr=False)
    _proc: Any = field(default=None, init=False, repr=False)
    _log_file: Any = field(default=None, init=False, repr=False)
    binary_path: Optional[str] = field(default=None, init=False)

    def __post_init__(self) -> None:
        if self.platform is None:
            self.platform = detect_platform()
        if self.environ is None:
            self.environ = os.environ

    def _env(self) -> Mapping[str, str]:
        return self.environ if self.environ is not None else os.environ

    def find_binary(self) -> Optional[str]:
        return find_viewer_binary(self.repo_root, self.platform, self._env())

    def ensure(self) -> Optional[dict[str, Any]]:
        """Start PggViewer if needed. None = RPC port is accepting."""
        if self.port_open_fn(self.host, self.port):
            if self.binary_path is None:
                self.binary_path = self.find_binary()
            return None

        if self._proc is not None and getattr(self._proc, "poll", lambda: 0)() is None:
            if self.wait_for_port_fn(self.host, self.port, timeout_s=30.0, step_s=0.5):
                return None
            return error_envelope(
                "unreachable",
                "PggViewer did not open the RPC port within 30 s; see tmp/pgg_viewer_serve.log",
                log="tmp/pgg_viewer_serve.log",
            )

        viewer = self.find_binary()
        if viewer is None:
            return need_build_error(self.repo_root, self.platform, self._env())

        env = self._env()
        cmd: list[str] = [viewer, "--serve"]
        if self.platform == "linux" and not env.get("DISPLAY"):
            xvfb = self.which_fn("xvfb-run")
            if xvfb:
                cmd = [xvfb, "-a"] + cmd
            else:
                return error_envelope(
                    "unreachable",
                    "PggViewer needs a display: DISPLAY is unset and xvfb-run is not on PATH",
                    hint="Install xvfb (Debian/Ubuntu: xvfb) or run under a graphical session, then retry.",
                    log="tmp/pgg_viewer_serve.log",
                )

        log_dir = Path(self.repo_root) / "tmp"
        log_dir.mkdir(parents=True, exist_ok=True)
        # Keep the handle on the session: Popen does not own the Python file object.
        self._log_file = open(log_dir / "pgg_viewer_serve.log", "ab", buffering=0)
        popen_kw: dict[str, Any] = {
            "cwd": self.repo_root,
            "stdout": self._log_file,
            "stderr": self._log_file,
        }
        self._proc = self.popen_fn(cmd, **popen_kw)
        self.binary_path = viewer
        if self.wait_for_port_fn(self.host, self.port, timeout_s=30.0, step_s=0.5):
            return None
        poll = getattr(self._proc, "poll", lambda: None)()
        if poll is not None:
            return error_envelope(
                "unreachable",
                f"PggViewer exited early (code {poll}); see tmp/pgg_viewer_serve.log",
                log="tmp/pgg_viewer_serve.log",
                binary=viewer,
            )
        return error_envelope(
            "unreachable",
            "PggViewer did not open the RPC port within 30 s; see tmp/pgg_viewer_serve.log",
            log="tmp/pgg_viewer_serve.log",
            binary=viewer,
        )

    def _make_client(self) -> PggRpcClient:
        if self.client_factory is not None:
            return self.client_factory()
        return PggRpcClient(host=self.host, port=self.port)

    def call(self, op: str, args: Optional[dict[str, Any]] = None) -> dict[str, Any]:
        """Send one RPC op. Auto-starts the viewer. Server-side ok=false is not retried."""
        start_error = self.ensure()
        if start_error:
            return start_error

        payload: dict[str, Any] = {"op": op}
        if args:
            payload["args"] = {k: v for k, v in args.items() if v is not None}

        last_error: Any = None
        for _ in range(2):
            try:
                if self._client is None:
                    self._client = self._make_client()
                resp = self._client.call(**payload)
                if op == "status" and resp.get("ok") and isinstance(resp.get("data"), dict):
                    data = resp["data"]
                    data["viewer"] = "running"
                    if self.binary_path:
                        data["binary"] = self.binary_path
                    data["rpc"] = {"host": self.host, "port": self.port}
                return resp
            except PggRpcError as e:
                return error_envelope(e.kind, e.message)
            except (ConnectionError, OSError) as e:
                last_error = e
                if self._client is not None:
                    self._client.close()
                self._client = None
                start_error = self.ensure()
                if start_error:
                    return start_error
        return error_envelope(
            "unreachable",
            f"PggViewer RPC unreachable: {last_error}",
        )

    def status(self) -> dict[str, Any]:
        return self.call("status")
