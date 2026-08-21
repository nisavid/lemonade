"""Public CLI tests for the portable residency implementation handoff."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from collections.abc import Callable
from pathlib import Path
from unittest import mock

from tools.residency_handoff.contract import HandoffError
from tools.residency_handoff.validation import (
    GitRepository,
    RedFixtureObservation,
    _apply_patch,
    _patch_paths,
    _patched_fixture_digests,
    _red_fixture_metadata,
    _red_fixture_metadata_v2,
    _run_at_commit,
)

REPO_ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = REPO_ROOT / "tools" / "validate_residency_implementation_handoff.py"
SOURCE_REVISION = (
    REPO_ROOT
    / "plan"
    / "evidence"
    / "source-revisions"
    / "v11.7.0"
    / "source-revision.json"
)
REQUIRED_SEAMS = (
    "artifact_install_commit",
    "artifact_pull_import_delete_cleanup",
    "backend_ops_dispatch",
    "backend_process_lifecycle_hooks",
    "configuration_and_legacy_keys",
    "existing_residency_tests",
    "fork_plan_gpu_memory_admission",
    "process_ownership_and_release",
    "public_api_route_mappings",
    "registry_artifact_selection",
    "router_eviction_and_retry",
    "stable_eviction_engine",
    "stable_global_vram_monitor",
)
LATER_UNIT_IDS = (
    "H-VULKAN-ADM-GTT-HOST-v1",
    "H-VULKAN-PRE-GTT-HOST-v1",
    "H-VULKAN-STA-GTT-HOST-v1",
    "H-VULKAN-REC-GTT-HOST-OWN-v1",
    *(
        f"W-XDNA2-{participant}-{operation}-v1"
        for participant in (
            "FLM-NPU-LLM",
            "FLM-NPU-EMBEDDING",
            "FLM-NPU-TRANSCRIPTION",
            "WHISPERCPP-NPU-TRANSCRIPTION",
            "RYZENAI-LLM-NPU-LLM",
        )
        for operation in ("ADM", "LFR", "STA", "REC", "UNL", "PIN")
    ),
)
TASK_020_COMMAND = [
    "python3",
    "-B",
    "test/residency/recovery/test_durable_journal_public_seam.py",
]
TASK_020_FAILURE = "TASK-020 durable residency persistence contract is unavailable"
TASK_020_BASE = "0c93411bc9b0463eee0f56d38cd97fffb0bae633"
TASK_020_EVIDENCE = "14a737a4f80510cb86f1e4e30a9a4d0d6ccc9c5a"
TASK_020_BUNDLE = "4e9ff6284e599bfb3869d929416b2f915524eab14506620abd1d7fe95f93a650"
TASK_020_PATCH_PATH = "plan/evidence/red-fixtures/TASK-020/test.patch"
TASK_020_PATCH_SHA256 = (
    "0e1dc4d01f848c19c41aade640b14f9ea656d00f358b8d82a58f8099fda12213"
)
TASK_020_RESULT_PATH = "plan/evidence/red-fixtures/TASK-020/result.json"
TASK_020_FIXTURE_PATHS = (
    "test/residency/recovery/durable_journal_private_authority_gate.cpp",
    "test/residency/recovery/durable_journal_public_seam.cpp",
    "test/residency/recovery/journal_persistence_test_support.cpp",
    "test/residency/recovery/journal_persistence_test_support.h",
    "test/residency/recovery/test_durable_journal_public_seam.py",
)
TASK_020_FIXTURE_SHA256 = {
    TASK_020_FIXTURE_PATHS[0]: (
        "9869521ed951ae3df9e9947d118665f65e3eda04edb854fd7711aa2160d31f63"
    ),
    TASK_020_FIXTURE_PATHS[1]: (
        "5ecadcea7ac2c0ed17a4ae95474f0b46f6848acf780c1b418ddaa02ebf1af30e"
    ),
    TASK_020_FIXTURE_PATHS[2]: (
        "27598da43517dbf829345a9d8cc2d1d61812a8669c21d25f3f4fd9217e827561"
    ),
    TASK_020_FIXTURE_PATHS[3]: (
        "24f2a89d3b670708157d69c1fca5565fd72dc3ebaa69c11cd59f072f58334e01"
    ),
    TASK_020_FIXTURE_PATHS[4]: (
        "8be11f9d6fa5fb7a22a2b50197a884855bad6d23739d3f44f2b23486730388a6"
    ),
}


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _record_digest(record: dict[str, object]) -> str:
    payload = {key: value for key, value in record.items() if key != "record_digest"}
    return _sha256(
        json.dumps(
            payload,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        ).encode("utf-8")
    )


def _directory_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(
        candidate for candidate in root.rglob("*") if candidate.is_file()
    ):
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


class HandoffRepository:
    def __init__(self, root: Path, *, object_format: str = "sha1"):
        self.root = root
        self.object_format = object_format
        self.manifest_path = (
            root / "plan" / "portable-residency-implementation-base.json"
        )
        self.manifest: dict[str, object] = {}
        self._git("init", "-q", f"--object-format={object_format}")
        self._git("config", "user.name", "Handoff Test")
        self._git("config", "user.email", "handoff@example.invalid")

    def _git(self, *args: str) -> str:
        result = subprocess.run(
            ["git", *args],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    def _commit(self, message: str) -> str:
        self._git("add", "-A")
        self._git("commit", "-q", "-m", message)
        return self._git("rev-parse", "HEAD")

    def _tree(self, commit: str) -> str:
        return self._git("rev-parse", f"{commit}^{{tree}}")

    def _blob_digest(self, commit: str, path: str) -> str:
        result = subprocess.run(
            ["git", "show", f"{commit}:{path}"],
            cwd=self.root,
            capture_output=True,
            check=True,
        )
        return _sha256(result.stdout)

    def _write(self, path: str, value: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value, encoding="utf-8")

    @staticmethod
    def _fallback() -> dict[str, str]:
        return {"authority": "legacy_runtime", "status": "active"}

    @staticmethod
    def _validation() -> list[dict[str, object]]:
        return [
            {
                "command": ["python3", "-m", "unittest", "-q"],
                "result": "pass",
            }
        ]

    @staticmethod
    def _new_file_patch(path: str, value: str) -> bytes:
        lines = value.splitlines(keepends=True)
        additions = "".join(f"+{line}" for line in lines)
        return (
            f"diff --git a/{path} b/{path}\n"
            "new file mode 100644\n"
            "index 0000000..1111111\n"
            "--- /dev/null\n"
            f"+++ b/{path}\n"
            f"@@ -0,0 +1,{len(lines)} @@\n"
            f"{additions}"
        ).encode()

    def _task_cycle(
        self,
        task_id: str,
        task_base: str,
        *,
        add_undeclared_test_path: bool = False,
        omit_reproducibility_metadata: bool = False,
        failure_signature: str = "FileNotFoundError",
        test_root: str = "test/",
    ) -> tuple[dict[str, object], str]:
        suffix = task_id.replace("-", "_").lower()
        test_path = f"{test_root}test_{suffix}.py"
        command = [
            "python3",
            "-m",
            "unittest",
            "discover",
            "-s",
            "test",
            "-p",
            Path(test_path).name,
            "-q",
        ]
        output_path = f"outputs/{task_id}.txt"
        test_source = (
            "import subprocess\n"
            "import unittest\n"
            "from pathlib import Path\n"
            "\n"
            "class HandoffProbe(unittest.TestCase):\n"
            "    def test_output_and_pinned_tree_resolve(self):\n"
            f"        self.assertEqual(Path({output_path!r}).read_text(encoding='utf-8'), 'ready\\n')\n"
            "        result = subprocess.run(\n"
            f"            ['git', 'rev-parse', '{task_base}^{{tree}}'],\n"
            "            capture_output=True, text=True, check=False,\n"
            "        )\n"
            "        self.assertEqual(result.returncode, 0, result.stderr)\n"
            "        object_format = subprocess.run(\n"
            "            ['git', 'rev-parse', '--show-object-format'],\n"
            "            capture_output=True, text=True, check=False,\n"
            "        )\n"
            "        self.assertEqual(object_format.returncode, 0, object_format.stderr)\n"
            f"        self.assertEqual(object_format.stdout.strip(), {self.object_format!r})\n"
        )
        patch = self._new_file_patch(test_path, test_source)
        if add_undeclared_test_path:
            patch += self._new_file_patch(
                f"test/undeclared_{suffix}.py", "UNDECLARED = True\n"
            )
        evidence_prefix = f"plan/evidence/red-fixtures/{task_id}"
        patch_path = f"{evidence_prefix}/test.patch"
        metadata_path = f"{evidence_prefix}/result.json"
        metadata_payload: dict[str, object] = {
            "schema": "portable_residency_red_fixture_result/v1",
            "task_id": task_id,
            "task_base_commit": task_base,
            "command": command,
            "environment": {
                "platform": "test-platform",
                "python_version": sys.version.split()[0],
                "toolchain": ["python3", "unittest"],
            },
            "observed_result": {
                "exit_code": 1,
                "failure_signature": failure_signature,
            },
        }
        if omit_reproducibility_metadata:
            metadata_payload.pop("environment")
        metadata = json.dumps(metadata_payload, sort_keys=True) + "\n"
        (self.root / patch_path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / patch_path).write_bytes(patch)
        self._write(metadata_path, metadata)
        evidence_commit = self._commit(f"test: record {task_id} red fixture")
        changed = sorted((patch_path, metadata_path))
        bundle = bytearray()
        for path in changed:
            bundle.extend(path.encode("utf-8"))
            bundle.append(0)
            bundle.extend((self.root / path).read_bytes())
            bundle.append(0)

        applied = subprocess.run(
            ["git", "apply", str(self.root / patch_path)],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )
        if applied.returncode:
            raise AssertionError(applied.stderr)
        self._write(output_path, "ready\n")
        green_commit = self._commit(f"feat: complete {task_id}")
        task = {
            "task_id": task_id,
            "task_base_commit": task_base,
            "outputs": [
                {
                    "path": output_path,
                    "sha256": self._blob_digest(green_commit, output_path),
                }
            ],
            "red_fixture": {
                "evidence_commit": evidence_commit,
                "evidence_tree": self._tree(evidence_commit),
                "bundle_sha256": _sha256(bytes(bundle)),
                "patch_path": patch_path,
                "patch_sha256": _sha256(patch),
                "metadata_path": metadata_path,
                "test_paths": [test_path],
                "command": command,
                "failure_signature": failure_signature,
            },
            "green_checkpoint": {
                "commit": green_commit,
                "tree": self._tree(green_commit),
                "command": command,
                "result": "pass",
            },
            "fallback_state": self._fallback(),
        }
        return task, green_commit

    def build(
        self,
        *,
        add_undeclared_test_path: bool = False,
        incomplete_bootstrap_precursor: bool = False,
        omit_reproducibility_metadata: bool = False,
        recorded_failure_signature: str = "FileNotFoundError",
        red_test_root: str = "test/",
        add_unrelated_phase_append_path: bool = False,
    ) -> None:
        all_symbols = "legacy_symbol\n"
        self._write("research.txt", "research closure\n")
        self._write("source.cpp", all_symbols)
        roster = [
            {"unit_id": unit_id, "issue_id": 100 + index}
            for index, unit_id in enumerate(LATER_UNIT_IDS)
        ]
        self._write(
            "docs/research/portable-residency-capability-inventory.json",
            json.dumps({"later_promotion_roster": roster}, sort_keys=True) + "\n",
        )
        stable_commit = self._commit("chore: stable source")
        self._git("tag", "v1.0.0", stable_commit)
        self._git("branch", "upstream-stable", stable_commit)
        self._git("update-ref", "refs/remotes/origin/upstream-stable", stable_commit)

        research_closure = {
            "baseline_commit": stable_commit,
            "baseline_tree": self._tree(stable_commit),
        }
        stable_release = {
            "tag": "v1.0.0",
            "release_url": (
                "https://github.com/lemonade-sdk/lemonade/releases/tag/v1.0.0"
            ),
            "tag_object": None,
            "peeled_commit": stable_commit,
            "local_ref": "refs/heads/upstream-stable",
            "local_ref_commit": stable_commit,
            "origin_ref": "refs/remotes/origin/upstream-stable",
            "origin_ref_commit": stable_commit,
        }
        implementation_base = {
            "commit": stable_commit,
            "tree": self._tree(stable_commit),
        }
        pre_handoff = []
        for task_id in ("TASK-001", "TASK-002", "TASK-003", "TASK-004"):
            pre_handoff.append(
                {
                    "task_id": task_id,
                    "classification": "research_refresh",
                    "source_relation": {
                        "kind": "implementation_base_ancestor",
                        "implementation_base_commit": stable_commit,
                    },
                    "checkpoint_commit": stable_commit,
                    "checkpoint_tree": self._tree(stable_commit),
                    "outputs": [
                        {
                            "path": "research.txt",
                            "sha256": self._blob_digest(stable_commit, "research.txt"),
                        }
                    ],
                    "validation_results": self._validation(),
                    "fallback_state": self._fallback(),
                }
            )

        precursor = (
            {"phase_records": []}
            if incomplete_bootstrap_precursor
            else {
                "schema": "portable_residency_implementation_handoff/v1",
                "research_closure": research_closure,
                "stable_release": stable_release,
                "implementation_base": implementation_base,
                "pre_handoff_records": pre_handoff,
                "phase_records": [],
            }
        )
        self._write(
            "plan/portable-residency-implementation-base.json",
            json.dumps(precursor, indent=2, sort_keys=True) + "\n",
        )
        bootstrap_commit = self._commit("chore: bootstrap implementation manifest")

        tasks: list[dict[str, object]] = []
        task_base = bootstrap_commit
        for task_id in ("TASK-093", "TASK-006", "TASK-007", "TASK-108", "TASK-109"):
            task, task_base = self._task_cycle(
                task_id,
                task_base,
                add_undeclared_test_path=(
                    add_undeclared_test_path and task_id == "TASK-093"
                ),
                omit_reproducibility_metadata=(
                    omit_reproducibility_metadata and task_id == "TASK-093"
                ),
                failure_signature=(
                    recorded_failure_signature
                    if task_id == "TASK-093"
                    else "FileNotFoundError"
                ),
                test_root=red_test_root if task_id == "TASK-093" else "test/",
            )
            tasks.append(task)
        self.manifest = {
            "schema": "portable_residency_implementation_handoff/v1",
            "research_closure": research_closure,
            "stable_release": stable_release,
            "implementation_base": implementation_base,
            "scout_runs": [
                {
                    "id": "implementation-base-source-scout",
                    "commit": task_base,
                    "tree": self._tree(task_base),
                    "inputs": [
                        {
                            "path": "source.cpp",
                            "sha256": self._blob_digest(task_base, "source.cpp"),
                        }
                    ],
                    "outputs": tasks[1]["outputs"],
                }
            ],
            "legacy_inventory": [
                {
                    "path": "source.cpp",
                    "symbol": "legacy_symbol",
                    "blob_sha256": self._blob_digest(stable_commit, "source.cpp"),
                }
            ],
            "source_seam_dispositions": [
                {
                    "id": seam,
                    "disposition": "retained",
                    "path": "source.cpp",
                    "symbol": "legacy_symbol",
                }
                for seam in REQUIRED_SEAMS
            ],
            "later_cell_issues": [
                {
                    "promotion_unit_id": item["unit_id"],
                    "issue_id": item["issue_id"],
                    "url": (
                        "https://github.com/nisavid/lemonade/issues/"
                        f"{item['issue_id']}"
                    ),
                }
                for item in roster
            ],
            "pre_handoff_records": pre_handoff,
            "implementation_base_bootstrap": {
                "task_id": "TASK-005",
                "classification": "implementation_base_bootstrap",
                "checkpoint_commit": bootstrap_commit,
                "checkpoint_tree": self._tree(bootstrap_commit),
                "outputs": [
                    {
                        "path": "plan/portable-residency-implementation-base.json",
                        "sha256": self._blob_digest(
                            bootstrap_commit,
                            "plan/portable-residency-implementation-base.json",
                        ),
                    }
                ],
                "validation_results": self._validation(),
                "fallback_state": self._fallback(),
            },
            "phase_records": [],
        }
        self.write_manifest()
        phase_checkpoint = self._commit("docs: freeze Phase 0 checkpoint")
        phase_record: dict[str, object] = {
            "phase": 0,
            "task_ids": [
                "TASK-093",
                "TASK-006",
                "TASK-007",
                "TASK-108",
                "TASK-109",
            ],
            "task_evidence": tasks,
            "phase_checkpoint_commit": phase_checkpoint,
            "phase_checkpoint_tree": self._tree(phase_checkpoint),
        }
        phase_record["record_digest"] = _record_digest(phase_record)
        self.manifest["phase_records"] = [phase_record]
        self.write_manifest()
        if add_unrelated_phase_append_path:
            self._write("unrelated-phase-append.txt", "not evidence-only\n")
        self._commit("docs: append Phase 0 implementation handoff")

    def write_manifest(self) -> None:
        self.manifest_path.write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def run_validator(self, phase: int = 0) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--manifest",
                str(self.manifest_path),
                "--phase",
                str(phase),
            ],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )


class SourceCampaignRepository:
    def __init__(self, root: Path):
        self.root = root
        self.source_path = (
            root
            / "plan"
            / "evidence"
            / "source-revisions"
            / "v2.0.0"
            / "source-revision.json"
        )
        self.binding_path = self.source_path.with_name("maintained-fork-binding.json")
        self._git("init", "-q")
        self._git("config", "user.name", "Source Campaign Test")
        self._git("config", "user.email", "source-campaign@example.invalid")

    def _git(self, *args: str) -> str:
        result = subprocess.run(
            ["git", *args],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    def _write(self, path: str, value: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value, encoding="utf-8")

    def _commit(self, message: str) -> str:
        self._git("add", "-A")
        self._git("commit", "-q", "-m", message)
        return self._git("rev-parse", "HEAD")

    def _tree(self, commit: str) -> str:
        return self._git("rev-parse", f"{commit}^{{tree}}")

    def _blob(self, commit: str, path: str) -> bytes | None:
        result = subprocess.run(
            ["git", "show", f"{commit}:{path}"],
            cwd=self.root,
            capture_output=True,
            check=False,
        )
        return result.stdout if result.returncode == 0 else None

    def build(self) -> None:
        sources = {
            "source/unchanged.cpp": "unchanged\n",
            "source/changed.cpp": "old\n",
            "source/fork_only.cpp": "fork only\n",
        }
        for path, value in sources.items():
            self._write(path, value)
        implementation_base = self._commit("chore: predecessor implementation base")
        scout_inputs = [
            {"path": path, "sha256": _sha256(value.encode())}
            for path, value in sources.items()
        ]
        scout_path = "plan/evidence/phase-0/stable-source-scout.json"
        self._write(
            scout_path,
            json.dumps(
                {
                    "schema": "portable_residency_stable_source_scout/v1",
                    "inputs": scout_inputs,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        scout_commit = self._commit("docs: record predecessor source scout")
        manifest_path = "plan/portable-residency-implementation-base.json"
        precursor = {
            "schema": "portable_residency_implementation_handoff/v1",
            "stable_release": {"peeled_commit": implementation_base},
            "implementation_base": {"commit": implementation_base},
            "phase_records": [],
        }
        self._write(
            manifest_path,
            json.dumps(precursor, indent=2, sort_keys=True) + "\n",
        )
        self._commit("chore: bootstrap predecessor handoff")
        self._write("phase-output.txt", "accepted\n")
        phase_checkpoint = self._commit("docs: freeze predecessor phase checkpoint")
        phase_record: dict[str, object] = {
            "phase": 0,
            "phase_checkpoint_commit": phase_checkpoint,
            "phase_checkpoint_tree": self._tree(phase_checkpoint),
        }
        phase_record["record_digest"] = _record_digest(phase_record)
        accepted_manifest = {**precursor, "phase_records": [phase_record]}
        self._write(
            manifest_path,
            json.dumps(accepted_manifest, indent=2, sort_keys=True) + "\n",
        )
        append_commit = self._commit("docs: append predecessor phase evidence")
        self._write("later.txt", "maintained fork work\n")
        fork_base = self._commit("feat: advance maintained fork")

        self._git("switch", "-q", "-c", "upstream-release")
        self._write("source/changed.cpp", "new upstream\n")
        (self.root / "source" / "fork_only.cpp").unlink()
        release_commit = self._commit("chore: upstream v2 release")
        self._git("tag", "v2.0.0", release_commit)
        self._git("switch", "-q", "-")

        source_payload: dict[str, object] = {
            "schema": "portable_residency_source_revision/v1",
            "revision_id": "v2.0.0-forward-1",
            "predecessor_campaign": {
                "manifest_path": manifest_path,
                "accepted_append_commit": append_commit,
                "accepted_append_tree": self._tree(append_commit),
                "manifest_sha256": _sha256(
                    self._blob(append_commit, manifest_path) or b""
                ),
                "terminal_phase": 0,
                "terminal_record_digest": phase_record["record_digest"],
                "stable_release_commit": implementation_base,
                "implementation_base_commit": implementation_base,
            },
            "predecessor_scout": {
                "path": scout_path,
                "checkpoint_commit": scout_commit,
                "checkpoint_tree": self._tree(scout_commit),
                "sha256": _sha256(self._blob(scout_commit, scout_path) or b""),
            },
            "maintained_fork_base": {
                "commit": fork_base,
                "tree": self._tree(fork_base),
            },
            "upstream_release": {
                "tag": "v2.0.0",
                "release_url": (
                    "https://github.com/lemonade-sdk/lemonade/releases/tag/v2.0.0"
                ),
                "tag_object": None,
                "peeled_commit": release_commit,
                "tree": self._tree(release_commit),
            },
            "scout_input_dispositions": [],
            "fallback_state": {"authority": "legacy_runtime", "status": "active"},
            "runtime_authority": "none",
            "status": "awaiting_maintained_fork_binding",
        }
        for item in scout_inputs:
            path = str(item["path"])
            upstream = self._blob(release_commit, path)
            upstream_digest = _sha256(upstream) if upstream is not None else None
            predecessor_digest = str(item["sha256"])
            source_payload["scout_input_dispositions"].append(
                {
                    "path": path,
                    "predecessor_sha256": predecessor_digest,
                    "upstream_sha256": upstream_digest,
                    "disposition": (
                        "candidate_for_carry_forward"
                        if upstream_digest == predecessor_digest
                        else "revalidation_required"
                    ),
                }
            )
        source_payload["record_digest"] = _record_digest(source_payload)
        self._write(
            self.source_path.relative_to(self.root).as_posix(),
            json.dumps(source_payload, indent=2, sort_keys=True) + "\n",
        )
        self.source_commit = self._commit("docs: open source revision")
        self.source_payload = source_payload
        self.release_commit = release_commit
        self.fork_base = fork_base

    def run_source_validator(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--source-revision",
                str(self.source_path),
                *extra,
            ],
            cwd=self.root,
            capture_output=True,
            text=True,
            check=False,
        )

    def finish_binding(self) -> None:
        self._git(
            "merge",
            "-q",
            "--no-ff",
            "upstream-release",
            "-m",
            "merge: reconcile upstream release",
        )
        reviewed_commit = self._git("rev-parse", "HEAD")
        evidence_path = "plan/evidence/source-revisions/v2.0.0/scout-revalidation.json"
        self._write(
            evidence_path,
            json.dumps(
                {
                    "schema": "portable_residency_source_revalidation/v1",
                    "reviewed_maintained_fork_commit": reviewed_commit,
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
        )
        evidence_commit = self._commit("test: record source revalidation")
        evidence = {
            "path": evidence_path,
            "checkpoint_commit": evidence_commit,
            "checkpoint_tree": self._tree(evidence_commit),
            "sha256": _sha256(self._blob(evidence_commit, evidence_path) or b""),
        }
        dispositions: list[dict[str, object]] = []
        for source_item in self.source_payload["scout_input_dispositions"]:
            path = str(source_item["path"])
            fork_blob = self._blob(reviewed_commit, path)
            fork_digest = _sha256(fork_blob) if fork_blob is not None else None
            predecessor_digest = str(source_item["predecessor_sha256"])
            carried = fork_digest == predecessor_digest
            dispositions.append(
                {
                    "path": path,
                    "fork_sha256": fork_digest,
                    "disposition": "carried_forward" if carried else "revalidated",
                    "evidence": None if carried else evidence,
                }
            )
        source_path = self.source_path.relative_to(self.root).as_posix()
        binding: dict[str, object] = {
            "schema": "portable_residency_maintained_fork_binding/v1",
            "revision_id": self.source_payload["revision_id"],
            "source_revision": {
                "path": source_path,
                "checkpoint_commit": self.source_commit,
                "checkpoint_tree": self._tree(self.source_commit),
                "sha256": _sha256(self._blob(self.source_commit, source_path) or b""),
                "record_digest": self.source_payload["record_digest"],
            },
            "reviewed_maintained_fork": {
                "commit": reviewed_commit,
                "tree": self._tree(reviewed_commit),
                "review_url": "https://github.com/nisavid/lemonade/pull/123",
                "reviewed_commit": reviewed_commit,
            },
            "scout_input_dispositions": dispositions,
            "fallback_state": {"authority": "legacy_runtime", "status": "active"},
            "runtime_authority": "none",
            "status": "source_ready",
        }
        binding["record_digest"] = _record_digest(binding)
        self._write(
            self.binding_path.relative_to(self.root).as_posix(),
            json.dumps(binding, indent=2, sort_keys=True) + "\n",
        )
        self.binding_payload = binding
        self.reviewed_commit = reviewed_commit


class ResidencyImplementationHandoffCliTest(unittest.TestCase):
    def test_reviewed_fork_binding_can_make_the_source_revision_ready(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.finish_binding()

            result = fixture.run_source_validator(
                "--fork-binding",
                str(fixture.binding_path),
                "--require-source-ready",
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "portable residency source revision: source ready\n",
        )

    def test_reviewed_fork_identity_must_be_distinct_from_the_release(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.finish_binding()
            reviewed = fixture.binding_payload["reviewed_maintained_fork"]
            reviewed["commit"] = fixture.release_commit
            reviewed["tree"] = fixture._tree(fixture.release_commit)
            reviewed["reviewed_commit"] = fixture.release_commit
            fixture.binding_payload["record_digest"] = _record_digest(
                fixture.binding_payload
            )
            fixture._write(
                fixture.binding_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.binding_payload, indent=2, sort_keys=True) + "\n",
            )

            result = fixture.run_source_validator(
                "--fork-binding", str(fixture.binding_path)
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("must be distinct from upstream release", result.stderr)

    def test_reviewed_fork_must_descend_from_both_source_lines(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.finish_binding()
            reviewed = fixture.binding_payload["reviewed_maintained_fork"]
            unrelated = fixture._git(
                "commit-tree",
                reviewed["tree"],
                "-m",
                "unrelated reviewed candidate",
            )
            reviewed["commit"] = unrelated
            reviewed["reviewed_commit"] = unrelated
            fixture.binding_payload["record_digest"] = _record_digest(
                fixture.binding_payload
            )
            fixture._write(
                fixture.binding_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.binding_payload, indent=2, sort_keys=True) + "\n",
            )

            result = fixture.run_source_validator(
                "--fork-binding", str(fixture.binding_path)
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("must descend from selected upstream release", result.stderr)

    def test_changed_fork_input_cannot_be_carried_forward(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.finish_binding()
            changed = fixture.binding_payload["scout_input_dispositions"][1]
            changed["disposition"] = "carried_forward"
            changed["evidence"] = None
            fixture.binding_payload["record_digest"] = _record_digest(
                fixture.binding_payload
            )
            fixture._write(
                fixture.binding_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.binding_payload, indent=2, sort_keys=True) + "\n",
            )

            result = fixture.run_source_validator(
                "--fork-binding", str(fixture.binding_path)
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("must be 'revalidated' or 'revalidation_required'", result.stderr)

    def test_due_fork_input_keeps_the_revision_non_authorizing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.finish_binding()
            changed = fixture.binding_payload["scout_input_dispositions"][1]
            changed["disposition"] = "revalidation_required"
            changed["evidence"] = None
            fixture.binding_payload["status"] = "awaiting_revalidation"
            fixture.binding_payload["record_digest"] = _record_digest(
                fixture.binding_payload
            )
            fixture._write(
                fixture.binding_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.binding_payload, indent=2, sort_keys=True) + "\n",
            )

            pending = fixture.run_source_validator(
                "--fork-binding", str(fixture.binding_path)
            )
            required = fixture.run_source_validator(
                "--fork-binding",
                str(fixture.binding_path),
                "--require-source-ready",
            )

        self.assertEqual(pending.returncode, 0, pending.stderr)
        self.assertEqual(
            pending.stdout,
            "portable residency source revision: awaiting revalidation\n",
        )
        self.assertEqual(required.returncode, 2)
        self.assertIn("source revision is not ready", required.stderr)

    def test_source_records_cannot_grant_runtime_authority(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            fixture.source_payload["runtime_authority"] = "production"
            fixture.source_payload["record_digest"] = _record_digest(
                fixture.source_payload
            )
            fixture._write(
                fixture.source_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.source_payload, indent=2, sort_keys=True) + "\n",
            )

            result = fixture.run_source_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("runtime_authority must be 'none'", result.stderr)

    def test_source_revision_classifies_changed_scout_inputs_as_due(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = SourceCampaignRepository(Path(directory))
            fixture.build()
            changed = fixture.source_payload["scout_input_dispositions"][1]
            changed["disposition"] = "candidate_for_carry_forward"
            fixture.source_payload["record_digest"] = _record_digest(
                fixture.source_payload
            )
            fixture._write(
                fixture.source_path.relative_to(fixture.root).as_posix(),
                json.dumps(fixture.source_payload, indent=2, sort_keys=True) + "\n",
            )

            result = fixture.run_source_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("disposition must be 'revalidation_required'", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_forward_source_revision_is_valid_but_non_authorizing(self) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--source-revision",
                str(SOURCE_REVISION),
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "portable residency source revision: awaiting maintained-fork binding\n",
        )

    def test_forward_source_revision_cannot_claim_readiness_without_binding(
        self,
    ) -> None:
        result = subprocess.run(
            [
                sys.executable,
                str(VALIDATOR),
                "--source-revision",
                str(SOURCE_REVISION),
                "--require-source-ready",
            ],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("maintained-fork binding is required", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_duplicate_json_key_is_rejected_without_a_traceback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "handoff.json"
            manifest.write_text(
                '{"schema":"portable_residency_implementation_handoff/v1",'
                '"schema":"portable_residency_implementation_handoff/v1"}\n',
                encoding="utf-8",
            )

            result = subprocess.run(
                [
                    sys.executable,
                    str(VALIDATOR),
                    "--manifest",
                    str(manifest),
                    "--phase",
                    "0",
                ],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertEqual(result.returncode, 2)
        self.assertIn("duplicate object key 'schema'", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertLessEqual(len(result.stderr), 2_100)

    def test_phase_zero_validates_real_git_identity_and_red_green_history(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory), object_format="sha256")
            fixture.build()
            source_digest = _directory_digest(fixture.root)

            result = fixture.run_validator()

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(_directory_digest(fixture.root), source_digest)
            self.assertEqual(
                result.stdout,
                "portable residency implementation handoff: phase 0 valid\n",
            )

    def test_phase_record_digest_is_content_addressed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            phase = fixture.manifest["phase_records"][0]
            phase["phase_checkpoint_tree"] = "0" * 40
            fixture.write_manifest()

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn(
            "record_digest does not match its canonical payload", result.stderr
        )
        self.assertNotIn("Traceback", result.stderr)

    def test_red_fixture_failure_signature_must_reproduce(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(recorded_failure_signature="failure-that-never-appeared")

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("did not reproduce its frozen failure signature", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_output_digest_is_bound_to_the_recorded_green_checkpoint(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            phase = fixture.manifest["phase_records"][0]
            phase["task_evidence"][1]["outputs"][0]["sha256"] = "0" * 64
            phase["record_digest"] = _record_digest(phase)
            fixture.write_manifest()

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("sha256 does not match", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_historical_phase_record_survives_an_unrelated_head_advance(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            fixture._write("unrelated.txt", "later change\n")
            fixture._commit("chore: move HEAD beyond evidence append")

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "portable residency implementation handoff: phase 0 valid\n",
        )

    def test_unrelated_ref_cannot_duplicate_the_accepted_phase_append(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            phase = fixture.manifest["phase_records"][0]
            alternate = fixture._git(
                "commit-tree",
                fixture._tree(fixture._git("rev-parse", "HEAD")),
                "-p",
                phase["phase_checkpoint_commit"],
                "-m",
                "unrelated duplicate append",
            )
            fixture._git("update-ref", "refs/heads/unrelated", alternate)

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "portable residency implementation handoff: phase 0 valid\n",
        )

    def test_historical_release_survives_the_canonical_stable_ref_moving(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            manifest_before = fixture.manifest_path.read_bytes()
            stable_tree = fixture._tree(
                fixture.manifest["stable_release"]["peeled_commit"]
            )
            next_release = fixture._git(
                "commit-tree", stable_tree, "-m", "next non-descendant stable release"
            )
            fixture._git("update-ref", "refs/heads/upstream-stable", next_release)
            fixture._git(
                "update-ref", "refs/remotes/origin/upstream-stable", next_release
            )

            result = fixture.run_validator()
            manifest_after = fixture.manifest_path.read_bytes()

        self.assertEqual(manifest_after, manifest_before)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "portable residency implementation handoff: phase 0 valid\n",
        )

    def test_diagnostic_is_globally_bounded(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            fixture.manifest["x" * 10_000] = "unknown"
            fixture.write_manifest()

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("... [truncated]", result.stderr)
        self.assertLessEqual(len(result.stderr), 2_000)
        self.assertNotIn("Traceback", result.stderr)

    def test_pre_handoff_checkpoint_must_be_an_implementation_base_ancestor(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            stable_tree = fixture._tree(
                fixture.manifest["implementation_base"]["commit"]
            )
            unrelated = fixture._git("commit-tree", stable_tree, "-m", "unrelated")
            record = fixture.manifest["pre_handoff_records"][0]
            record["checkpoint_commit"] = unrelated
            record["checkpoint_tree"] = stable_tree
            fixture.write_manifest()

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn(
            "must be an ancestor of implementation_base.commit", result.stderr
        )

    def test_bootstrap_manifest_must_be_an_exact_pre_phase_precursor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(incomplete_bootstrap_precursor=True)

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("bootstrap precursor", result.stderr)

    def test_red_fixture_metadata_must_capture_environment_and_observed_result(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(omit_reproducibility_metadata=True)

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("red fixture metadata", result.stderr)

    def test_red_patch_may_change_only_declared_test_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(add_undeclared_test_path=True)

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("patch paths must exactly match test_paths", result.stderr)

    def test_every_phase_uses_the_exact_ordered_plan_task_roster(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            task_base = fixture._git("rev-parse", "HEAD")
            task, checkpoint = fixture._task_cycle("TASK-777", task_base)
            record: dict[str, object] = {
                "phase": 1,
                "task_ids": ["TASK-777"],
                "task_evidence": [task],
                "phase_checkpoint_commit": checkpoint,
                "phase_checkpoint_tree": fixture._tree(checkpoint),
            }
            record["record_digest"] = _record_digest(record)
            fixture.manifest["phase_records"].append(record)
            fixture.write_manifest()
            fixture._commit("docs: append arbitrary Phase 1 handoff")

            result = fixture.run_validator(1)

        self.assertEqual(result.returncode, 2)
        self.assertIn("phase_records[1].task_ids must be", result.stderr)

    def test_release_url_is_bound_to_the_upstream_release_authority(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            fixture.manifest["stable_release"][
                "release_url"
            ] = "https://github.com/example/lemonade/releases/tag/v1.0.0"
            fixture.write_manifest()

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("must be the canonical upstream release URL", result.stderr)

    def test_red_patch_paths_must_stay_under_the_public_test_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(red_test_root="src/")

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("must be under the public test/ root", result.stderr)

    def test_phase_append_commit_changes_only_the_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build(add_unrelated_phase_append_path=True)

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn("must change exactly the manifest path", result.stderr)


class ResidencyImplementationHandoffV2Test(unittest.TestCase):
    @staticmethod
    def _inputs() -> tuple[GitRepository, dict[str, object], bytes]:
        repo = GitRepository(REPO_ROOT)
        metadata = json.loads(
            repo.blob(TASK_020_EVIDENCE, TASK_020_RESULT_PATH).decode()
        )
        patch = repo.blob(TASK_020_EVIDENCE, TASK_020_PATCH_PATH)
        return repo, metadata, patch

    def _validate_metadata(
        self,
        metadata: dict[str, object],
        *,
        evidence_commit: str = TASK_020_EVIDENCE,
        bundle_sha256: str = TASK_020_BUNDLE,
        task_base: str = TASK_020_BASE,
        command: list[str] | None = None,
        patch_path: str = TASK_020_PATCH_PATH,
        patch: bytes | None = None,
    ) -> RedFixtureObservation:
        repo, _, accepted_patch = self._inputs()
        return _red_fixture_metadata_v2(
            repo,
            metadata,
            evidence_commit,
            bundle_sha256,
            "TASK-020",
            task_base,
            TASK_020_COMMAND if command is None else command,
            TASK_020_FAILURE,
            patch_path,
            accepted_patch if patch is None else patch,
            list(TASK_020_FIXTURE_PATHS),
            "task_evidence[TASK-020]",
        )

    def _assert_invalid(
        self,
        metadata_mutator: Callable[[dict[str, object]], None],
        expected: str,
    ) -> None:
        _, metadata, _ = self._inputs()
        metadata_mutator(metadata)
        with self.assertRaises(HandoffError) as raised:
            self._validate_metadata(metadata)
        self.assertIn(expected, str(raised.exception))

    def test_task020_v2_binds_exact_fixture_and_binary_red_result(self) -> None:
        repo, _, patch = self._inputs()

        self.assertEqual(_sha256(patch), TASK_020_PATCH_SHA256)
        self.assertEqual(
            _patched_fixture_digests(
                repo,
                TASK_020_BASE,
                patch,
                list(TASK_020_FIXTURE_PATHS),
                "task_evidence[TASK-020]",
            ),
            TASK_020_FIXTURE_SHA256,
        )

        observed = _red_fixture_metadata(
            repo,
            TASK_020_EVIDENCE,
            TASK_020_BUNDLE,
            TASK_020_RESULT_PATH,
            "TASK-020",
            TASK_020_BASE,
            TASK_020_COMMAND,
            TASK_020_FAILURE,
            TASK_020_PATCH_PATH,
            patch,
            list(TASK_020_FIXTURE_PATHS),
            "task_evidence[TASK-020]",
        )
        replay = _run_at_commit(
            repo,
            TASK_020_BASE,
            TASK_020_COMMAND,
            patch,
            text=False,
        )

        self.assertEqual(observed.exit_code, 1)
        self.assertEqual(observed.stdout, b"")
        self.assertEqual(observed.stderr, f"{TASK_020_FAILURE}\n".encode())
        self.assertEqual(replay.returncode, observed.exit_code)
        self.assertEqual(replay.stdout, observed.stdout)
        self.assertEqual(replay.stderr, observed.stderr)

    def test_task020_v2_uses_the_published_evidence_lineage(self) -> None:
        repo = GitRepository(REPO_ROOT)

        self.assertTrue(
            repo.is_ancestor(TASK_020_EVIDENCE, repo.resolve("HEAD")),
            "TASK-020 evidence must be reachable from the published history",
        )

    def test_task020_v2_missing_patched_fixture_fails_closed(self) -> None:
        repo, _, patch = self._inputs()

        with self.assertRaises(HandoffError) as raised:
            _patched_fixture_digests(
                repo,
                TASK_020_BASE,
                patch,
                [*TASK_020_FIXTURE_PATHS, "test/residency/recovery/missing.cpp"],
                "task_evidence[TASK-020]",
            )

        self.assertIn("missing.cpp cannot be read", str(raised.exception))

    def test_git_materialization_ignores_ambient_attributes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            ambient = root / "ambient"
            destination = root / "destination"
            source.mkdir()
            ambient.mkdir()
            destination.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=source, check=True)
            subprocess.run(["git", "init", "-q"], cwd=ambient, check=True)
            (source / "fixture.cpp").write_bytes(b"frozen\n")
            subprocess.run(["git", "add", "fixture.cpp"], cwd=source, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Residency Handoff Test",
                    "-c",
                    "user.email=residency-handoff@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                cwd=source,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=source,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
            (source / ".git" / "info").mkdir(parents=True, exist_ok=True)
            (ambient / ".git" / "info").mkdir(parents=True, exist_ok=True)
            (source / ".git" / "info" / "attributes").write_text(
                "*.cpp export-ignore\n",
                encoding="utf-8",
            )
            (ambient / ".git" / "info" / "attributes").write_text(
                "*.cpp text eol=crlf\n",
                encoding="utf-8",
            )
            template = root / "template"
            (template / "info").mkdir(parents=True)
            (template / "info" / "attributes").write_text(
                "*.cpp text eol=crlf\n",
                encoding="utf-8",
            )
            global_attributes = root / "global-attributes"
            global_attributes.write_text(
                "*.cpp text eol=crlf\n",
                encoding="utf-8",
            )
            xdg = root / "xdg"
            home = root / "home"
            (xdg / "git").mkdir(parents=True)
            home.mkdir()
            (home / ".gitconfig").write_text("[broken\n", encoding="utf-8")
            config = xdg / "git" / "config"
            hooks = root / "hooks"
            hooks.mkdir()
            reference_transaction = hooks / "reference-transaction"
            reference_transaction.write_text(
                "#!/bin/sh\nexit 1\n",
                encoding="utf-8",
            )
            reference_transaction.chmod(0o755)
            subprocess.run(
                [
                    "git",
                    "config",
                    "--file",
                    str(config),
                    "init.templateDir",
                    str(template),
                ],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "config",
                    "--file",
                    str(config),
                    "core.hooksPath",
                    str(hooks),
                ],
                check=True,
            )
            subprocess.run(
                [
                    "git",
                    "config",
                    "--file",
                    str(config),
                    "core.attributesFile",
                    str(global_attributes),
                ],
                check=True,
            )

            with mock.patch.dict(
                os.environ,
                {
                    "GIT_DIR": str(ambient / ".git"),
                    "HOME": str(home),
                    "XDG_CONFIG_HOME": str(xdg),
                },
            ):
                GitRepository(source).materialize("HEAD", destination)

            self.assertEqual(
                (destination / "fixture.cpp").read_bytes(),
                b"frozen\n",
            )
            self.assertFalse((destination / ".git" / "info" / "attributes").exists())
            self.assertEqual(
                subprocess.run(
                    ["git", "rev-parse", "HEAD"],
                    cwd=destination,
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.strip(),
                commit,
            )

    def test_git_materialization_rejects_committed_export_subst(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "destination"
            source.mkdir()
            destination.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=source, check=True)
            (source / ".gitattributes").write_text(
                "fixture.txt export-subst\n",
                encoding="utf-8",
            )
            (source / "fixture.txt").write_text(
                "$Format:%H$\n",
                encoding="utf-8",
            )
            subprocess.run(["git", "add", "."], cwd=source, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Residency Handoff Test",
                    "-c",
                    "user.email=residency-handoff@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                cwd=source,
                check=True,
            )

            with self.assertRaises(HandoffError) as raised:
                GitRepository(source).materialize("HEAD", destination)

            self.assertIn("export-subst", str(raised.exception))
            self.assertIn("fixture.txt", str(raised.exception))
            self.assertEqual(list(destination.iterdir()), [])

    def test_git_materialization_rejects_committed_export_ignore(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "destination"
            source.mkdir()
            destination.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=source, check=True)
            (source / ".gitattributes").write_text(
                "omitted.txt export-ignore\n",
                encoding="utf-8",
            )
            (source / "omitted.txt").write_text("frozen\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=source, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Residency Handoff Test",
                    "-c",
                    "user.email=residency-handoff@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                cwd=source,
                check=True,
            )

            with self.assertRaises(HandoffError) as raised:
                GitRepository(source).materialize("HEAD", destination)

            self.assertIn("export-ignore", str(raised.exception))
            self.assertIn("omitted.txt", str(raised.exception))
            self.assertEqual(list(destination.iterdir()), [])

    def test_git_repository_queries_ignore_ambient_global_config(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            home = root / "home"
            xdg = root / "xdg"
            source.mkdir()
            home.mkdir()
            xdg.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=source, check=True)
            (source / "fixture.txt").write_text("fixture\n", encoding="utf-8")
            subprocess.run(["git", "add", "fixture.txt"], cwd=source, check=True)
            subprocess.run(
                [
                    "git",
                    "-c",
                    "user.name=Residency Handoff Test",
                    "-c",
                    "user.email=residency-handoff@example.invalid",
                    "commit",
                    "-qm",
                    "fixture",
                ],
                cwd=source,
                check=True,
            )
            commit = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=source,
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
            (home / ".gitconfig").write_text("[broken\n", encoding="utf-8")

            with mock.patch.dict(
                os.environ,
                {"HOME": str(home), "XDG_CONFIG_HOME": str(xdg)},
            ):
                repo = GitRepository.discover(source)
                self.assertEqual(repo.resolve("HEAD"), commit)
                self.assertTrue(repo.is_ancestor(commit, commit))

    def test_git_repository_ignores_replace_refs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "destination"
            source.mkdir()
            destination.mkdir()
            fixture_path = source / "fixture.txt"

            def git(*arguments: str, cwd: Path = source) -> str:
                return subprocess.run(
                    ["git", *arguments],
                    cwd=cwd,
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.strip()

            def commit(contents: str, message: str) -> str:
                fixture_path.write_text(contents, encoding="utf-8")
                git("add", "fixture.txt")
                git("commit", "-qm", message)
                return git("rev-parse", "HEAD")

            git("init", "-q")
            git("config", "user.name", "Residency Handoff Test")
            git("config", "user.email", "residency-handoff@example.invalid")
            original_parent = commit("original parent\n", "original parent")
            original = commit("original\n", "original")

            git("checkout", "-q", "--orphan", "replacement")
            git("rm", "-q", "-rf", ".")
            replacement_parent = commit("replacement parent\n", "replacement parent")
            replacement = commit("replacement\n", "replacement")
            git("replace", original, replacement)

            self.assertEqual(git("show", f"{original}:fixture.txt"), "replacement")
            replaced_history = git("rev-list", "--parents", "-n", "1", original).split()
            self.assertEqual(replaced_history, [original, replacement_parent])

            repo = GitRepository(source)
            self.assertEqual(repo.blob(original, "fixture.txt"), b"original\n")
            self.assertEqual(repo.parents(original), [original_parent])
            repo.materialize(original, destination)
            self.assertEqual((destination / "fixture.txt").read_bytes(), b"original\n")
            self.assertEqual(git("rev-parse", "HEAD", cwd=destination), original)
            self.assertEqual(
                git(
                    "rev-list", "--parents", "-n", "1", original, cwd=destination
                ).split(),
                [original, original_parent],
            )

    def test_patch_inspection_ignores_ambient_global_config(self) -> None:
        patch = b"""diff --git a/fixture.txt b/fixture.txt
