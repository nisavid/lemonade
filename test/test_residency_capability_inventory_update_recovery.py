"""Crash-recovery tests for residency inventory document updates."""

from __future__ import annotations

import json
import shutil
import sys
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise document update crash recovery through the CLI."""

    def test_second_artifact_write_failure_rolls_back_without_mutation(self) -> None:
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
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        environment = self._sitecustomize_env("""
import os

_real_open = os.open
_artifact_writes = 0

def _injected_open(path, flags, *args, **kwargs):
    global _artifact_writes
    path_text = os.fspath(path)
    if '.residency-' in path_text and flags & os.O_CREAT:
        _artifact_writes += 1
        if _artifact_writes == 2:
            raise OSError('injected second artifact write failure')
    return _real_open(path, flags, *args, **kwargs)

os.open = _injected_open
""")

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(result, "injected second artifact write failure")
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])

    def test_second_document_rename_failure_rolls_forward_before_reporting(
        self,
    ) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace
_failed = False

def _injected_replace(source, destination, *args, **kwargs):
    global _failed
    if (
        not _failed
        and os.path.abspath(os.fspath(destination)) == os.environ['FAIL_TARGET']
        and os.fspath(source).endswith('.new')
    ):
        _failed = True
        raise OSError('injected second document rename failure')
    return _real_replace(source, destination, *args, **kwargs)

os.replace = _injected_replace
""") | {"FAIL_TARGET": str(self.campaign_path)}

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(result, "injected second document rename failure")
        valid = self._run_cli()
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])

    def test_persistent_recovery_failure_is_a_bounded_domain_error(self) -> None:
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
        environment = (
            self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    source_text = os.fspath(source)
    destination_text = os.path.abspath(os.fspath(destination))
    if (
        destination_text == os.environ['FAIL_TARGET'] and source_text.endswith('.new')
    ) or (
        destination_text == os.environ['ROLLBACK_FAIL_TARGET']
        and source_text.endswith('.old')
    ):
        raise OSError('injected persistent document rename failure')
    return _real_replace(source, destination, *args, **kwargs)

os.replace = _injected_replace
""")
            | {
                "FAIL_TARGET": str(self.campaign_path),
                "ROLLBACK_FAIL_TARGET": str(self.matrix_path),
            }
        )

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(result, "generated-document transaction recovery failed")
        self.assertIn("injected persistent document rename failure", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertTrue(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )

    def test_persistent_new_rename_failure_rolls_back_from_old_artifacts(self) -> None:
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
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        event_log = self.repo / "rollback-events.log"
        environment = (
            self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    source_text = os.fspath(source)
    destination_text = os.path.abspath(os.fspath(destination))
    if destination_text == os.environ['FAIL_TARGET'] and source_text.endswith('.new'):
        raise OSError('injected persistent new-artifact rename failure')
    if source_text.endswith('.old'):
        with open(os.environ['ROLLBACK_EVENT_LOG'], 'a', encoding='utf-8') as stream:
            stream.write(os.path.basename(source_text) + '\\n')
    return _real_replace(source, destination, *args, **kwargs)

os.replace = _injected_replace
""")
            | {
                "FAIL_TARGET": str(self.campaign_path),
                "ROLLBACK_EVENT_LOG": str(event_log),
            }
        )

        result = self._run_cli("--update", env=environment)

        self.assert_invalid(result, "injected persistent new-artifact rename failure")
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)
        self.assertTrue(event_log.read_text(encoding="utf-8").strip().endswith(".old"))
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )

    def test_missing_new_artifact_uses_verified_old_artifact_rollback(self) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    if os.path.abspath(os.fspath(destination)) == os.environ['CRASH_TARGET']:
        os._exit(78)
    return result

os.replace = _injected_replace
""") | {"CRASH_TARGET": str(self.matrix_path)}
        crashed = self._run_cli("--update", env=environment)
        self.assertEqual(crashed.returncode, 78, crashed.stderr)
        campaign_new = next(
            path
            for path in self.campaign_path.parent.glob(".*.residency-*.new")
            if self.campaign_path.name in path.name
        )
        campaign_new.unlink()
        event_log = self.repo / "recovery-rollback-events.log"
        recovery_environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    source_text = os.fspath(source)
    if source_text.endswith('.old'):
        with open(os.environ['ROLLBACK_EVENT_LOG'], 'a', encoding='utf-8') as stream:
            stream.write(os.path.basename(source_text) + '\\n')
    return _real_replace(source, destination, *args, **kwargs)

os.replace = _injected_replace
""") | {"ROLLBACK_EVENT_LOG": str(event_log)}

        recovered = self._run_cli("--update", env=recovery_environment)

        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertTrue(event_log.read_text(encoding="utf-8").strip().endswith(".old"))
        self.assertEqual(self._run_cli().returncode, 0)

    def test_update_recovers_after_process_exit_between_document_renames(self) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    if os.path.abspath(os.fspath(destination)) == os.environ['CRASH_TARGET']:
        os._exit(73)
    return result

