"""Durability and locking tests for residency inventory document updates."""

from __future__ import annotations

import os
import time
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase

try:
    import fcntl
except ImportError:  # pragma: no cover - exercised by the simulated Windows backend.
    fcntl = None


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise document update durability and locking through the CLI."""

    def test_update_fsyncs_directories_at_commit_and_cleanup_boundaries(self) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        self.campaign_path.write_text(
            self.campaign_path.read_text(encoding="utf-8").replace(
                "#### Runtime exact cells", "#### Stale runtime exact cells", 1
            ),
            encoding="utf-8",
        )
        event_log = self.repo / "transaction-events.log"
        environment = (
            self._sitecustomize_env("""
import os
import stat

_real_fsync = os.fsync
_real_replace = os.replace
_real_unlink = os.unlink

def _log(message):
    with open(os.environ['TRANSACTION_EVENT_LOG'], 'a', encoding='utf-8') as stream:
        stream.write(message + '\\n')

def _injected_fsync(descriptor):
    result = _real_fsync(descriptor)
    if stat.S_ISDIR(os.fstat(descriptor).st_mode):
        _log('FSYNC_DIR')
    return result

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    destination = os.path.abspath(os.fspath(destination))
    if destination in {
        os.environ['MATRIX_PATH'],
        os.environ['CAMPAIGN_PATH'],
        os.environ['JOURNAL_PATH'],
    }:
        _log('REPLACE ' + destination)
    return result

class _InjectedUnlink:
    def __call__(self, path, *args, **kwargs):
        result = _real_unlink(path, *args, **kwargs)
        path = os.path.abspath(os.fspath(path))
        if path == os.environ['JOURNAL_PATH'] or '.residency-' in path:
            _log('UNLINK ' + path)
        return result

os.fsync = _injected_fsync
os.replace = _injected_replace
os.unlink = _InjectedUnlink()
""")
            | {
                "TRANSACTION_EVENT_LOG": str(event_log),
                "MATRIX_PATH": str(self.matrix_path),
                "CAMPAIGN_PATH": str(self.campaign_path),
                "JOURNAL_PATH": str(
                    self.inventory_path.with_name(
                        f".{self.inventory_path.name}.update-v1.json"
                    )
                ),
            }
        )

        result = self._run_cli("--update", env=environment)

        self.assertEqual(result.returncode, 0, result.stderr)
        events = event_log.read_text(encoding="utf-8").splitlines()
        matrix_replace = events.index(f"REPLACE {self.matrix_path}")
        campaign_replace = events.index(f"REPLACE {self.campaign_path}")
        journal_unlink = events.index(
            "UNLINK "
            + str(
                self.inventory_path.with_name(
                    f".{self.inventory_path.name}.update-v1.json"
                )
            )
        )
        self.assertIn("FSYNC_DIR", events[matrix_replace + 1 : campaign_replace])
        self.assertIn("FSYNC_DIR", events[campaign_replace + 1 : journal_unlink])
        self.assertIn("FSYNC_DIR", events[journal_unlink + 1 :])

    def test_update_fails_before_staging_when_file_fsync_is_unsupported(self) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        event_log = self.repo / "unsupported-file-fsync-events.log"
        environment = self._sitecustomize_env("""
import os
import stat

_real_fsync = os.fsync
_real_open = os.open

def _injected_open(path, flags, *args, **kwargs):
    if flags & os.O_CREAT and '.residency-' in os.fspath(path):
        with open(os.environ['DURABILITY_EVENT_LOG'], 'a', encoding='utf-8') as stream:
            stream.write('CREATE\\n')
    return _real_open(path, flags, *args, **kwargs)

def _injected_fsync(descriptor):
    if stat.S_ISREG(os.fstat(descriptor).st_mode):
        raise OSError('injected unsupported regular-file fsync')
    return _real_fsync(descriptor)

