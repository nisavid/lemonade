"""Public CLI tests for the portable residency implementation handoff."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = REPO_ROOT / "tools" / "validate_residency_implementation_handoff.py"
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
        for task_id in ("TASK-093", "TASK-006", "TASK-007"):
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
            "task_ids": ["TASK-093", "TASK-006", "TASK-007"],
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


class ResidencyImplementationHandoffCliTest(unittest.TestCase):
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

    def test_phase_record_must_be_an_append_only_evidence_commit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = HandoffRepository(Path(directory))
            fixture.build()
            fixture._write("unrelated.txt", "later change\n")
            fixture._commit("chore: move HEAD beyond evidence append")

            result = fixture.run_validator()

        self.assertEqual(result.returncode, 2)
        self.assertIn(
            "must be committed directly after its phase checkpoint", result.stderr
        )
        self.assertNotIn("Traceback", result.stderr)

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


if __name__ == "__main__":
    unittest.main()
