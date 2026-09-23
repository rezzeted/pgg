"""Unit tests for tools.pgg_mcp.session (no PggServe process, stdlib only).

Run from the repo root:

    python3 -m unittest tools.pgg_mcp.test_session
"""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.pgg_mcp.launch import _repo_root
from tools.pgg_mcp.session import (
    PggSession,
    default_repo_root,
    detect_platform,
    find_serve_binary,
    find_viewer_binary,
    need_build_error,
    product_lib_root,
    serve_recipe,
    viewer_recipe,
    with_product_lib_roots,
)


def _touch(root: str, rel: str) -> str:
    path = Path(root) / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")
    return str(path)


class RepoRootTests(unittest.TestCase):
    def test_tilde_repo_root_expands_before_cwd_join(self) -> None:
        home = Path.home()
        raw = "~/sources/pgg"
        expected = str((home / "sources" / "pgg").resolve())
        self.assertEqual(default_repo_root({"PGG_REPO_ROOT": raw}), expected)
        with mock.patch.dict(os.environ, {"PGG_REPO_ROOT": raw}, clear=False):
            self.assertEqual(str(_repo_root()), expected)

    def test_absolute_repo_root_unchanged(self) -> None:
        root = str(Path("/tmp/pgg-root").resolve())
        self.assertEqual(default_repo_root({"PGG_REPO_ROOT": root}), root)

    def test_missing_repo_root_falls_back_to_checkout(self) -> None:
        fallback = Path(__file__).resolve().parent.parent.parent
        with mock.patch.dict(
            os.environ, {"PGG_REPO_ROOT": "~/no-such-pgg-checkout"}, clear=False
        ):
            self.assertEqual(_repo_root(), fallback)


class PlatformTests(unittest.TestCase):
    def test_detect_linux_darwin_win(self) -> None:
        self.assertEqual(detect_platform("linux"), "linux")
        self.assertEqual(detect_platform("linux2"), "linux")
        self.assertEqual(detect_platform("darwin"), "macos")
        self.assertEqual(detect_platform("win32"), "windows")
        self.assertEqual(detect_platform("cygwin"), "windows")

    def test_linux_recipe_prefers_release(self) -> None:
        r = serve_recipe("linux")
        self.assertEqual(r.platform, "linux")
        self.assertTrue(r.candidates[0].startswith("_int_linux_release"))
        self.assertTrue("PggServe" in r.candidates[0])
        self.assertEqual(
            list(r.build),
            ["cmake", "--build", "--preset", "linux-release", "--target", "PggServe"],
        )
        self.assertEqual(list(r.configure), ["./build_linux.sh"])
        self.assertIn("linux-debug", r.debug_build)
        self.assertIs(viewer_recipe, serve_recipe)

    def test_macos_and_windows_recipes(self) -> None:
        mac = serve_recipe("macos")
        self.assertEqual(list(mac.configure), ["./build_mac.sh"])
        self.assertIn("macos-release", mac.build)
        self.assertIn("PggServe", mac.build)
        win = serve_recipe("windows")
        self.assertEqual(list(win.configure), ["generate_vs.bat"])
        self.assertTrue(win.expected.endswith(".exe"))
        self.assertIn("release", win.build)
        self.assertIn("PggServe", win.build)