os.open = _injected_open
os.fsync = _injected_fsync
""") | {"DURABILITY_EVENT_LOG": str(event_log)}

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(
            result, "durable generated-document updates are unsupported"
        )
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertFalse(event_log.exists(), "durability failure staged an artifact")

    def test_update_fails_before_staging_when_directory_fsync_is_unsupported(
        self,
    ) -> None:
        self.matrix_path.write_text(
            self.matrix_path.read_text(encoding="utf-8").replace(
                "### Backend variants", "### Stale backend variants", 1
            ),
            encoding="utf-8",
        )
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        environment = self._sitecustomize_env("""
import os
import stat

_real_fsync = os.fsync

def _injected_fsync(descriptor):
    if stat.S_ISDIR(os.fstat(descriptor).st_mode):
        raise OSError('injected unsupported directory fsync')
    return _real_fsync(descriptor)

os.fsync = _injected_fsync
""")

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(
            result, "durable generated-document updates are unsupported"
        )
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])

    @unittest.skipIf(fcntl is None, "POSIX advisory locks are unavailable")
    def test_validator_clients_serialize_on_the_inventory_lock(self) -> None:
        lock_acquired = self.repo / "lock-acquired"
        release_lock = self.repo / "release-lock"
        environment = (
            os.environ
            | self._sitecustomize_env("""
import fcntl
import os
import time

_real_flock = fcntl.flock

def _injected_flock(descriptor, operation):
    result = _real_flock(descriptor, operation)
    if operation & fcntl.LOCK_EX:
        with open(os.environ['LOCK_ACQUIRED'], 'w', encoding='utf-8') as stream:
            stream.write('locked')
        deadline = time.monotonic() + 10.0
        while not os.path.exists(os.environ['RELEASE_LOCK']):
            if time.monotonic() >= deadline:
                raise RuntimeError('test lock release was never signaled')
            time.sleep(0.01)
    return result

fcntl.flock = _injected_flock
""")
            | {
                "LOCK_ACQUIRED": str(lock_acquired),
                "RELEASE_LOCK": str(release_lock),
            }
        )
        first = self._start_cli_process("--update", env=environment)
        try:
            deadline = time.monotonic() + 5
            while not lock_acquired.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(
                lock_acquired.exists(), "first validator never acquired lock"
            )

            lock_attempted = self.repo / "lock-attempted"
            second_environment = self._sitecustomize_env("""
import fcntl
import os

_real_flock = fcntl.flock

def _observed_flock(descriptor, operation):
    with open(os.environ['LOCK_ATTEMPTED'], 'w', encoding='utf-8') as stream:
        stream.write('attempted')
    return _real_flock(descriptor, operation)

fcntl.flock = _observed_flock
""") | {"LOCK_ATTEMPTED": str(lock_attempted)}
            second = self._start_cli_process(env=second_environment)
            deadline = time.monotonic() + 5
            while not lock_attempted.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(
                lock_attempted.exists(), "second validator never attempted the lock"
            )
            self.assertIsNone(second.poll(), "second validator bypassed inventory lock")
        finally:
            release_lock.write_text("release", encoding="utf-8")

        first_stdout, first_stderr = first.communicate(timeout=5)
        second_stdout, second_stderr = second.communicate(timeout=5)

        self.assertEqual(first.returncode, 0, first_stderr or first_stdout)
        self.assertEqual(second.returncode, 0, second_stderr or second_stdout)

    @unittest.skipIf(fcntl is None, "POSIX advisory locks are unavailable")
    def test_posix_lock_uses_shared_read_and_exclusive_update_modes(self) -> None:
        event_log = self.repo / "posix-lock-events.log"
        environment = self._sitecustomize_env("""
import fcntl
import os

_real_flock = fcntl.flock