new file mode 100644
--- /dev/null
+++ b/fixture.txt
@@ -0,0 +1 @@
+fixture
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            home = root / "home"
            xdg = root / "xdg"
            home.mkdir()
            xdg.mkdir()
            (home / ".gitconfig").write_text("[broken\n", encoding="utf-8")

            with mock.patch.dict(
                os.environ,
                {"HOME": str(home), "XDG_CONFIG_HOME": str(xdg)},
            ):
                self.assertEqual(_patch_paths(patch, "fixture patch"), ["fixture.txt"])

    def test_task020_patch_application_preserves_checkout_control_path(
        self,
    ) -> None:
        patch = b"""diff --git a/fixture.txt b/fixture.txt
--- a/fixture.txt
+++ b/fixture.txt
@@ -1 +1 @@
-old
+new
"""
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            control = checkout / ".handoff-red.patch"
            fixture = checkout / "fixture.txt"
            control.write_bytes(b"checkout-owned\n")
            fixture.write_bytes(b"old\n")

            _apply_patch(checkout, patch, "fixture patch does not apply")

            self.assertEqual(control.read_bytes(), b"checkout-owned\n")
            self.assertEqual(fixture.read_bytes(), b"new\n")

    def test_task020_patch_application_ignores_ambient_line_endings(self) -> None:
        patch = b"""diff --git a/fixture.cpp b/fixture.cpp
new file mode 100644
--- /dev/null
+++ b/fixture.cpp
@@ -0,0 +1 @@
+frozen
"""
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory) / "checkout"
            ambient = Path(directory) / "ambient"
            checkout.mkdir()
            ambient.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=checkout, check=True)
            subprocess.run(["git", "init", "-q"], cwd=ambient, check=True)
            (checkout / ".gitattributes").write_text(
                "*.cpp text eol=crlf\n",
                encoding="utf-8",
            )
            subprocess.run(
                ["git", "config", "core.autocrlf", "true"],
                cwd=checkout,
                check=True,
            )
            attributes = checkout / "local-attributes"
            attributes.write_text("*.cpp text eol=crlf\n", encoding="utf-8")
            subprocess.run(
                [
                    "git",
                    "config",
                    "core.attributesFile",
                    str(attributes),
                ],
                cwd=checkout,
                check=True,
            )
            (ambient / ".git" / "info").mkdir(parents=True, exist_ok=True)
            (ambient / ".git" / "info" / "attributes").write_text(
                "*.cpp text eol=crlf\n",
                encoding="utf-8",
            )

            with mock.patch.dict(
                os.environ,
                {"GIT_DIR": str(ambient / ".git")},
            ):
                _apply_patch(checkout, patch, "fixture patch does not apply")

            self.assertEqual((checkout / "fixture.cpp").read_bytes(), b"frozen\n")

    def test_task020_v2_is_closed_and_content_addressed(self) -> None:
        cases: tuple[tuple[Callable[[dict[str, object]], None], str], ...] = (
            (
                lambda metadata: metadata.__setitem__("unknown", True),
                "keys differ",
            ),
            (
                lambda metadata: metadata.__setitem__(
                    "schema", "portable_residency_red_fixture_result/v1"
                ),
                "schema is unsupported",
            ),
            (
                lambda metadata: metadata.__setitem__(
                    "fallback_state", "legacy_cutover_active"
                ),
                "fallback_state must be 'production_cutover_inactive'",
            ),
            (
                lambda metadata: metadata.__setitem__("task_base_commit", "0" * 40),
                "identity does not match the task",
            ),
            (
                lambda metadata: metadata["patch"].__setitem__(
                    "path", "plan/evidence/red-fixtures/TASK-020/other.patch"
                ),
                "patch identity does not match the record",
            ),
            (
                lambda metadata: metadata["fixture_sha256"].__setitem__(
                    TASK_020_FIXTURE_PATHS[0], "0" * 64
                ),
                "fixture_sha256 does not match patched bytes",
            ),
            (
                lambda metadata: metadata["observed_result"]["stderr"].update(
                    {
                        "hex": "00" * len((TASK_020_FAILURE + "\n").encode()),
                        "sha256": _sha256(
                            b"\0" * len((TASK_020_FAILURE + "\n").encode())
                        ),
                    }
                ),
                "streams differ from the accepted TASK-020 RED",
            ),
            (
                lambda metadata: metadata["observed_result"].__setitem__(
                    "exit_code", True
                ),
                "exit_code must be 1",
            ),
            (
                lambda metadata: metadata["observed_result"].__setitem__(
                    "exit_code", 1.0
                ),
                "exit_code must be 1",
            ),
        )
        for mutator, expected in cases:
            with self.subTest(expected=expected):
                self._assert_invalid(mutator, expected)

    def test_task020_v2_command_requires_python_no_bytecode_mode(self) -> None:
        command = [
            "python3",
            "test/residency/recovery/test_durable_journal_public_seam.py",
        ]

        _, metadata, _ = self._inputs()
        metadata["command"] = command
        with self.assertRaises(HandoffError) as raised:
            self._validate_metadata(metadata, command=command)

        self.assertIn(
            "must use the exact accepted TASK-020 command",
            str(raised.exception),
        )

    def test_task020_v2_rejects_rebound_record_identities(self) -> None:
        _, metadata, patch = self._inputs()
        cases = (
            (
                {"evidence_commit": "0" * 40},
                "evidence_commit is not the frozen TASK-020 RED",
            ),
            (
                {"bundle_sha256": "0" * 64},
                "bundle_sha256 is not the frozen TASK-020 bundle",
            ),
            (
                {"task_base": "0" * 40},
                "task_base_commit is not the frozen TASK-020 base",
            ),
            (
                {"patch_path": "plan/evidence/red-fixtures/TASK-020/other.patch"},
                "patch is not the frozen TASK-020 patch",
            ),
            (
                {"patch": patch + b"\n"},
                "patch is not the frozen TASK-020 patch",
            ),
        )
        for overrides, expected in cases:
            with self.subTest(expected=expected):
                with self.assertRaises(HandoffError) as raised:
                    self._validate_metadata(metadata, **overrides)
                self.assertIn(expected, str(raised.exception))


if __name__ == "__main__":
    unittest.main()
