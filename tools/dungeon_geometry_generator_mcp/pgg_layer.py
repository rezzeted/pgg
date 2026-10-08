"""pgg layer of the dungeon_geometry_generator MCP server: a lazy PggServe proxy with dungeon-geometry-generator-aware
``lib_roots`` — slot-asset debugging (probe/render of a single .pgg) without a
full level fill.

No ``mcp`` import here on purpose: the DI unit tests drive this module with
the system python (no venv). ``server.py`` wraps these methods into FastMCP
tools.

DungeonGeometryGenerator context matters: PggServe runs with cwd = the pgg repo root, but dungeon_geometry_generator
assets live in the dungeon_geometry_generator repo. So relative asset paths are absolutized against
the dungeon_geometry_generator repo root (when they exist there), and the default ``lib_roots`` are
derived from the dungeon_geometry_generator side: the asset's own directory plus ``<dungeon_geometry_generator>/assets``
(covers the real ``import codes as c`` / ``import patterns as z`` of the dungeon_geometry_generator
assets — both modules sit in ``assets/``), plus ``project.dir`` and the
project's ``asset_roots`` once a dungeon_geometry_generator project is loaded. PggServe itself
always appends its shipped ``resources/pgg`` on top (serve_rpc.md).
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Callable, Optional

from tools.dungeon_geometry_generator_mcp.session import DungeonGeometryGeneratorSession, PggSession


def _with_file(args: dict[str, Any], file: Optional[str]) -> dict[str, Any]:
    if file:
        args = dict(args)
        args["file"] = file
    return args


class PggLayer:
    """Owns the PggSession (created on the first pgg call) and the lib_roots context."""

    def __init__(
        self,
        dungeon_geometry_generator_session: DungeonGeometryGeneratorSession,
        pgg_factory: Optional[Callable[[], PggSession]] = None,
        mtime_fn: Callable[[str], float] = os.path.getmtime,
    ) -> None:
        self.dungeon_geometry_generator = dungeon_geometry_generator_session
        self._pgg_factory = pgg_factory or PggSession
        self._pgg: Optional[PggSession] = None
        self._mtime_fn = mtime_fn
        # project path -> (mtime, roots); re-read only when the file changed
        self._project_roots_cache: dict[str, tuple[Optional[float], list[str]]] = {}

    # --- lazy session -----------------------------------------------------

    @property
    def session(self) -> PggSession:
        """The pgg session, created on first use — dungeon-geometry-generator-only work never starts PggServe."""
        if self._pgg is None:
            self._pgg = self._pgg_factory()
        return self._pgg

    def status(self) -> dict[str, Any]:
        return self.session.status()

    # --- tool forwards ------------------------------------------------------

    def load(self, path: str, lib_roots: Optional[list[str]] = None) -> dict[str, Any]:
        return self.session.call("load", {
            "path": self._absolutize_asset(path),
            "lib_roots": self._lib_roots(path, lib_roots),
        })

    def params(self, params: dict[str, Any], file: Optional[str] = None) -> dict[str, Any]:
        return self.session.call("params", _with_file(dict(params or {}), file))

    def render(self, node: str, file: Optional[str] = None, out: Optional[str] = None,
               size: Optional[list[float]] = None, ortho: Optional[str] = None,
               target: Optional[str] = None, orbit: Optional[list[float]] = None,
               zoom: Optional[float] = None) -> dict[str, Any]:
        return self.session.call("render", _with_file({
            "node": node, "out": self._absolutize_out(out), "size": size, "ortho": ortho,
            "target": target, "orbit": orbit, "zoom": zoom,
        }, file))

    def probe(self, file: Optional[str] = None, spec: Optional[str] = None,
              specs: Optional[list[str]] = None) -> dict[str, Any]:
        args: dict[str, Any] = {}
        if spec:
            args["spec"] = spec
        if specs:
            args["specs"] = list(specs)
        return self.session.call("probe", _with_file(args, file))

    def docs(self, symbol: str, file: Optional[str] = None) -> dict[str, Any]:
        return self.session.call("docs", _with_file({"symbol": symbol}, file))

    # --- dungeon-geometry-generator-aware paths and lib_roots ------------------------------------

    def _abs_root(self, root: str) -> str:
        if os.path.isabs(root):
            return root
        return str(Path(self.dungeon_geometry_generator.repo_root) / root)

    def _absolutize_asset(self, path: str) -> str:
        """Resolve a dungeon-geometry-generator-repo-relative asset path; leave the rest to PggServe.

        PggServe's cwd is the pgg repo root, so a relative dungeon_geometry_generator path would not
        resolve there. Absolute paths and paths not found under the dungeon_geometry_generator repo
        (pgg-native files) pass through unchanged.
        """
        if os.path.isabs(path):
            return path
        cand = Path(self.dungeon_geometry_generator.repo_root) / path
        if cand.is_file():
            return str(cand)
        return path

    def _absolutize_out(self, out: Optional[str]) -> Optional[str]:
        """Resolve a relative render ``out`` against the dungeon_geometry_generator repo root.

        Unlike ``_absolutize_asset`` the target usually does not exist yet, so
        every relative path is absolutized (a bare ``out=shots/a.png`` landing
        outside the monorepo root is never what a dungeon_geometry_generator user wants). Absolute paths
        pass through; None keeps the server-side default (tmp/pgg_rpc_shots of
        the pgg repo).
        """
        if out is None or os.path.isabs(out):
            return out
        return str(Path(self.dungeon_geometry_generator.repo_root) / out)

    def _lib_roots(self, path: str, explicit: Optional[list[str]]) -> list[str]:
        if explicit is not None:
            return [self._abs_root(r) for r in explicit]
        roots: list[str] = []
        abs_asset = self._absolutize_asset(path)
        if os.path.isabs(abs_asset):
            roots.append(str(Path(abs_asset).parent))
        roots.append(str(Path(self.dungeon_geometry_generator.repo_root) / "assets"))
        roots.extend(self._project_roots())
        deduped: list[str] = []
        for r in roots:
            if r not in deduped:
                deduped.append(r)
        return deduped

    def _project_roots(self) -> list[str]:
        """``project.dir`` + ``project.dir/<asset_roots>`` of the last loaded dungeon_geometry_generator project.

        Read lazily and cached by path+mtime; a missing/broken project is
        silently skipped (asset debugging must not fail on an unrelated file).
        """
        last = getattr(self.dungeon_geometry_generator, "last_file", None)
        if not last:
            return []
        try:
            mtime: Optional[float] = self._mtime_fn(last)
        except OSError:
            mtime = None
        cached = self._project_roots_cache.get(last)
        if cached is not None and cached[0] == mtime:
            return list(cached[1])
        roots: list[str] = []
        if mtime is not None:
            try:
                data = json.loads(Path(last).read_text(encoding="utf-8"))
            except (OSError, ValueError):
                data = None
            if isinstance(data, dict):
                pdir = str(Path(last).parent)
                roots.append(pdir)
                asset_roots = data.get("asset_roots")
                if asset_roots is None:
                    asset_roots = ["assets"]  # dungeon_geometry_generator project schema default
                if isinstance(asset_roots, list):
                    for r in asset_roots:
                        roots.append(str(Path(pdir) / str(r)))
        self._project_roots_cache[last] = (mtime, roots)
        return list(roots)