def _injected_flock(descriptor, operation):
    if operation & fcntl.LOCK_SH:
        event = 'read'
    elif operation & fcntl.LOCK_EX:
        event = 'update'
    else:
        event = 'other'
    with open(os.environ['LOCK_EVENT_LOG'], 'a', encoding='utf-8') as stream:
        stream.write(event + '\\n')
    return _real_flock(descriptor, operation)

fcntl.flock = _injected_flock
""") | {"LOCK_EVENT_LOG": str(event_log)}

        read_result = self._run_cli(env=environment)
        update_result = self._run_cli("--update", env=environment)

        self.assertEqual(read_result.returncode, 0, read_result.stderr)
        self.assertEqual(update_result.returncode, 0, update_result.stderr)
        self.assertEqual(
            event_log.read_text(encoding="utf-8").splitlines(), ["read", "update"]
        )

    @unittest.skipIf(fcntl is None, "POSIX advisory locks are unavailable")
    def test_posix_lock_failure_is_bounded(self) -> None:
        environment = self._sitecustomize_env("""
import fcntl

def _injected_flock(descriptor, operation):
    raise OSError('injected POSIX lock failure')

fcntl.flock = _injected_flock
""")

        result = self._run_cli(env=environment)

        self.assert_invalid(result, "injected POSIX lock failure")
        self.assertNotIn("Traceback", result.stderr)

    def test_simulated_windows_lock_uses_read_and_update_modes(self) -> None:
        event_log = self.repo / "windows-lock-events.log"
        environment = self._sitecustomize_env("""
import builtins
import os
import types

_real_import = builtins.__import__
_fake_msvcrt = types.ModuleType('msvcrt')
_fake_msvcrt.LK_RLCK = 101
_fake_msvcrt.LK_LOCK = 102
_fake_msvcrt.LK_UNLCK = 103

def _locking(descriptor, operation, length):
    names = {101: 'read', 102: 'update', 103: 'unlock'}
    with open(os.environ['LOCK_EVENT_LOG'], 'a', encoding='utf-8') as stream:
        stream.write(names[operation] + '\\n')

_fake_msvcrt.locking = _locking

def _injected_import(name, globals=None, locals=None, fromlist=(), level=0):
    importer = '' if globals is None else globals.get('__name__', '')
    if importer == 'residency_inventory.transaction':
        if name == 'fcntl':
            raise ImportError('simulated non-POSIX platform')
        if name == 'msvcrt':
            return _fake_msvcrt
    return _real_import(name, globals, locals, fromlist, level)

builtins.__import__ = _injected_import
""") | {"LOCK_EVENT_LOG": str(event_log)}

        read_result = self._run_cli(env=environment)
        update_result = self._run_cli("--update", env=environment)

        self.assertEqual(read_result.returncode, 0, read_result.stderr)
        self.assertEqual(update_result.returncode, 0, update_result.stderr)
        self.assertEqual(
            event_log.read_text(encoding="utf-8").splitlines(),
            ["read", "unlock", "update", "unlock"],
        )

    def test_simulated_windows_lock_failure_is_bounded(self) -> None:
        environment = self._sitecustomize_env("""
import builtins
import types

_real_import = builtins.__import__
_fake_msvcrt = types.ModuleType('msvcrt')
_fake_msvcrt.LK_RLCK = 101
_fake_msvcrt.LK_LOCK = 102
_fake_msvcrt.LK_UNLCK = 103

def _locking(descriptor, operation, length):
    raise OSError('injected Windows lock failure')

_fake_msvcrt.locking = _locking

def _injected_import(name, globals=None, locals=None, fromlist=(), level=0):
    importer = '' if globals is None else globals.get('__name__', '')
    if importer == 'residency_inventory.transaction':
        if name == 'fcntl':
            raise ImportError('simulated non-POSIX platform')
        if name == 'msvcrt':
            return _fake_msvcrt
    return _real_import(name, globals, locals, fromlist, level)

builtins.__import__ = _injected_import
""")

        result = self._run_cli(env=environment)

        self.assert_invalid(result, "injected Windows lock failure")
        self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
