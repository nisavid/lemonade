"""
Process integration tests for per-model llama.cpp executable bindings.

A bound model must start its bound llama-server, whatever the shared llama.cpp
install looks like. These tests bind models to a mock llama-server through a
fragment directory and config.json, with the shared backends left uninstalled,
and check the process lemond actually launched.

Resolver rules and router ordering are covered in C++ by
test/cpp/test_llamacpp_executable_binding.cpp.

Usage:
    python test/test_llamacpp_executable_bindings.py
    python test/test_llamacpp_executable_bindings.py --cli-binary /path/to/lemonade
"""

import json
import os
import shutil
import stat
import sys
import tempfile
import time
import unittest

import requests

import test_llamacpp_system_backend as system_backend
from utils.server_base import _auth_headers, pull_model_with_retry
from utils.test_models import (
    ENDPOINT_TEST_MODEL,
    MULTI_MODEL_TERTIARY,
    TIMEOUT_DEFAULT,
    TIMEOUT_MODEL_OPERATION,
)

BOUND_BACKEND = "cpu"
FRAGMENT_MODEL = "user.Bound-Tiny"
CONFIG_MODEL = "user.Config-Bound"
UNBOUND_MODEL = "user.Null-Unbound"
BAD_PATH_MODEL = "user.Bad-Path"
MALFORMED_MODEL = "user.Malformed"
# Bound by its public name, which is not its cache key.
PUBLIC_KEY_MODEL = "user.Public-Key"
# First registered while no shared llama.cpp is runnable.
PULL_BOUND_MODEL = "user.Pull-Bound"
PULL_UNBOUND_MODEL = "user.Pull-Unbound"
# Bound in config.json and left unregistered by setUpClass: each is the target
# of one definition-write path.
REGISTER_TARGET_MODEL = "user.Register-Target"
PULL_TARGET_MODEL = "user.Pull-Target"
IMPORT_TARGET_MODEL = "user.Import-Target"
COMPONENT_TARGET_MODEL = "user.Component-Target"
DEFINITION_TARGET_MODELS = (
    REGISTER_TARGET_MODEL,
    PULL_TARGET_MODEL,
    IMPORT_TARGET_MODEL,
    COMPONENT_TARGET_MODEL,
)
# Unbound itself; it carries COMPONENT_TARGET_MODEL as an inline component.
COMPONENT_COLLECTION = "user.Component-Target-Kit"
CONFLICTING_BACKEND = "vulkan"
REGISTERED_MODELS = (
    FRAGMENT_MODEL,
    CONFIG_MODEL,
    UNBOUND_MODEL,
    BAD_PATH_MODEL,
    MALFORMED_MODEL,
    PUBLIC_KEY_MODEL,
)
# Shadow pairs, registered in test_009: a bound user model over an unbound
# built-in, and an unbound user model over a bound built-in. The second
# built-in is never downloaded; only its options are read.
SHADOWING_BOUND_MODEL = f"user.{ENDPOINT_TEST_MODEL}"
SHADOWED_BOUND_BUILTIN = MULTI_MODEL_TERTIARY
SHADOWING_UNBOUND_MODEL = f"user.{SHADOWED_BOUND_BUILTIN}"
BOUND_BACKEND_ARGS = "--threads 3"


def _url(path):
    return f"http://localhost:{system_backend.PORT}{path}"


def _bare(model_name):
    return model_name.split(".", 1)[1] if model_name.startswith("user.") else model_name


def _write_executable(path, contents):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(contents)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


def _loaded_entry(model_name):
    response = requests.get(
        _url("/api/v1/health"), headers=_auth_headers(), timeout=TIMEOUT_DEFAULT
    )
    response.raise_for_status()
    for entry in response.json().get("all_models_loaded", []):
        if entry.get("model_name") in (model_name, _bare(model_name)):
            return entry
    return None


def _load(model_name, **options):
    return requests.post(
        _url("/api/v1/load"),
        json={"model_name": model_name, **options},
        headers=_auth_headers(),
        timeout=TIMEOUT_MODEL_OPERATION,
    )


def _register(model_name, checkpoint, **definition):
    return requests.post(
        _url("/api/v1/models/register"),
        json={
            "model_name": model_name,
            "recipe": "llamacpp",
            "checkpoint": checkpoint,
            **definition,
        },
        headers=_auth_headers(),
        timeout=TIMEOUT_DEFAULT,
    )


