import importlib.util
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
import urllib.request
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / ".github/scripts/validate_backend_bench.py"
SPEC = importlib.util.spec_from_file_location("validate_backend_bench", SCRIPT_PATH)
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class BackendBenchArchiveTest(unittest.TestCase):
    def _write_tar(self, archive: Path, members: list[tarfile.TarInfo]) -> None:
        with tarfile.open(archive, "w:gz") as bundle:
            for member in members:
                payload = b"backend"
                if member.isfile():
                    member.size = len(payload)
                    bundle.addfile(member, io.BytesIO(payload))
                else:
                    bundle.addfile(member)

    def test_tar_member_cannot_escape_destination(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = root / "backend.tar.gz"
            destination = root / "install"
            outside = root / "outside.txt"
            self._write_tar(archive, [tarfile.TarInfo("../outside.txt")])

            with mock.patch.object(
                tarfile.TarFile,
                "extraction_filter",
                staticmethod(tarfile.fully_trusted_filter),
            ):
                with self.assertRaises(tarfile.FilterError):
                    VALIDATOR.extract_archive(archive, destination)

            self.assertFalse(outside.exists())

    def test_tar_link_cannot_escape_destination(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = root / "backend.tar.gz"
            destination = root / "install"
            link = tarfile.TarInfo("lib/backend-link")
            link.type = tarfile.SYMTYPE
            link.linkname = "../../outside.txt"
            self._write_tar(archive, [link])

            with mock.patch.object(
                tarfile.TarFile,
                "extraction_filter",
                staticmethod(tarfile.fully_trusted_filter),
            ):
                with self.assertRaises(tarfile.FilterError):
                    VALIDATOR.extract_archive(archive, destination)

            self.assertFalse((destination / "lib/backend-link").exists())

    def test_regular_tar_member_extracts(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = root / "backend.tar.gz"
            destination = root / "install"
            self._write_tar(archive, [tarfile.TarInfo("bin/backend")])

            VALIDATOR.extract_archive(archive, destination)

            self.assertEqual((destination / "bin/backend").read_bytes(), b"backend")

    def test_tar_link_within_destination_extracts(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            archive = root / "backend.tar.gz"
            destination = root / "install"
            target = tarfile.TarInfo("lib/backend.so.1")
            link = tarfile.TarInfo("lib/backend.so")
            link.type = tarfile.SYMTYPE
            link.linkname = "backend.so.1"
            self._write_tar(archive, [target, link])

            VALIDATOR.extract_archive(archive, destination)

            self.assertEqual((destination / "lib/backend.so").read_bytes(), b"backend")


class BackendBenchPathTest(unittest.TestCase):
    def test_repository_inputs_do_not_depend_on_launch_directory(self):
        versions = json.loads(VALIDATOR.BACKEND_VERSIONS_PATH.read_text())
        expected_version = ".".join(
            versions["therock"]["version"].lstrip("v").split(".")[:2]
        )
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as temp_dir:
            try:
                os.chdir(temp_dir)
                self.assertEqual(VALIDATOR.get_therock_ver(), expected_version)
                with mock.patch.object(
                    sys,
                    "argv",
                    [
                        str(SCRIPT_PATH),
                        "--download-only",
                        "--dry-run",
                        "--fork-filter",
                        "not-a-tracked-fork",
                    ],
                ):
                    self.assertEqual(VALIDATOR.main(), 0)
            finally:
                os.chdir(original_cwd)

    def test_default_binary_cache_is_user_writable(self):
        cache_env = "LOCALAPPDATA" if VALIDATOR.IS_WINDOWS else "XDG_CACHE_HOME"
        fallback = Path.home() / ("AppData/Local" if VALIDATOR.IS_WINDOWS else ".cache")
        expected_root = Path(os.environ.get(cache_env) or fallback)
        expected = expected_root / "lemonade/bench-binaries"

        with mock.patch.object(
            sys,
            "argv",
            [
                str(SCRIPT_PATH),
                "--download-only",
                "--dry-run",
                "--fork-filter",
                "llamacpp-rocm-stable",
            ],
        ), mock.patch.object(
            VALIDATOR, "resolve_latest_version", return_value="v1"
        ), mock.patch.object(
            VALIDATOR, "install_fork_binary"
        ) as install:
            self.assertEqual(VALIDATOR.main(), 0)

        self.assertEqual(install.call_args.args[2], expected)

        with mock.patch.dict(os.environ, {cache_env: ""}):
            self.assertEqual(
                VALIDATOR._default_binaries_dir(),
                fallback / "lemonade/bench-binaries",
            )


class BackendBenchDownloadTest(unittest.TestCase):
    def test_initial_http_url_does_not_receive_authorization(self):
        captured_requests = []

        class Response(io.BytesIO):
            def __enter__(self):
                return self

            def __exit__(self, *_args):
                self.close()

        class Opener:
            def open(self, request, timeout):
                captured_requests.append((request, timeout))
                return Response(b"backend")

        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            urllib.request, "build_opener", return_value=Opener()
        ):
            VALIDATOR.download_file(
                "http://github.com/org/repo/releases/download/v1/backend.tar.gz",
                Path(temp_dir) / "backend.tar.gz",
                token="secret",
            )

        self.assertEqual(len(captured_requests), 1)
        self.assertIsNone(captured_requests[0][0].get_header("Authorization"))

    def test_cross_host_redirect_drops_authorization(self):
        request = urllib.request.Request(
            "https://github.com/org/repo/releases/download/v1/backend.tar.gz",
            headers={"Authorization": "Bearer secret"},
        )

        redirected = (
            VALIDATOR.StripAuthorizationOnCrossOriginRedirect().redirect_request(
                request,
                None,
                302,
                "Found",
                {},
                "https://objects.githubusercontent.com/backend.tar.gz",
            )
        )

        self.assertIsNotNone(redirected)
        self.assertIsNone(redirected.get_header("Authorization"))

    def test_https_downgrade_redirect_drops_authorization(self):
        request = urllib.request.Request(
            "https://github.com/org/repo/releases/download/v1/backend.tar.gz",
            headers={"Authorization": "Bearer secret"},
        )

        redirected = (
            VALIDATOR.StripAuthorizationOnCrossOriginRedirect().redirect_request(
                request,
                None,
                302,
                "Found",
                {},
                "http://github.com/org/repo/releases/download/v1/backend-2.tar.gz",
            )
        )

        self.assertIsNotNone(redirected)
        self.assertIsNone(redirected.get_header("Authorization"))

    def test_alternate_port_redirect_drops_authorization(self):
        request = urllib.request.Request(
            "https://github.com/org/repo/releases/download/v1/backend.tar.gz",
            headers={"Authorization": "Bearer secret"},
        )

        redirected = VALIDATOR.StripAuthorizationOnCrossOriginRedirect().redirect_request(
            request,
            None,
            302,
            "Found",
            {},
            "https://github.com:8443/org/repo/releases/download/v1/backend-2.tar.gz",
        )

        self.assertIsNotNone(redirected)
        self.assertIsNone(redirected.get_header("Authorization"))

    def test_same_https_origin_redirect_keeps_authorization(self):
        request = urllib.request.Request(
            "https://github.com/org/repo/releases/download/v1/backend.tar.gz",
            headers={"Authorization": "Bearer secret"},
        )

        redirected = (
            VALIDATOR.StripAuthorizationOnCrossOriginRedirect().redirect_request(
                request,
                None,
                302,
                "Found",
                {},
                "https://github.com:443/org/repo/releases/download/v1/backend-2.tar.gz",
            )
        )

        self.assertIsNotNone(redirected)
        self.assertEqual(redirected.get_header("Authorization"), "Bearer secret")


class BackendBenchResultTest(unittest.TestCase):
    def test_previous_result_is_the_newest_existing_run(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results = Path(temp_dir)
            model_dir = results / "fork" / "model"
            model_dir.mkdir(parents=True)
            older = model_dir / "run-older.json"
            newest = model_dir / "run-newest.json"
            ignored = model_dir / "run-regression.json"
            older.write_text("{}")
            newest.write_text("{}")
            ignored.write_text("{}")
            os.utime(older, (1, 1))
            os.utime(newest, (2, 2))
            os.utime(ignored, (3, 3))

            self.assertEqual(
                VALIDATOR.find_previous_result(results, "fork", "model"), newest
            )


if __name__ == "__main__":
    unittest.main()
