"""Unit tests for tools.pgg_mcp.session (no PggViewer, stdlib only).

Run from the repo root:

    python3 -m unittest tools.pgg_mcp.test_session
"""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.pgg_mcp.session import (
    PggSession,
    detect_platform,
    find_viewer_binary,
    need_build_error,
    product_lib_root,
    viewer_recipe,
    with_product_lib_roots,
)


def _touch(root: str, rel: str) -> str:
    path = Path(root) / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")
    return str(path)


class PlatformTests(unittest.TestCase):
    def test_detect_linux_darwin_win(self) -> None:
        self.assertEqual(detect_platform("linux"), "linux")
        self.assertEqual(detect_platform("linux2"), "linux")
        self.assertEqual(detect_platform("darwin"), "macos")
        self.assertEqual(detect_platform("win32"), "windows")
        self.assertEqual(detect_platform("cygwin"), "windows")

    def test_linux_recipe_prefers_release(self) -> None:
        r = viewer_recipe("linux")
        self.assertEqual(r.platform, "linux")
        self.assertTrue(r.candidates[0].startswith("_int_linux_release"))
        self.assertEqual(
            list(r.build),
            ["cmake", "--build", "--preset", "linux-release", "--target", "PggViewer"],
        )
        self.assertEqual(list(r.configure), ["./build_linux.sh"])
        self.assertIn("linux-debug", r.debug_build)

    def test_macos_and_windows_recipes(self) -> None:
        mac = viewer_recipe("macos")
        self.assertEqual(list(mac.configure), ["./build_mac.sh"])
        self.assertIn("macos-release", mac.build)
        win = viewer_recipe("windows")
        self.assertEqual(list(win.configure), ["generate_vs.bat"])
        self.assertTrue(win.expected.endswith(".exe"))
        self.assertIn("release", win.build)


class FindBinaryTests(unittest.TestCase):
    def test_prefers_release_over_debug(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux/src/apps/PggViewer/Debug/PggViewer")
            rel = _touch(root, "_int_linux_release/src/apps/PggViewer/Release/PggViewer")
            found = find_viewer_binary(root, platform="linux", environ={})
            self.assertEqual(found, rel)

    def test_falls_back_to_debug(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            rel = _touch(root, "_int_linux/src/apps/PggViewer/Debug/PggViewer")
            found = find_viewer_binary(root, platform="linux", environ={})
            self.assertEqual(found, rel)

    def test_pgg_viewer_env_wins(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux_release/src/apps/PggViewer/Release/PggViewer")
            custom = _touch(root, "custom/PggViewer")
            found = find_viewer_binary(
                root, platform="linux", environ={"PGG_VIEWER": custom}
            )
            self.assertEqual(found, custom)

    def test_stale_env_falls_through_to_preset(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            preset = _touch(root, "_int_linux/src/apps/PggViewer/Debug/PggViewer")
            found = find_viewer_binary(
                root,
                platform="linux",
                environ={"PGG_VIEWER": os.path.join(root, "nope", "PggViewer")},
            )
            self.assertEqual(found, preset)

    def test_windows_exe_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            rel = _touch(root, "_intermediate_64/src/apps/PggViewer/Release/PggViewer.exe")
            found = find_viewer_binary(root, platform="windows", environ={})
            self.assertEqual(found, rel)

    def test_missing_returns_none(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            self.assertIsNone(find_viewer_binary(root, platform="linux", environ={}))


class NeedBuildTests(unittest.TestCase):
    def test_envelope_shape(self) -> None:
        err = need_build_error("/repo", platform="linux", environ={})
        self.assertFalse(err["ok"])
        e = err["error"]
        self.assertEqual(e["kind"], "need_build")
        self.assertEqual(e["target"], "PggViewer")
        self.assertEqual(e["platform"], "linux")
        self.assertEqual(e["cwd"], "/repo")
        self.assertEqual(e["viewer"], "missing")
        self.assertEqual(e["configure"], ["./build_linux.sh"])
        self.assertEqual(e["build"][0], "cmake")
        self.assertIn("candidates", e)
        self.assertIn("hint", e)

    def test_mentions_stale_pgg_viewer(self) -> None:
        err = need_build_error(
            "/repo",
            platform="linux",
            environ={"PGG_VIEWER": "/nope/PggViewer"},
        )
        self.assertIn("PGG_VIEWER", err["error"]["message"])
        self.assertIn("/nope/PggViewer", err["error"]["message"])


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
            self.assertEqual(err["error"]["cwd"], root)

    def test_linux_headless_without_xvfb(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            _touch(root, "_int_linux/src/apps/PggViewer/Debug/PggViewer")
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
            binary = _touch(root, "_int_linux/src/apps/PggViewer/Debug/PggViewer")
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
            self.assertEqual(cmds, [[binary, "--serve"]])
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
        session.binary_path = "/repo/PggViewer"
        resp = session.status()
        self.assertTrue(resp["ok"])
        self.assertEqual(resp["data"]["viewer"], "running")
        self.assertEqual(resp["data"]["binary"], "/repo/PggViewer")
        self.assertEqual(resp["data"]["rpc"]["port"], 9878)


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