def _pull_request(body):
    return requests.post(
        _url("/api/v1/pull"),
        json={**body, "do_not_upgrade": True},
        headers=_auth_headers(),
        timeout=TIMEOUT_MODEL_OPERATION,
    )


def _pull(model_name, checkpoint, **definition):
    return _pull_request(
        {
            "model_name": model_name,
            "recipe": "llamacpp",
            "checkpoint": checkpoint,
            **definition,
        }
    )


def _model_status(model_name):
    return requests.get(
        _url(f"/api/v1/models/{model_name}"),
        headers=_auth_headers(),
        timeout=TIMEOUT_DEFAULT,
    ).status_code


def _effective_options(model_name):
    response = requests.get(
        _url(f"/api/v1/models/{model_name}/options"),
        headers=_auth_headers(),
        timeout=TIMEOUT_DEFAULT,
    )
    response.raise_for_status()
    return response.json()["effective"]


def _runnable_shared_backends():
    response = requests.get(
        _url("/api/v1/system-info"),
        headers=_auth_headers(),
        timeout=TIMEOUT_DEFAULT,
    )
    response.raise_for_status()
    backends = response.json()["recipes"]["llamacpp"]["backends"]
    return {
        name: backend.get("state")
        for name, backend in backends.items()
        if backend.get("state") != "unsupported"
    }


def _server_log():
    # Where test_llamacpp_system_backend._start_server sends lemond's output.
    path = os.path.join(tempfile.gettempdir(), "lemond_test.log")
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