class FindBinaryTests(unittest.TestCase):
    def test_prefers_release_over_debug(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            rel = _touch(root, "_int_linux_release/src/apps/PggServe/Release/PggServe")
            found = find_serve_binary(root, platform="linux", environ={})
            self.assertEqual(found, rel)

    def test_falls_back_to_debug(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            rel = _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            found = find_serve_binary(root, platform="linux", environ={})
            self.assertEqual(found, rel)

    def test_pgg_serve_env_wins(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux_release/src/apps/PggServe/Release/PggServe")
            custom = _touch(root, "custom/PggServe")
            found = find_serve_binary(
                root, platform="linux", environ={"PGG_SERVE": custom}
            )
            self.assertEqual(found, custom)

    def test_pgg_viewer_env_still_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            custom = _touch(root, "custom/PggServe")
            found = find_serve_binary(
                root, platform="linux", environ={"PGG_VIEWER": custom}
            )
            self.assertEqual(found, custom)

    def test_stale_env_falls_through_to_preset(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            preset = _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            found = find_serve_binary(
                root,
                platform="linux",
                environ={"PGG_SERVE": os.path.join(root, "nope", "PggServe")},
            )
            self.assertEqual(found, preset)

    def test_windows_exe_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            rel = _touch(root, "_intermediate_64/src/apps/PggServe/Release/PggServe.exe")
            found = find_serve_binary(root, platform="windows", environ={})
            self.assertEqual(found, rel)

    def test_missing_returns_none(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.assertIsNone(find_serve_binary(root, platform="linux", environ={}))

    def test_alias_matches_find_serve(self) -> None:
        self.assertIs(find_viewer_binary, find_serve_binary)


class NeedBuildTests(unittest.TestCase):
    def test_envelope_shape(self) -> None:
        err = need_build_error("/repo", platform="linux", environ={})
        self.assertFalse(err["ok"])
        e = err["error"]
        self.assertEqual(e["kind"], "need_build")
        self.assertEqual(e["target"], "PggServe")
        self.assertEqual(e["platform"], "linux")
        self.assertEqual(e["cwd"], "/repo")
        self.assertEqual(e["viewer"], "missing")
        self.assertEqual(e["serve"], "missing")
        self.assertEqual(e["configure"], ["./build_linux.sh"])
        self.assertEqual(e["build"][0], "cmake")
        self.assertIn("PggServe", e["build"])
        self.assertIn("candidates", e)
        self.assertIn("hint", e)

    def test_mentions_stale_pgg_serve(self) -> None:
        err = need_build_error(
            "/repo",
            platform="linux",
            environ={"PGG_SERVE": "/nope/PggServe"},
        )
        self.assertIn("PGG_SERVE", err["error"]["message"])
        self.assertIn("/nope/PggServe", err["error"]["message"])

    def test_mentions_stale_pgg_viewer_fallback(self) -> None:
        err = need_build_error(
            "/repo",
            platform="linux",
            environ={"PGG_VIEWER": "/nope/PggViewer"},
        )
        self.assertIn("PGG_VIEWER", err["error"]["message"])


class FakeProc:
    def __init__(self, returncode: int | None = None) -> None:
        self._returncode = returncode

    def poll(self) -> int | None:
        return self._returncode


class SessionEnsureTests(unittest.TestCase):
    def test_port_open_does_not_spawn(self) -> None:
        spawned: list[object] = []

        def popen(*_a: object, **_k: object) -> FakeProc:
            spawned.append(1)
            return FakeProc()

        session = PggSession(
            repo_root="/repo",
            platform="linux",
            environ={},
            port_open_fn=lambda *_a, **_k: True,
            popen_fn=popen,
        )
        self.assertIsNone(session.ensure())
        self.assertEqual(spawned, [])

    def test_missing_binary_is_need_build(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            session = PggSession(
                repo_root=root,
                platform="linux",
                environ={},
                port_open_fn=lambda *_a, **_k: False,
                popen_fn=lambda *_a, **_k: FakeProc(),
            )
            err = session.ensure()
            assert err is not None
            self.assertEqual(err["error"]["kind"], "need_build")
            self.assertEqual(err["error"]["target"], "PggServe")
            self.assertEqual(err["error"]["cwd"], root)

    def test_linux_headless_without_xvfb(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            spawned: list[object] = []
            session = PggSession(
                repo_root=root,
                platform="linux",
                environ={},
                port_open_fn=lambda *_a, **_k: False,
                popen_fn=lambda *_a, **_k: spawned.append(1) or FakeProc(),
                which_fn=lambda _name: None,
            )
            err = session.ensure()
            assert err is not None
            self.assertEqual(err["error"]["kind"], "unreachable")
            self.assertIn("xvfb-run", err["error"]["message"])
            self.assertEqual(spawned, [])

    def test_spawns_serve_when_binary_exists(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            binary = _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            cmds: list[list[str]] = []

            def popen(cmd: list[str], **_k: object) -> FakeProc:
                cmds.append(list(cmd))
                return FakeProc(returncode=None)

            session = PggSession(
                repo_root=root,
                platform="linux",
                environ={"DISPLAY": ":0"},
                port_open_fn=lambda *_a, **_k: False,
                wait_for_port_fn=lambda *_a, **_k: True,
                popen_fn=popen,
            )
            self.addCleanup(lambda: session._log_file.close() if session._log_file else None)
            self.assertIsNone(session.ensure())
            self.assertEqual(cmds, [[binary, "--port=9878", "--host=127.0.0.1"]])
            self.assertEqual(session.binary_path, binary)

    def test_call_returns_need_build_without_client(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            session = PggSession(
                repo_root=root,
                platform="linux",
                environ={},
                port_open_fn=lambda *_a, **_k: False,
            )
            resp = session.call("status")
            self.assertFalse(resp["ok"])
            self.assertEqual(resp["error"]["kind"], "need_build")

    def test_status_enriches_running(self) -> None:
        class FakeClient:
            def call(self, **_kw: object) -> dict:
                return {"ok": True, "data": {"file": "x.pgg", "uptime_s": 1}}

            def close(self) -> None:
                pass

        session = PggSession(
            repo_root="/repo",
            platform="linux",
            environ={},
            port_open_fn=lambda *_a, **_k: True,
            client_factory=FakeClient,
        )
        session.binary_path = "/repo/PggServe"
        resp = session.status()
        self.assertTrue(resp["ok"])
        self.assertEqual(resp["data"]["serve"], "running")
        self.assertEqual(resp["data"]["viewer"], "running")
        self.assertEqual(resp["data"]["binary"], "/repo/PggServe")
        self.assertEqual(resp["data"]["rpc"]["port"], 9878)

    def test_one_tcp_client_per_call(self) -> None:
        created: list[int] = []
        closed: list[int] = []

        class FakeClient:
            def __init__(self) -> None:
                created.append(1)

            def call(self, **_kw: object) -> dict:
                return {"ok": True, "data": {"pong": True}}

            def close(self) -> None:
                closed.append(1)

        session = PggSession(
            repo_root="/repo",
            platform="linux",
            environ={},
            port_open_fn=lambda *_a, **_k: True,
            client_factory=FakeClient,
        )
        session.call("ping")
        session.call("ping")
        self.assertEqual(created, [1, 1])
        self.assertEqual(closed, [1, 1])

    def test_injects_last_file_on_slot_ops(self) -> None:
        calls: list[dict] = []

        class FakeClient:
            def call(self, **kw: object) -> dict:
                calls.append(dict(kw))
                if kw.get("op") == "load":
                    return {"ok": True, "data": {"session": {"file": "/repo/a.pgg"}, "path": "a.pgg"}}
                return {"ok": True, "data": {}}

            def close(self) -> None:
                pass

        session = PggSession(
            repo_root="/repo",
            platform="linux",
            environ={},
            port_open_fn=lambda *_a, **_k: True,
            client_factory=FakeClient,
        )
        session.call("load", {"path": "a.pgg"})
        session.call("render", {"node": "house"})
        session.call("render", {"node": "house", "file": "/other.pgg"})
        self.assertEqual(session.last_file, "/repo/a.pgg")
        self.assertEqual(calls[1]["args"]["file"], "/repo/a.pgg")
        self.assertEqual(calls[2]["args"]["file"], "/other.pgg")


class StaleBinaryTests(unittest.TestCase):
    class LiveProc:
        def __init__(self) -> None:
            self.alive = True

        def poll(self) -> int | None:
            return None if self.alive else 0

        def terminate(self) -> None:
            self.alive = False

        def wait(self, timeout: float | None = None) -> int:
            return 0

    def _session(self, root: str, clock: list[float], mtimes: dict[str, float], calls: list[dict],
                 procs: list[object], port: list[bool]) -> PggSession:
        class FakeClient:
            def call(self, **kw: object) -> dict:
                calls.append(dict(kw))
                if kw.get("op") == "load":
                    path = kw["args"]["path"]  # type: ignore[index]
                    return {"ok": True, "data": {"session": {"file": "/abs/" + str(path)}, "path": path}}
                if kw.get("op") == "status":
                    return {"ok": True, "data": {"uptime_s": 100.0}}
                return {"ok": True, "data": {}}

            def close(self) -> None:
                pass

        def popen(*_a: object, **_k: object) -> object:
            proc = StaleBinaryTests.LiveProc()
            procs.append(proc)
            port[0] = True
            return proc

        def port_open(*_a: object, **_k: object) -> bool:
            if procs and not getattr(procs[-1], "alive", True):
                port[0] = False
            return port[0]

        session = PggSession(
            repo_root=root,
            platform="linux",
            environ={"DISPLAY": ":0"},
            port_open_fn=port_open,
            wait_for_port_fn=lambda *_a, **_k: True,
            popen_fn=popen,
            client_factory=FakeClient,
            time_fn=lambda: clock[0],
            mtime_fn=lambda p: mtimes[p],
        )
        self.addCleanup(lambda: session._log_file.close() if session._log_file else None)
        return session

    def test_own_process_restarts_on_rebuilt_binary_and_reloads_slots(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            binary = _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            clock, mtimes = [1000.0], {binary: 900.0}
            calls: list[dict] = []
            procs: list[object] = []
            session = self._session(root, clock, mtimes, calls, procs, [False])
            session.call("load", {"path": "a.pgg", "lib_roots": ["/lib"]})
            session.call("load", {"path": "b.pgg"})
            self.assertEqual(len(procs), 1)
            self.assertNotIn("restarted", session.call("ping"))

            mtimes[binary] = 1001.0
            clock[0] = 1001.5  # still linking: no restart yet
            self.assertNotIn("restarted", session.call("ping"))
            self.assertEqual(len(procs), 1)

            clock[0] = 1010.0
            calls.clear()
            resp = session.call("render", {"node": "house"})
            self.assertEqual(len(procs), 2)
            self.assertTrue(resp["restarted"])
            self.assertEqual(resp["restart"]["reloaded_slots"], ["/abs/a.pgg", "/abs/b.pgg"])
            self.assertEqual([c["op"] for c in calls], ["load", "load", "render"])
            self.assertEqual(calls[0]["args"], {"path": "a.pgg", "lib_roots": ["/lib"]})
            self.assertEqual(calls[2]["args"]["file"], "/abs/b.pgg")
            self.assertNotIn("restarted", session.call("ping"))

    def test_foreign_process_gets_stale_binary_warning(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            binary = _touch(root, "_int_linux/src/apps/PggServe/Debug/PggServe")
            clock, mtimes = [1000.0], {binary: 950.0}  # server started at 900
            calls: list[dict] = []
            procs: list[object] = []
            session = self._session(root, clock, mtimes, calls, procs, [True])
            resp = session.call("ping")
            self.assertEqual(procs, [])
            self.assertIn("stale_binary", resp)
            self.assertEqual(resp["stale_binary"]["binary"], binary)

            mtimes[binary] = 850.0
            self.assertNotIn("stale_binary", session.call("ping"))


class ProductLibRootsTests(unittest.TestCase):
    def test_appends_shipped_lib(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            (Path(root) / "resources" / "pgg" / "lib").mkdir(parents=True)
            self.assertEqual(
                with_product_lib_roots(root, None),
                [os.path.join(root, "resources", "pgg")],
            )
            extra = ["/custom"]
            self.assertEqual(
                with_product_lib_roots(root, extra),
                ["/custom", os.path.join(root, "resources", "pgg")],
            )
            self.assertIsNotNone(product_lib_root(root))

    def test_skips_when_lib_missing(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.assertEqual(with_product_lib_roots(root, ["/custom"]), ["/custom"])
            self.assertIsNone(product_lib_root(root))


class LaunchModuleTests(unittest.TestCase):
    def test_venv_python_path(self) -> None:
        from tools.pgg_mcp.launch import _venv_python

        venv = Path("/tmp/fake-venv")
        with mock.patch("tools.pgg_mcp.launch.os.name", "posix"):
            self.assertEqual(_venv_python(venv), venv / "bin" / "python")
        with mock.patch("tools.pgg_mcp.launch.os.name", "nt"):
            self.assertEqual(_venv_python(venv), venv / "Scripts" / "python.exe")


if __name__ == "__main__":
    unittest.main()
