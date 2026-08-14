"""Shared public-CLI fixture for residency capability inventory tests."""

from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from collections.abc import Callable
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = REPO_ROOT / "tools" / "validate_residency_capability_inventory.py"
VALIDATOR_PACKAGE = REPO_ROOT / "tools" / "residency_inventory"
INVENTORY = (
    REPO_ROOT / "docs" / "research" / "portable-residency-capability-inventory.json"
)
MATRIX_BEGIN = "<!-- BEGIN GENERATED SUPPORT INVENTORY -->"
MATRIX_END = "<!-- END GENERATED SUPPORT INVENTORY -->"
CAMPAIGN_BEGIN = "<!-- BEGIN GENERATED HATCHERY EXACT CELLS -->"
CAMPAIGN_END = "<!-- END GENERATED HATCHERY EXACT CELLS -->"


class ResidencyCapabilityInventoryCliCase(unittest.TestCase):
    """Exercise only the validator's command-line contract."""

    def setUp(self) -> None:
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary_directory.cleanup)
        self.repo = Path(self._temporary_directory.name) / "repository"
        self.repo.mkdir()
        self._run_git("init", "--quiet")
        self._install_local_object_alternate()

        tools = self.repo / "tools"
        tools.mkdir()
        shutil.copy2(VALIDATOR, tools / VALIDATOR.name)
        if VALIDATOR_PACKAGE.exists():
            shutil.copytree(VALIDATOR_PACKAGE, tools / VALIDATOR_PACKAGE.name)

        research = self.repo / "docs" / "research"
        research.mkdir(parents=True)
        self.inventory_path = research / INVENTORY.name
        self.matrix_path = research / "portable-residency-capability-matrix.md"
        self.campaign_path = research / "hatchery-campaign-parameters.md"
        shutil.copy2(
            REPO_ROOT
            / "docs"
            / "research"
            / "hatchery-residency-validation-profile.md",
            research / "hatchery-residency-validation-profile.md",
        )
        shutil.copy2(
            REPO_ROOT / "docs" / "research" / "profile-free-residency-estimation.md",
            research / "profile-free-residency-estimation.md",
        )
        shutil.copy2(
            REPO_ROOT / "docs" / "research" / "hatchery-campaign-parameters.md",
            self.campaign_path,
        )
        with INVENTORY.open(encoding="utf-8") as stream:
            self.inventory = json.load(stream)
        self._write_inventory()
        self._write_fresh_documents()

    def _run_git(
        self,
        *arguments: str,
        input_text: str | None = None,
        env: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", *arguments],
            cwd=self.repo,
            check=True,
            capture_output=True,
            text=True,
            input=input_text,
            env=env,
        )

    def _install_local_object_alternate(self) -> None:
        source_objects_text = subprocess.run(
            ["git", "rev-parse", "--git-path", "objects"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        source_objects = Path(source_objects_text)
        if not source_objects.is_absolute():
            source_objects = REPO_ROOT / source_objects
        source_objects = source_objects.resolve()
        alternates = self.repo / ".git" / "objects" / "info" / "alternates"
        alternates.write_text(f"{source_objects}\n", encoding="utf-8")

    def _run_cli(
        self, *arguments: str, env: dict[str, str] | None = None
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            self._cli_argv(*arguments),
            cwd=self.repo,
            check=False,
            capture_output=True,
            text=True,
            env=(os.environ | env) if env is not None else None,
        )

    def _start_cli_process(
        self, *arguments: str, env: dict[str, str] | None = None
    ) -> subprocess.Popen[str]:
        process = subprocess.Popen(
            self._cli_argv(*arguments),
            cwd=self.repo,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=(os.environ | env) if env is not None else None,
        )
        self.addCleanup(self._cleanup_cli_process, process)
        return process

    @staticmethod
    def _cleanup_cli_process(process: subprocess.Popen[str]) -> None:
        if process.poll() is None:
            process.kill()
        try:
            process.wait(timeout=5)
        finally:
            if process.stdout is not None:
                process.stdout.close()
            if process.stderr is not None:
                process.stderr.close()

    def _cli_argv(self, *arguments: str) -> list[str]:
        return [
            sys.executable,
            str(self.repo / "tools" / VALIDATOR.name),
            *arguments,
        ]

    def _sitecustomize_env(self, source: str) -> dict[str, str]:
        injection_directory = self.repo / "test-injection"
        injection_directory.mkdir(exist_ok=True)
        (injection_directory / "sitecustomize.py").write_text(source, encoding="utf-8")
        return {"PYTHONPATH": str(injection_directory)}

    def _write_inventory(self) -> None:
        self.inventory_path.write_text(
            json.dumps(self.inventory, indent=2) + "\n", encoding="utf-8"
        )

    def _write_fresh_documents(self) -> None:
        result = self._run_cli("--render")
        self.assertEqual(result.returncode, 0, result.stderr)
        matrix, campaign = result.stdout.split(f"\n{CAMPAIGN_BEGIN}", maxsplit=1)
        self.matrix_path.write_text(
            f"matrix preface\n{matrix.rstrip()}\nmatrix epilogue\n",
            encoding="utf-8",
        )
        self.campaign_path.write_text(
            self._replace_marked_block(
                self.campaign_path.read_text(encoding="utf-8"),
                f"{CAMPAIGN_BEGIN}{campaign.rstrip()}",
                CAMPAIGN_BEGIN,
                CAMPAIGN_END,
            ),
            encoding="utf-8",
        )

    @staticmethod
    def _replace_marked_block(
        document: str, rendered: str, begin_marker: str, end_marker: str
    ) -> str:
        start = document.index(begin_marker)
        end = document.index(end_marker) + len(end_marker)
        return document[:start] + rendered + document[end:]

    def _replace_inventory(self, mutator: Callable[[dict[str, object]], None]) -> None:
        inventory = copy.deepcopy(self.inventory)
        mutator(inventory)
        self.inventory = inventory
        self._write_inventory()

    def _commit_source_change(
        self, source_path: str, transform: Callable[[str], str]
    ) -> str:
        baseline = self.inventory["source_support_baseline"]
        source = self._run_git("show", f"{baseline}:{source_path}").stdout
        changed = transform(source)
        blob = self._run_git(
            "hash-object", "-w", "--stdin", input_text=changed
        ).stdout.strip()
        self._run_git("read-tree", str(baseline))
        self._run_git(
            "update-index", "--add", "--cacheinfo", f"100644,{blob},{source_path}"
        )
        tree = self._run_git("write-tree").stdout.strip()
        commit_env = os.environ | {
            "GIT_AUTHOR_NAME": "Residency inventory test",
            "GIT_AUTHOR_EMAIL": "inventory-test@example.invalid",
            "GIT_AUTHOR_DATE": "2000-01-01T00:00:00+00:00",
            "GIT_COMMITTER_NAME": "Residency inventory test",
            "GIT_COMMITTER_EMAIL": "inventory-test@example.invalid",
            "GIT_COMMITTER_DATE": "2000-01-01T00:00:00+00:00",
        }
        commit = self._run_git(
            "commit-tree",
            tree,
            "-p",
            str(baseline),
            input_text="validator source fixture\n",
            env=commit_env,
        ).stdout.strip()
        self.inventory["source_support_baseline"] = commit
        source_blobs = self.inventory["source_file_blobs"]
        for path in source_blobs:
            source_blobs[path] = self._run_git(
                "rev-parse", f"{commit}:{path}"
            ).stdout.strip()
        source_trees = self.inventory.get("source_tree_objects", {})
        for path in source_trees:
            source_trees[path] = self._run_git(
                "rev-parse", f"{commit}:{path}"
            ).stdout.strip()
        self._write_inventory()
        return commit

    def assert_invalid(
        self, result: subprocess.CompletedProcess[str], expected: str
    ) -> None:
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(
            "portable residency capability inventory: invalid:", result.stderr
        )
        self.assertIn(expected, result.stderr)
        self.assertNotIn("Traceback", result.stderr)