@unittest.skipUnless(
    sys.platform.startswith("linux"), "The mock llama-server runs on Linux only"
)
class LlamaCppExecutableBindingTests(unittest.TestCase):
    """Bound models run their bound executable through lemond's real load paths."""

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        cls._active = False
        cls.temp_dir = tempfile.mkdtemp(prefix="lemonade_llamacpp_binding_")
        cls.cache_dir = os.path.join(cls.temp_dir, "cache")
        cls.path_dir = os.path.join(cls.temp_dir, "path")
        cls.bound_dir = os.path.join(cls.temp_dir, "bound")
        cls.fragments_dir = os.path.join(cls.temp_dir, "llamacpp-bindings.d")
        for directory in (
            cls.cache_dir,
            cls.path_dir,
            cls.bound_dir,
            cls.fragments_dir,
        ):
            os.makedirs(directory)
        cls.shared_executable = os.path.join(cls.path_dir, "llama-server")
        cls.bound_executable = os.path.join(cls.bound_dir, "llama-server-bound")
        cls.missing_executable = os.path.join(cls.bound_dir, "missing-llama-server")
        cls.original_env = {
            name: os.environ.get(name)
            for name in ("PATH", "LEMONADE_LLAMACPP_BINDINGS_DIR")
        }

        try:
            _write_executable(
                cls.shared_executable, system_backend.MOCK_LLAMA_SERVER_PYTHON
            )
            _write_executable(
                cls.bound_executable, system_backend.MOCK_LLAMA_SERVER_PYTHON
            )
            # The system backend accepts a HIP plugin beside a PATH
            # llama-server, which keeps unbound loads deterministic on AMD hosts.
            with open(
                os.path.join(cls.path_dir, "libggml-hip.so"), "w", encoding="utf-8"
            ) as handle:
                handle.write("stub")

            cls._write_fragment(
                FRAGMENT_MODEL,
                {"executable": cls.bound_executable, "backend": BOUND_BACKEND},
            )
            cls._write_fragment(
                UNBOUND_MODEL,
                {"executable": cls.bound_executable, "backend": BOUND_BACKEND},
            )
            with open(
                os.path.join(cls.fragments_dir, f"{MALFORMED_MODEL}.json"),
                "w",
                encoding="utf-8",
            ) as handle:
                handle.write("{ not json")

            config = {
                "log_level": "debug",
                "max_loaded_models": 4,
                "no_fetch_executables": True,
                "pinned_models": [BAD_PATH_MODEL],
                "llamacpp": {
                    "backend": "system",
                    f"{BOUND_BACKEND}_args": BOUND_BACKEND_ARGS,
                    "model_executables": {
                        CONFIG_MODEL: {
                            "executable": cls.bound_executable,
                            "backend": BOUND_BACKEND,
                        },
                        UNBOUND_MODEL: None,
                        BAD_PATH_MODEL: {
                            "executable": cls.missing_executable,
                            "backend": BOUND_BACKEND,
                        },
                        _bare(PUBLIC_KEY_MODEL): {
                            "executable": cls.bound_executable,
                            "backend": BOUND_BACKEND,
                        },
                        SHADOWING_BOUND_MODEL: {
                            "executable": cls.bound_executable,
                            "backend": BOUND_BACKEND,
                        },
                        SHADOWED_BOUND_BUILTIN: {
                            "executable": cls.bound_executable,
                            "backend": BOUND_BACKEND,
                        },
                        **{
                            model_name: {
                                "executable": cls.bound_executable,
                                "backend": BOUND_BACKEND,
                            }
                            for model_name in DEFINITION_TARGET_MODELS
                        },
                    },
                },
            }
            with open(
                os.path.join(cls.cache_dir, "config.json"), "w", encoding="utf-8"
            ) as handle:
                json.dump(config, handle)

            original_path = cls.original_env["PATH"] or ""
            os.environ["PATH"] = cls.path_dir + os.pathsep + original_path
            os.environ["LEMONADE_LLAMACPP_BINDINGS_DIR"] = cls.fragments_dir
            system_backend.LlamaCppSystemBackendTests.cache_dir = cls.cache_dir

            system_backend._start_server()
            cls._active = True
            pull_model_with_retry(ENDPOINT_TEST_MODEL, port=system_backend.PORT)
            checkpoint = cls._checkpoint_of(ENDPOINT_TEST_MODEL)
            for model_name in REGISTERED_MODELS:
                response = _register(model_name, checkpoint)
                if response.status_code != 200:
                    raise RuntimeError(
                        f"Failed to register {model_name}: "
                        f"{response.status_code} {response.text}"
                    )

            # Restart so the pinned restore runs against the registered models.
            system_backend._stop_server()
            cls._active = False
            system_backend._start_server()
            cls._active = True
            cls._wait_for_pin_error(BAD_PATH_MODEL)
        except Exception:
            if cls._active:
                system_backend._stop_server()
            cls._restore_environment()
            shutil.rmtree(cls.temp_dir, ignore_errors=True)
            raise

    @classmethod
    def tearDownClass(cls):
        if cls._active:
            system_backend._stop_server()
        cls._restore_environment()
        shutil.rmtree(cls.temp_dir, ignore_errors=True)
        super().tearDownClass()

    @classmethod
    def _restore_environment(cls):
        for name, value in cls.original_env.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value

    @classmethod
    def _write_fragment(cls, model_name, entry):
        path = os.path.join(cls.fragments_dir, f"{model_name}.json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump({"llamacpp": {"model_executables": {model_name: entry}}}, handle)

    @classmethod
    def _checkpoint_of(cls, model_name):
        response = requests.get(
            _url(f"/api/v1/models/{model_name}"),
            headers=_auth_headers(),
            timeout=TIMEOUT_DEFAULT,
        )
        response.raise_for_status()
        return response.json()["checkpoint"]

    @classmethod
    def _pin_error(cls, model_name):
        response = requests.get(
            _url("/api/v1/pins"), headers=_auth_headers(), timeout=TIMEOUT_DEFAULT
        )
        response.raise_for_status()
        for entry in response.json().get("data", []):
            if entry.get("model_name") in (model_name, _bare(model_name)):
                return entry.get("load_error")
        return None

    @classmethod
    def _wait_for_pin_error(cls, model_name, timeout=60):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if cls._pin_error(model_name):
                return
            time.sleep(1)
        raise RuntimeError(f"Pinned restore of {model_name} never reported an error")

    def setUp(self):
        print(f"\n=== Starting test: {self._testMethodName} ===")

    def _assert_bound_launch(self, model_name):
        entry = _loaded_entry(model_name)
        self.assertIsNotNone(entry, f"{model_name} is not loaded")
        self.assertEqual(entry["launch_command"][0], self.bound_executable)
        self.assertEqual(
            entry["recipe_options"]["llamacpp_backend"], BOUND_BACKEND, entry
        )
        return entry

    def _assert_conflict_saved_nothing(self, response, model_name):
        self.assertEqual(response.status_code, 400, f"{model_name}: {response.text}")
        self.assertIn("conflicts", response.text)
        self.assertIn(model_name, response.text)
        self.assertEqual(_model_status(model_name), 404, model_name)

    def test_001_fragment_binding_runs_bound_executable(self):
        """A fragment-bound model loads with the shared backend uninstalled."""
        response = _load(FRAGMENT_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(FRAGMENT_MODEL)

    def test_002_config_binding_runs_bound_executable(self):
        """A config.json binding applies without any fragment."""
        response = _load(CONFIG_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(CONFIG_MODEL)

    def test_003_config_null_unbinds_fragment(self):
        """A config.json null leaves the model on the shared runtime."""
        response = _load(UNBOUND_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        entry = _loaded_entry(UNBOUND_MODEL)
        self.assertIsNotNone(entry)
        self.assertNotEqual(entry["launch_command"][0], self.bound_executable)

    def test_004_config_view_shows_effective_table(self):
        """GET /internal/config shows each binding with its source."""
        response = requests.get(
            _url("/internal/config"), headers=_auth_headers(), timeout=TIMEOUT_DEFAULT
        )
        self.assertEqual(response.status_code, 200, response.text)
        table = response.json()["llamacpp"]["model_executables"]
        self.assertEqual(table[FRAGMENT_MODEL]["executable"], self.bound_executable)
        self.assertEqual(
            table[FRAGMENT_MODEL]["source"],
            os.path.join(self.fragments_dir, f"{FRAGMENT_MODEL}.json"),
        )
        self.assertEqual(table[CONFIG_MODEL]["source"], "config.json")
        self.assertIsNone(table[UNBOUND_MODEL])
        self.assertIn("error", table[MALFORMED_MODEL])

        response = requests.post(
            _url("/internal/set"),
            json={"llamacpp": {"model_executables": {}}},
            headers=_auth_headers(),
            timeout=TIMEOUT_DEFAULT,
        )
        self.assertEqual(response.status_code, 400, response.text)
        self.assertIn("restart", response.text)

    def test_005_binding_failures_leave_other_models_loaded(self):
        """Binding failures surface on every load path and evict nothing."""
        response = _load(ENDPOINT_TEST_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        resident = _loaded_entry(ENDPOINT_TEST_MODEL)
        self.assertIsNotNone(resident)

        response = _load(BAD_PATH_MODEL)
        self.assertNotEqual(response.status_code, 200, response.text)
        self.assertIn(self.missing_executable, response.text)

        response = _load(MALFORMED_MODEL)
        self.assertNotEqual(response.status_code, 200, response.text)
        self.assertIn("invalid", response.text)

        response = requests.post(
            _url("/api/v1/chat/completions"),
            json={
                "model": BAD_PATH_MODEL,
                "messages": [{"role": "user", "content": "Say hello."}],
                "max_tokens": 8,
            },
            headers=_auth_headers(),
            timeout=TIMEOUT_MODEL_OPERATION,
        )
        self.assertNotEqual(response.status_code, 200, response.text)
        self.assertIn(self.missing_executable, response.text)

        self.assertIn(self.missing_executable, self._pin_error(BAD_PATH_MODEL))

        after = _loaded_entry(ENDPOINT_TEST_MODEL)
        self.assertIsNotNone(after, "a binding failure evicted another model")
        self.assertEqual(after["pid"], resident["pid"])

    def test_006_malformed_fragment_keeps_listings_working(self):
        """Model listings and option reads never fail for binding state."""
        response = requests.get(
            _url("/api/v1/models"), headers=_auth_headers(), timeout=TIMEOUT_DEFAULT
        )
        self.assertEqual(response.status_code, 200, response.text)
        response = requests.get(
            _url(f"/api/v1/models/{MALFORMED_MODEL}/options"),
            headers=_auth_headers(),
            timeout=TIMEOUT_DEFAULT,
        )
        self.assertEqual(response.status_code, 200, response.text)

    def test_007_conflicting_backend_is_rejected(self):
        """A request naming another backend fails visibly; unset values proceed."""
        response = _load(FRAGMENT_MODEL, llamacpp_backend=CONFLICTING_BACKEND)
        self.assertEqual(response.status_code, 400, response.text)
        self.assertIn("conflicts", response.text)

        response = requests.post(
            _url(f"/api/v1/models/{FRAGMENT_MODEL}/options"),
            json={"llamacpp_backend": CONFLICTING_BACKEND},
            headers=_auth_headers(),
            timeout=TIMEOUT_DEFAULT,
        )
        self.assertEqual(response.status_code, 400, response.text)

        response = _load(FRAGMENT_MODEL, llamacpp_backend="auto")
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(FRAGMENT_MODEL)

    def test_008_bin_change_and_uninstall_keep_bound_model(self):
        """Shared-install changes never restart a bound model."""
        response = _load(FRAGMENT_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        before = self._assert_bound_launch(FRAGMENT_MODEL)

        response = requests.post(
            _url("/internal/set"),
            json={"llamacpp": {f"{BOUND_BACKEND}_bin": self.shared_executable}},
            headers=_auth_headers(),
            timeout=TIMEOUT_MODEL_OPERATION,
        )
        self.assertEqual(response.status_code, 200, response.text)
        time.sleep(2)
        after_bin = _loaded_entry(FRAGMENT_MODEL)
        self.assertIsNotNone(after_bin, "a *_bin change unloaded the bound model")
        self.assertEqual(after_bin["pid"], before["pid"])

        # The uninstall itself may fail with nothing installed; only the
        # bound model's survival matters here.
        requests.post(
            _url("/api/v1/uninstall"),
            json={"recipe": "llamacpp", "backend": BOUND_BACKEND},
            headers=_auth_headers(),
            timeout=TIMEOUT_MODEL_OPERATION,
        )
        after_uninstall = _loaded_entry(FRAGMENT_MODEL)
        self.assertIsNotNone(after_uninstall, "uninstall unloaded the bound model")
        self.assertEqual(after_uninstall["pid"], before["pid"])

    def test_009_shadowed_builtin_keeps_its_own_binding(self):
        """A binding follows the cache key, never the bare name a user model shares."""
        checkpoint = self._checkpoint_of(ENDPOINT_TEST_MODEL)
        for model_name in (SHADOWING_BOUND_MODEL, SHADOWING_UNBOUND_MODEL):
            response = _register(model_name, checkpoint)
            self.assertEqual(response.status_code, 200, response.text)

        shadowed_unbound = f"builtin.{ENDPOINT_TEST_MODEL}"
        effective = _effective_options(shadowed_unbound)
        self.assertNotEqual(effective.get("llamacpp_backend"), BOUND_BACKEND, effective)
        requests.post(
            _url("/api/v1/unload"),
            json={"model_name": shadowed_unbound},
            headers=_auth_headers(),
            timeout=TIMEOUT_MODEL_OPERATION,
        )
        response = _load(shadowed_unbound)
        self.assertEqual(response.status_code, 200, response.text)
        entry = _loaded_entry(shadowed_unbound)
        self.assertIsNotNone(entry, f"{shadowed_unbound} is not loaded")
        self.assertNotEqual(entry["launch_command"][0], self.bound_executable)
        self.assertNotEqual(
            entry["recipe_options"]["llamacpp_backend"], BOUND_BACKEND, entry
        )

        response = _load(SHADOWING_BOUND_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(SHADOWING_BOUND_MODEL)

        effective = _effective_options(f"builtin.{SHADOWED_BOUND_BUILTIN}")
        self.assertEqual(effective.get("llamacpp_backend"), BOUND_BACKEND, effective)
        self.assertIn(BOUND_BACKEND_ARGS, effective.get("llamacpp_args", ""), effective)
        effective = _effective_options(SHADOWING_UNBOUND_MODEL)
        self.assertNotEqual(effective.get("llamacpp_backend"), BOUND_BACKEND, effective)
        self.assertNotIn(
            BOUND_BACKEND_ARGS, effective.get("llamacpp_args", ""), effective
        )

    def test_010_public_name_key_warns_and_binds_nothing(self):
        """A key that is a public name, not a cache key, warns at startup."""
        warning = f"names no known model: '{_bare(PUBLIC_KEY_MODEL)}'"
        deadline = time.time() + 30
        while warning not in _server_log() and time.time() < deadline:
            time.sleep(1)
        log_text = _server_log()
        self.assertIn(warning, log_text)
        self.assertNotIn(f"names no known model: '{CONFIG_MODEL}'", log_text)

        response = _load(PUBLIC_KEY_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        entry = _loaded_entry(PUBLIC_KEY_MODEL)
        self.assertIsNotNone(entry, f"{PUBLIC_KEY_MODEL} is not loaded")
        self.assertNotEqual(entry["launch_command"][0], self.bound_executable)

    def test_011_conflicting_definition_is_rejected(self):
        """Registration, pull, and local import refuse a conflicting backend."""
        checkpoint = self._checkpoint_of(FRAGMENT_MODEL)
        conflicting = {"llamacpp_backend": CONFLICTING_BACKEND}
        for write, model_name in (
            (_register, REGISTER_TARGET_MODEL),
            (_pull, PULL_TARGET_MODEL),
        ):
            response = write(model_name, checkpoint, recipe_options=conflicting)
            self._assert_conflict_saved_nothing(response, model_name)

            response = write(
                model_name,
                checkpoint,
                recipe_options={"llamacpp_backend": BOUND_BACKEND},
            )
            self.assertEqual(
                response.status_code, 200, f"{model_name}: {response.text}"
            )
            self.assertEqual(_model_status(model_name), 200, model_name)

        # No model directory is staged: the import is refused before lemond
        # looks for one.
        response = _pull_request(
            {
                "model_name": IMPORT_TARGET_MODEL,
                "recipe": "llamacpp",
                "local_import": True,
                "recipe_options": conflicting,
            }
        )
        self._assert_conflict_saved_nothing(response, IMPORT_TARGET_MODEL)

    def test_012_conflicting_collection_component_is_rejected(self):
        """A collection import saves neither a conflicting component nor itself."""
        checkpoint = self._checkpoint_of(FRAGMENT_MODEL)
        saved = (COMPONENT_COLLECTION, COMPONENT_TARGET_MODEL)

        def pull_collection(backend):
            component = _bare(COMPONENT_TARGET_MODEL)
            return _pull_request(
                {
                    "model_name": COMPONENT_COLLECTION,
                    "recipe": "collection.omni",
                    "components": [component],
                    "models": [
                        {
                            "model_name": component,
                            "recipe": "llamacpp",
                            "checkpoint": checkpoint,
                            "recipe_options": {"llamacpp_backend": backend},
                        }
                    ],
                }
            )

        response = pull_collection(CONFLICTING_BACKEND)
        self.assertNotEqual(response.status_code, 200, response.text)
        self.assertIn("conflicts", response.text)
        self.assertIn(COMPONENT_TARGET_MODEL, response.text)
        for model_name in saved:
            self.assertEqual(_model_status(model_name), 404, model_name)

        response = pull_collection(BOUND_BACKEND)
        self.assertEqual(response.status_code, 200, response.text)
        for model_name in saved:
            self.assertEqual(_model_status(model_name), 200, model_name)

    def test_013_no_shared_llamacpp_keeps_bound_models(self):
        """Bound and rejected models keep their behavior without a shared llama.cpp."""
        # Runs after the other tests: it restarts lemond without the PATH
        # llama-server and without the *_bin override test_008 left in
        # config.json.
        system_backend._stop_server()
        type(self)._active = False
        config_path = os.path.join(self.cache_dir, "config.json")
        with open(config_path, "r", encoding="utf-8") as handle:
            config = json.load(handle)
        config["llamacpp"].pop(f"{BOUND_BACKEND}_bin", None)
        config["llamacpp"]["model_executables"][PULL_BOUND_MODEL] = {
            "executable": self.bound_executable,
            "backend": BOUND_BACKEND,
        }
        with open(config_path, "w", encoding="utf-8") as handle:
            json.dump(config, handle)
        os.environ["PATH"] = self.original_env["PATH"] or ""
        system_backend._start_server()
        type(self)._active = True

        runnable = _runnable_shared_backends()
        if runnable:
            self.skipTest(f"this host can still run a shared llama.cpp: {runnable}")

        bound_or_rejected = (
            FRAGMENT_MODEL,
            CONFIG_MODEL,
            BAD_PATH_MODEL,
            MALFORMED_MODEL,
        )
        for model_name in bound_or_rejected:
            response = requests.get(
                _url(f"/api/v1/models/{model_name}"),
                headers=_auth_headers(),
                timeout=TIMEOUT_DEFAULT,
            )
            self.assertEqual(
                response.status_code, 200, f"{model_name}: {response.text}"
            )
        response = requests.get(
            _url(f"/api/v1/models/{UNBOUND_MODEL}"),
            headers=_auth_headers(),
            timeout=TIMEOUT_DEFAULT,
        )
        self.assertEqual(response.status_code, 404, response.text)

        response = _load(FRAGMENT_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(FRAGMENT_MODEL)
        response = _load(UNBOUND_MODEL)
        self.assertEqual(response.status_code, 404, response.text)
        response = _load(MALFORMED_MODEL)
        self.assertNotIn(response.status_code, (200, 404), response.text)
        self.assertIn("invalid", response.text)
        self.assertIn(
            os.path.join(self.fragments_dir, f"{MALFORMED_MODEL}.json"), response.text
        )

        self._wait_for_pin_error(BAD_PATH_MODEL)
        self.assertIn(self.missing_executable, self._pin_error(BAD_PATH_MODEL))

        checkpoint = self._checkpoint_of(FRAGMENT_MODEL)
        response = _pull(PULL_BOUND_MODEL, checkpoint)
        self.assertEqual(response.status_code, 200, response.text)
        response = _load(PULL_BOUND_MODEL)
        self.assertEqual(response.status_code, 200, response.text)
        self._assert_bound_launch(PULL_BOUND_MODEL)
        response = _pull(PULL_UNBOUND_MODEL, checkpoint)
        self.assertNotEqual(response.status_code, 200, response.text)
        self.assertIn("cannot be used on this system", response.text)

        warning = f"names no known model: '{_bare(PUBLIC_KEY_MODEL)}'"
        deadline = time.time() + 30
        while warning not in _server_log() and time.time() < deadline:
            time.sleep(1)
        log_text = _server_log()
        self.assertIn(warning, log_text)
        for model_name in bound_or_rejected:
            self.assertNotIn(f"names no known model: '{model_name}'", log_text)

    def test_014_map_error_fails_every_llamacpp_load(self):
        """A binding error that no single model owns fails every llama.cpp load."""
        # Runs last, after test_013 removed the shared llama.cpp: a fragment
        # path that is not a directory drops every fragment binding.
        system_backend._stop_server()
        type(self)._active = False
        os.environ["PATH"] = self.original_env["PATH"] or ""
        os.environ["LEMONADE_LLAMACPP_BINDINGS_DIR"] = self.shared_executable
        system_backend._start_server()
        type(self)._active = True

        runnable = _runnable_shared_backends()
        if runnable:
            self.skipTest(f"this host can still run a shared llama.cpp: {runnable}")

        # FRAGMENT_MODEL lost its fragment entry; UNBOUND_MODEL is unbound in
        # config.json. Neither may fall back to "not found".
        for model_name in (FRAGMENT_MODEL, UNBOUND_MODEL, CONFIG_MODEL):
            response = requests.get(
                _url(f"/api/v1/models/{model_name}"),
                headers=_auth_headers(),
                timeout=TIMEOUT_DEFAULT,
            )
            self.assertEqual(
                response.status_code, 200, f"{model_name}: {response.text}"
            )
            response = _load(model_name)
            self.assertNotIn(
                response.status_code, (200, 404), f"{model_name}: {response.text}"
            )
            self.assertIn("misconfigured", response.text)
            self.assertIsNone(_loaded_entry(model_name), model_name)

        self._wait_for_pin_error(BAD_PATH_MODEL)
        self.assertIn("misconfigured", self._pin_error(BAD_PATH_MODEL))


def _run_tests():
    """Run llama.cpp executable binding tests."""
    print(f"\n{'=' * 70}")
    print("LLAMACPP EXECUTABLE BINDING TESTS")
    print(f"{'=' * 70}\n")

    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(LlamaCppExecutableBindingTests)
    runner = unittest.TextTestRunner(verbosity=2, buffer=False, failfast=True)
    result = runner.run(suite)
    sys.exit(0 if (result and result.wasSuccessful()) else 1)


if __name__ == "__main__":
    _run_tests()