os.replace = _injected_replace
""") | {"CRASH_TARGET": str(self.matrix_path)}

        crashed = self._run_cli("--update", env=environment)

        self.assertEqual(crashed.returncode, 73, crashed.stderr)
        self.assertNotIn(
            "### Stale backend variants",
            self.matrix_path.read_text(encoding="utf-8"),
        )
        self.assertIn(
            "#### Stale runtime exact cells",
            self.campaign_path.read_text(encoding="utf-8"),
        )
        pending = self._run_cli()
        self.assert_invalid(pending, "pending generated-document update transaction")

        recovered = self._run_cli("--update")

        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        valid = self._run_cli()
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])

    def test_moved_repository_recovers_from_role_relative_journal_paths(self) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    if os.path.abspath(os.fspath(destination)) == os.environ['CRASH_TARGET']:
        os._exit(76)
    return result

os.replace = _injected_replace
""") | {"CRASH_TARGET": str(self.matrix_path)}
        crashed = self._run_cli("--update", env=environment)
        self.assertEqual(crashed.returncode, 76, crashed.stderr)
        journal_path = self.inventory_path.with_name(
            f".{self.inventory_path.name}.update-v1.json"
        )
        journal = json.loads(journal_path.read_text(encoding="utf-8"))
        self.assertEqual(
            journal["inventory"],
            {
                "role": "inventory",
                "path": "docs/research/portable-residency-capability-inventory.json",
            },
        )
        self.assertEqual(
            [(target["role"], target["path"]) for target in journal["targets"]],
            [
                ("matrix", "docs/research/portable-residency-capability-matrix.md"),
                ("campaign", "docs/research/hatchery-campaign-parameters.md"),
            ],
        )
        self.assertNotIn("old_artifact", journal["targets"][0])
        self.assertNotIn("new_artifact", journal["targets"][0])

        moved_repo = self.repo.with_name("moved-repository")
        shutil.move(self.repo, moved_repo)
        self.repo = moved_repo
        research = self.repo / "docs" / "research"
        self.inventory_path = research / self.inventory_path.name
        self.matrix_path = research / self.matrix_path.name
        self.campaign_path = research / self.campaign_path.name

        recovered = self._run_cli("--update")

        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertEqual(self._run_cli().returncode, 0)
        self.assertFalse(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )

    def test_recovery_rejects_noncanonical_or_traversing_journal_paths(self) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    if os.path.abspath(os.fspath(destination)) == os.environ['CRASH_TARGET']:
        os._exit(77)
    return result

os.replace = _injected_replace
""") | {"CRASH_TARGET": str(self.matrix_path)}
        crashed = self._run_cli("--update", env=environment)
        self.assertEqual(crashed.returncode, 77, crashed.stderr)
        journal_path = self.inventory_path.with_name(
            f".{self.inventory_path.name}.update-v1.json"
        )
        journal = json.loads(journal_path.read_text(encoding="utf-8"))
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        for invalid_path in ("../outside.md", str(self.matrix_path)):
            with self.subTest(path=invalid_path):
                journal["targets"][0]["path"] = invalid_path
                journal_path.write_text(json.dumps(journal) + "\n", encoding="utf-8")

                result = self._run_cli("--update")

                self.assert_invalid(
                    result, "must be a canonical repository-relative path"
                )
                self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
                self.assertEqual(self.campaign_path.read_bytes(), campaign_before)

    def test_recovery_refuses_a_foreign_document_edit(self) -> None:
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
        environment = self._sitecustomize_env("""
import os

_real_replace = os.replace

def _injected_replace(source, destination, *args, **kwargs):
    result = _real_replace(source, destination, *args, **kwargs)
    if os.path.abspath(os.fspath(destination)) == os.environ['CRASH_TARGET']:
        os._exit(74)
    return result

os.replace = _injected_replace
""") | {"CRASH_TARGET": str(self.matrix_path)}
        crashed = self._run_cli("--update", env=environment)
        self.assertEqual(crashed.returncode, 74, crashed.stderr)
        self.campaign_path.write_text(
            self.campaign_path.read_text(encoding="utf-8") + "foreign edit\n",
            encoding="utf-8",
        )

        result = self._run_cli("--update")

        self.assert_invalid(result, "foreign edit to campaign")
        self.assertTrue(
            self.inventory_path.with_name(
                f".{self.inventory_path.name}.update-v1.json"
            ).exists()
        )
        self.assertTrue(
            self.campaign_path.read_text(encoding="utf-8").endswith("foreign edit\n")
        )

    def test_update_rejects_a_duplicate_key_in_the_transaction_journal(self) -> None:
        journal_path = self.inventory_path.with_name(
            f".{self.inventory_path.name}.update-v1.json"
        )
        journal_path.write_text('{"version": 1, "version": 1}\n', encoding="utf-8")

        result = self._run_cli("--update")

        self.assert_invalid(
            result, "transaction journal contains duplicate object key 'version'"
        )
        self.assertTrue(journal_path.exists())

    @unittest.skipUnless(
        sys.platform.startswith("linux"), "/proc/self/fd is Linux-only"
    )
    def test_staging_recovery_tolerates_a_missing_artifact_and_rolls_back(self) -> None:
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
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()
        environment = self._sitecustomize_env("""
import os

_real_fsync = os.fsync

def _injected_fsync(descriptor):
    result = _real_fsync(descriptor)
    try:
        path = os.readlink(f'/proc/self/fd/{descriptor}')
    except OSError:
        return result
    if '.residency-' in path and path.endswith('.old'):
        os._exit(75)
    return result

os.fsync = _injected_fsync
""")
        crashed = self._run_cli("--update", env=environment)
        self.assertEqual(crashed.returncode, 75, crashed.stderr)
        artifacts = list(self.matrix_path.parent.glob(".*.residency-*"))
        self.assertTrue(artifacts)
        for artifact in artifacts:
            if artifact.suffix == ".old":
                artifact.unlink()
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)

        recovered = self._run_cli("--update")

        self.assertEqual(recovered.returncode, 0, recovered.stderr)
        self.assertEqual(self._run_cli().returncode, 0)
        self.assertEqual(list(self.matrix_path.parent.glob(".*.residency-*")), [])


if __name__ == "__main__":
    unittest.main()
