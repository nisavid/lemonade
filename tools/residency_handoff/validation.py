"""Git-backed validation for the portable residency implementation handoff."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tarfile
import tempfile
from collections.abc import Iterable
from io import BytesIO
from pathlib import Path, PurePosixPath
from typing import Any

from .contract import canonical_digest, fail, load_manifest, sha256_bytes

SCHEMA = "portable_residency_implementation_handoff/v1"
HEX_DIGEST = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
TASK_ID = re.compile(r"^TASK-[0-9]{3}$")
ALLOWED_COMMANDS = {"cmake", "ctest", "python3"}
LATER_ROSTER_PATH = "docs/research/portable-residency-capability-inventory.json"
EXPECTED_PHASE_TASKS = {
    0: ("TASK-093", "TASK-006", "TASK-007"),
    1: ("TASK-008", "TASK-009", "TASK-010", "TASK-011", "TASK-012", "TASK-013"),
    2: ("TASK-014", "TASK-015", "TASK-016", "TASK-017", "TASK-018"),
    3: (
        "TASK-019",
        "TASK-020",
        "TASK-021",
        "TASK-094",
        "TASK-095",
        "TASK-022",
        "TASK-023",
        "TASK-024",
        "TASK-025",
    ),
    4: tuple(f"TASK-{number:03d}" for number in range(26, 43)),
    5: (
        "TASK-043",
        "TASK-044",
        "TASK-045",
        "TASK-046",
        "TASK-047",
        "TASK-048",
        "TASK-049",
        "TASK-099",
        "TASK-100",
        "TASK-050",
        "TASK-051",
        "TASK-052",
        "TASK-096",
        "TASK-097",
        "TASK-098",
        "TASK-053",
        "TASK-054",
        "TASK-055",
        "TASK-056",
        "TASK-057",
        "TASK-058",
        "TASK-059",
        "TASK-060",
        "TASK-061",
        "TASK-101",
        "TASK-102",
        "TASK-103",
    ),
    6: (
        "TASK-062",
        "TASK-063",
        "TASK-064",
        "TASK-065",
        "TASK-066",
        "TASK-069",
        "TASK-070",
        "TASK-067",
        "TASK-068",
        "TASK-071",
    ),
    7: (
        "TASK-072",
        "TASK-073",
        "TASK-105",
        "TASK-106",
        "TASK-074",
        "TASK-075",
    ),
    8: (
        "TASK-076",
        "TASK-077",
        "TASK-078",
        "TASK-104",
        "TASK-079",
        "TASK-080",
    ),
    9: ("TASK-081", "TASK-082"),
    10: (
        "TASK-083",
        "TASK-084",
        "TASK-085",
        "TASK-086",
        "TASK-087",
        "TASK-107",
        "TASK-092",
    ),
    11: ("TASK-088", "TASK-089", "TASK-090", "TASK-091"),
}
REQUIRED_SEAMS = {
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
}


def _without_git_environment() -> dict[str, str]:
    return {
        key: value for key, value in os.environ.items() if not key.startswith("GIT_")
    }


class GitRepository:
    def __init__(self, root: Path):
        self.root = root.resolve()

    @classmethod
    def discover(cls, start: Path) -> GitRepository:
        result = subprocess.run(
            ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode:
            fail(f"cannot locate Git repository: {result.stderr.strip()}")
        return cls(Path(result.stdout.strip()))

    def run(self, *args: str, text: bool = True) -> str | bytes:
        result = subprocess.run(
            ["git", "-C", str(self.root), *args],
            capture_output=True,
            text=text,
            check=False,
        )
        if result.returncode:
            stderr = result.stderr if text else result.stderr.decode("utf-8", "replace")
            fail(f"git {' '.join(args)} failed: {stderr.strip()}")
        return result.stdout

    def resolve(self, expression: str) -> str:
        return str(self.run("rev-parse", "--verify", expression)).strip()

    def object_type(self, object_id: str) -> str:
        return str(self.run("cat-file", "-t", object_id)).strip()

    def blob(self, commit: str, path: str) -> bytes:
        return bytes(self.run("show", f"{commit}:{path}", text=False))

    def parents(self, commit: str) -> list[str]:
        line = str(self.run("rev-list", "--parents", "-n", "1", commit)).strip()
        return line.split()[1:]

    def is_ancestor(self, ancestor: str, descendant: str) -> bool:
        result = subprocess.run(
            [
                "git",
                "-C",
                str(self.root),
                "merge-base",
                "--is-ancestor",
                ancestor,
                descendant,
            ],
            capture_output=True,
            check=False,
        )
        return result.returncode == 0

    def changed_paths(self, parent: str, commit: str) -> list[str]:
        output = str(
            self.run("diff-tree", "--no-commit-id", "--name-only", "-r", parent, commit)
        )
        return [line for line in output.splitlines() if line]

    def materialize(self, commit: str, destination: Path) -> None:
        archive = bytes(self.run("archive", "--format=tar", commit, text=False))
        with tarfile.open(fileobj=BytesIO(archive), mode="r:") as source:
            for member in source.getmembers():
                path = PurePosixPath(member.name)
                if (
                    path.is_absolute()
                    or ".." in path.parts
                    or member.issym()
                    or member.islnk()
                ):
                    fail(f"unsafe path in Git archive: {member.name!r}")
            source.extractall(destination, filter="data")

        environment = _without_git_environment()
        object_format = str(self.run("rev-parse", "--show-object-format")).strip()
        if object_format not in {"sha1", "sha256"}:
            fail(f"unsupported source repository object format {object_format!r}")
        object_path = Path(str(self.run("rev-parse", "--git-path", "objects")).strip())
        if not object_path.is_absolute():
            object_path = self.root / object_path
        initialized = subprocess.run(
            ["git", "init", "-q", f"--object-format={object_format}"],
            cwd=destination,
            capture_output=True,
            text=True,
            check=False,
            env=environment,
        )
        if initialized.returncode:
            fail(f"cannot initialize replay repository: {initialized.stderr.strip()}")
        alternates = destination / ".git" / "objects" / "info" / "alternates"
        alternates.write_text(f"{object_path.resolve()}\n", encoding="utf-8")
        for arguments in (
            ("update-ref", "--no-deref", "HEAD", commit),
            ("read-tree", commit),
        ):
            prepared = subprocess.run(
                ["git", *arguments],
                cwd=destination,
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )
            if prepared.returncode:
                fail("cannot prepare replay repository: " f"{prepared.stderr.strip()}")


def _mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def _list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        fail(f"{label} must be an array")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{label} must be a non-empty string")
    if "\x00" in value:
        fail(f"{label} contains a NUL byte")
    return value


def _exact_keys(value: dict[str, Any], expected: Iterable[str], label: str) -> None:
    expected_set = set(expected)
    actual = set(value)
    missing = sorted(expected_set - actual)
    unknown = sorted(actual - expected_set)
    if missing or unknown:
        fail(f"{label} keys differ: missing={missing}, unknown={unknown}")


def _digest(value: Any, label: str) -> str:
    digest = _string(value, label)
    if not HEX_DIGEST.fullmatch(digest):
        fail(f"{label} must be a lowercase SHA-256 digest")
    return digest


def _object_id(value: Any, label: str) -> str:
    object_id = _string(value, label)
    if not GIT_OBJECT.fullmatch(object_id):
        fail(f"{label} must be a full lowercase Git object ID")
    return object_id


def _path(value: Any, label: str) -> str:
    path = _string(value, label)
    parsed = PurePosixPath(path)
    if (
        parsed.is_absolute()
        or ".." in parsed.parts
        or "\\" in path
        or path in {".", ""}
    ):
        fail(f"{label} must be a normalized repository-relative path")
    if parsed.as_posix() != path:
        fail(f"{label} must be a normalized repository-relative path")
    return path


def _commit(repo: GitRepository, value: Any, label: str) -> str:
    commit = _object_id(value, label)
    if repo.object_type(commit) != "commit":
        fail(f"{label} does not identify a commit")
    return commit


def _tree(repo: GitRepository, commit: str, value: Any, label: str) -> str:
    tree = _object_id(value, label)
    actual = repo.resolve(f"{commit}^{{tree}}")
    if tree != actual:
        fail(f"{label} does not match {commit} tree {actual}")
    return tree


def _fallback(value: Any, label: str) -> None:
    fallback = _mapping(value, label)
    _exact_keys(fallback, {"authority", "status"}, label)
    _string(fallback["authority"], f"{label}.authority")
    if fallback["status"] != "active":
        fail(f"{label}.status must be 'active'")


def _command(value: Any, label: str) -> list[str]:
    command = _list(value, label)
    if not command:
        fail(f"{label} must not be empty")
    parts = [_string(part, f"{label}[{index}]") for index, part in enumerate(command)]
    executable = PurePosixPath(parts[0]).name
    if executable not in ALLOWED_COMMANDS:
        fail(f"{label} executable {parts[0]!r} is not allowed")
    if executable == "python3" and "-c" in parts[1:]:
        fail(f"{label} may not execute inline Python")
    for index, part in enumerate(parts):
        if os.path.isabs(part) or "\x00" in part or "\n" in part or "\r" in part:
            fail(f"{label}[{index}] is unsafe")
    return parts


def _outputs(
    repo: GitRepository, value: Any, checkpoint: str, label: str
) -> list[dict[str, Any]]:
    outputs = _list(value, label)
    if not outputs:
        fail(f"{label} must not be empty")
    seen: set[str] = set()
    result: list[dict[str, Any]] = []
    for index, raw in enumerate(outputs):
        item_label = f"{label}[{index}]"
        item = _mapping(raw, item_label)
        _exact_keys(item, {"path", "sha256"}, item_label)
        path = _path(item["path"], f"{item_label}.path")
        digest = _digest(item["sha256"], f"{item_label}.sha256")
        if path in seen:
            fail(f"{label} repeats output path {path!r}")
        seen.add(path)
        actual = sha256_bytes(repo.blob(checkpoint, path))
        if actual != digest:
            fail(f"{item_label}.sha256 does not match {checkpoint}:{path}")
        result.append(item)
    return result


def _validation_results(value: Any, label: str) -> None:
    results = _list(value, label)
    if not results:
        fail(f"{label} must not be empty")
    for index, raw in enumerate(results):
        item_label = f"{label}[{index}]"
        item = _mapping(raw, item_label)
        _exact_keys(item, {"command", "result"}, item_label)
        _command(item["command"], f"{item_label}.command")
        if item["result"] != "pass":
            fail(f"{item_label}.result must be 'pass'")


def _validate_release(repo: GitRepository, value: Any) -> str:
    release = _mapping(value, "stable_release")
    _exact_keys(
        release,
        {
            "tag",
            "release_url",
            "tag_object",
            "peeled_commit",
            "local_ref",
            "local_ref_commit",
            "origin_ref",
            "origin_ref_commit",
        },
        "stable_release",
    )
    tag = _string(release["tag"], "stable_release.tag")
    url = _string(release["release_url"], "stable_release.release_url")
    expected_url = f"https://github.com/lemonade-sdk/lemonade/releases/tag/{tag}"
    if url != expected_url:
        fail("stable_release.release_url must be the canonical upstream release URL")
    peeled = _commit(repo, release["peeled_commit"], "stable_release.peeled_commit")
    ref = f"refs/tags/{tag}"
    ref_value = repo.resolve(ref)
    tag_object = release["tag_object"]
    if tag_object is None:
        if repo.object_type(ref_value) != "commit" or ref_value != peeled:
            fail("stable_release lightweight tag identity is inconsistent")
    else:
        annotated = _object_id(tag_object, "stable_release.tag_object")
        if repo.object_type(annotated) != "tag" or annotated != ref_value:
            fail("stable_release.tag_object does not match the annotated tag")
        if repo.resolve(f"{ref}^{{commit}}") != peeled:
            fail("stable_release annotated tag does not peel to peeled_commit")
    for prefix in ("local", "origin"):
        ref_name = _string(release[f"{prefix}_ref"], f"stable_release.{prefix}_ref")
        expected_ref = (
            "refs/heads/upstream-stable"
            if prefix == "local"
            else "refs/remotes/origin/upstream-stable"
        )
        if ref_name != expected_ref:
            fail(f"stable_release.{prefix}_ref must be {expected_ref!r}")
        recorded = _commit(
            repo,
            release[f"{prefix}_ref_commit"],
            f"stable_release.{prefix}_ref_commit",
        )
        if repo.resolve(ref_name) != recorded or recorded != peeled:
            fail(f"stable_release.{prefix}_ref does not resolve to peeled_commit")
    return peeled


def _validate_fixed_identity(repo: GitRepository, manifest: dict[str, Any]) -> str:
    research = _mapping(manifest["research_closure"], "research_closure")
    _exact_keys(research, {"baseline_commit", "baseline_tree"}, "research_closure")
    research_commit = _commit(
        repo, research["baseline_commit"], "research_closure.baseline_commit"
    )
    _tree(
        repo,
        research_commit,
        research["baseline_tree"],
        "research_closure.baseline_tree",
    )
    stable_commit = _validate_release(repo, manifest["stable_release"])
    implementation = _mapping(manifest["implementation_base"], "implementation_base")
    _exact_keys(implementation, {"commit", "tree"}, "implementation_base")
    base_commit = _commit(repo, implementation["commit"], "implementation_base.commit")
    _tree(repo, base_commit, implementation["tree"], "implementation_base.tree")
    if not repo.is_ancestor(stable_commit, base_commit):
        fail("stable release commit is not an ancestor of implementation_base.commit")
    return base_commit


def _validate_scouts(repo: GitRepository, value: Any, base_commit: str) -> None:
    scouts = _list(value, "scout_runs")
    if not scouts:
        fail("scout_runs must not be empty")
    ids: set[str] = set()
    for index, raw in enumerate(scouts):
        label = f"scout_runs[{index}]"
        scout = _mapping(raw, label)
        _exact_keys(scout, {"id", "commit", "tree", "inputs", "outputs"}, label)
        scout_id = _string(scout["id"], f"{label}.id")
        if scout_id in ids:
            fail(f"scout_runs repeats id {scout_id!r}")
        ids.add(scout_id)
        commit = _commit(repo, scout["commit"], f"{label}.commit")
        _tree(repo, commit, scout["tree"], f"{label}.tree")
        if not repo.is_ancestor(base_commit, commit):
            fail(f"{label}.commit does not descend from implementation base")
        _outputs(repo, scout["inputs"], commit, f"{label}.inputs")
        _outputs(repo, scout["outputs"], commit, f"{label}.outputs")


def _validate_source_closure(
    repo: GitRepository, manifest: dict[str, Any], base: str
) -> None:
    inventory = _list(manifest["legacy_inventory"], "legacy_inventory")
    if not inventory:
        fail("legacy_inventory must not be empty")
    inventory_keys: set[tuple[str, str]] = set()
    for index, raw in enumerate(inventory):
        label = f"legacy_inventory[{index}]"
        item = _mapping(raw, label)
        _exact_keys(item, {"path", "symbol", "blob_sha256"}, label)
        path = _path(item["path"], f"{label}.path")
        symbol = _string(item["symbol"], f"{label}.symbol")
        blob = repo.blob(base, path)
        if sha256_bytes(blob) != _digest(item["blob_sha256"], f"{label}.blob_sha256"):
            fail(f"{label}.blob_sha256 does not match implementation base")
        if symbol.encode("utf-8") not in blob:
            fail(f"{label}.symbol is absent from {path}")
        key = (path, symbol)
        if key in inventory_keys:
            fail(f"legacy_inventory repeats {path}:{symbol}")
        inventory_keys.add(key)

    dispositions = _list(
        manifest["source_seam_dispositions"], "source_seam_dispositions"
    )
    seam_ids: set[str] = set()
    disposition_inventory_keys: set[tuple[str, str]] = set()
    allowed = {"adapted", "deferred", "replaced", "research_only", "retained", "reused"}
    for index, raw in enumerate(dispositions):
        label = f"source_seam_dispositions[{index}]"
        item = _mapping(raw, label)
        _exact_keys(item, {"id", "disposition", "path", "symbol"}, label)
        seam_id = _string(item["id"], f"{label}.id")
        if seam_id in seam_ids:
            fail(f"source_seam_dispositions repeats id {seam_id!r}")
        seam_ids.add(seam_id)
        if item["disposition"] not in allowed:
            fail(f"{label}.disposition is unsupported")
        path = _path(item["path"], f"{label}.path")
        symbol = _string(item["symbol"], f"{label}.symbol")
        if symbol.encode("utf-8") not in repo.blob(base, path):
            fail(f"{label}.symbol is absent from {path}")
        disposition_inventory_keys.add((path, symbol))
    if seam_ids != REQUIRED_SEAMS:
        fail(
            "source_seam_dispositions does not close the required seams: "
            f"missing={sorted(REQUIRED_SEAMS - seam_ids)}, "
            f"unknown={sorted(seam_ids - REQUIRED_SEAMS)}"
        )
    missing_inventory = sorted(disposition_inventory_keys - inventory_keys)
    if missing_inventory:
        fail("legacy_inventory omits source-seam callsites: " f"{missing_inventory}")


def _validate_issues(repo: GitRepository, value: Any) -> None:
    issues = _list(value, "later_cell_issues")
    if not issues:
        fail("later_cell_issues must not be empty")
    ids: set[int] = set()
    units: set[str] = set()
    for index, raw in enumerate(issues):
        label = f"later_cell_issues[{index}]"
        issue = _mapping(raw, label)
        _exact_keys(issue, {"promotion_unit_id", "issue_id", "url"}, label)
        unit = _string(issue["promotion_unit_id"], f"{label}.promotion_unit_id")
        issue_id = issue["issue_id"]
        if isinstance(issue_id, bool) or not isinstance(issue_id, int) or issue_id <= 0:
            fail(f"{label}.issue_id must be a positive integer")
        expected = f"https://github.com/nisavid/lemonade/issues/{issue_id}"
        if issue["url"] != expected:
            fail(f"{label}.url must be {expected}")
        if issue_id in ids or unit in units:
            fail("later_cell_issues contains a duplicate issue or promotion unit")
        ids.add(issue_id)
        units.add(unit)
    try:
        roster_document = json.loads(
            repo.blob(repo.resolve("HEAD"), LATER_ROSTER_PATH).decode("utf-8"),
            object_pairs_hook=lambda pairs: _pairs_without_duplicates(
                pairs, LATER_ROSTER_PATH
            ),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"{LATER_ROSTER_PATH} is invalid: {error}")
    roster = _list(
        _mapping(roster_document, LATER_ROSTER_PATH).get("later_promotion_roster"),
        f"{LATER_ROSTER_PATH}.later_promotion_roster",
    )
    expected = []
    for index, raw in enumerate(roster):
        label = f"{LATER_ROSTER_PATH}.later_promotion_roster[{index}]"
        item = _mapping(raw, label)
        unit_id = _string(item.get("unit_id"), f"{label}.unit_id")
        issue_id = item.get("issue_id")
        if isinstance(issue_id, bool) or not isinstance(issue_id, int) or issue_id <= 0:
            fail(f"{label}.issue_id must be a created positive issue ID")
        expected.append(
            {
                "promotion_unit_id": unit_id,
                "issue_id": issue_id,
                "url": f"https://github.com/nisavid/lemonade/issues/{issue_id}",
            }
        )
    if len(expected) != 34 or issues != expected:
        fail("later_cell_issues must exactly match the frozen 34-unit issue roster")


def _validate_pre_handoff(
    repo: GitRepository, value: Any, implementation_base: str
) -> str:
    records = _list(value, "pre_handoff_records")
    if [
        record.get("task_id") if isinstance(record, dict) else None
        for record in records
    ] != [
        "TASK-001",
        "TASK-002",
        "TASK-003",
        "TASK-004",
    ]:
        fail("pre_handoff_records must contain TASK-001 through TASK-004 in order")
    prior_checkpoint: str | None = None
    for index, raw in enumerate(records):
        label = f"pre_handoff_records[{index}]"
        record = _mapping(raw, label)
        _exact_keys(
            record,
            {
                "task_id",
                "classification",
                "source_relation",
                "checkpoint_commit",
                "checkpoint_tree",
                "outputs",
                "validation_results",
                "fallback_state",
            },
            label,
        )
        if record["classification"] != "research_refresh":
            fail(f"{label}.classification must be 'research_refresh'")
        relation = _mapping(record["source_relation"], f"{label}.source_relation")
        _exact_keys(
            relation,
            {"kind", "implementation_base_commit"},
            f"{label}.source_relation",
        )
        if relation != {
            "kind": "implementation_base_ancestor",
            "implementation_base_commit": implementation_base,
        }:
            fail(
                f"{label}.source_relation must explicitly bind the "
                "implementation-base ancestry relation"
            )
        commit = _commit(
            repo, record["checkpoint_commit"], f"{label}.checkpoint_commit"
        )
        if not repo.is_ancestor(commit, implementation_base):
            fail(
                f"{label}.checkpoint_commit must be an ancestor of "
                "implementation_base.commit"
            )
        if prior_checkpoint is not None and not repo.is_ancestor(
            prior_checkpoint, commit
        ):
            fail(f"{label}.checkpoint_commit breaks pre-handoff record order")
        _tree(repo, commit, record["checkpoint_tree"], f"{label}.checkpoint_tree")
        _outputs(repo, record["outputs"], commit, f"{label}.outputs")
        _validation_results(record["validation_results"], f"{label}.validation_results")
        _fallback(record["fallback_state"], f"{label}.fallback_state")
        prior_checkpoint = commit
    if prior_checkpoint is None:
        fail("pre_handoff_records must not be empty")
    return prior_checkpoint


def _validate_bootstrap(
    repo: GitRepository,
    value: Any,
    manifest: dict[str, Any],
    implementation_base: str,
    pre_handoff_checkpoint: str,
) -> str:
    record = _mapping(value, "implementation_base_bootstrap")
    _exact_keys(
        record,
        {
            "task_id",
            "classification",
            "checkpoint_commit",
            "checkpoint_tree",
            "outputs",
            "validation_results",
            "fallback_state",
        },
        "implementation_base_bootstrap",
    )
    if record["task_id"] != "TASK-005":
        fail("implementation_base_bootstrap.task_id must be 'TASK-005'")
    if record["classification"] != "implementation_base_bootstrap":
        fail(
            "implementation_base_bootstrap.classification must be "
            "'implementation_base_bootstrap'"
        )
    commit = _commit(
        repo,
        record["checkpoint_commit"],
        "implementation_base_bootstrap.checkpoint_commit",
    )
    if not repo.is_ancestor(implementation_base, commit):
        fail(
            "implementation_base_bootstrap.checkpoint_commit must descend from "
            "implementation_base.commit"
        )
    if not repo.is_ancestor(pre_handoff_checkpoint, commit):
        fail(
            "implementation_base_bootstrap.checkpoint_commit must descend from the "
            "pre-handoff checkpoint"
        )
    _tree(
        repo,
        commit,
        record["checkpoint_tree"],
        "implementation_base_bootstrap.checkpoint_tree",
    )
    outputs = _outputs(
        repo, record["outputs"], commit, "implementation_base_bootstrap.outputs"
    )
    if not any(
        item["path"] == "plan/portable-residency-implementation-base.json"
        for item in outputs
    ):
        fail("implementation_base_bootstrap.outputs must bind the pre-phase manifest")
    precursor = _manifest_at(
        repo, commit, "plan/portable-residency-implementation-base.json"
    )
    precursor_keys = {
        "schema",
        "research_closure",
        "stable_release",
        "implementation_base",
        "pre_handoff_records",
        "phase_records",
    }
    _exact_keys(precursor, precursor_keys, "bootstrap precursor")
    for field in precursor_keys - {"phase_records"}:
        if precursor[field] != manifest[field]:
            fail(f"bootstrap precursor {field} does not match the final manifest")
    if precursor["phase_records"] != []:
        fail("bootstrap precursor phase_records must be empty")
    if "implementation_base_bootstrap" in precursor:
        fail("bootstrap precursor must not contain a self-referential bootstrap output")
    _validation_results(
        record["validation_results"], "implementation_base_bootstrap.validation_results"
    )
    _fallback(record["fallback_state"], "implementation_base_bootstrap.fallback_state")
    return commit


def _bundle_digest(repo: GitRepository, commit: str, prefix: str) -> str:
    paths = sorted(
        path
        for path in repo.changed_paths(repo.parents(commit)[0], commit)
        if path.startswith(prefix)
    )
    payload = bytearray()
    for path in paths:
        payload.extend(path.encode("utf-8"))
        payload.append(0)
        payload.extend(repo.blob(commit, path))
        payload.append(0)
    return sha256_bytes(bytes(payload))


def _run_at_commit(
    repo: GitRepository,
    commit: str,
    command: list[str],
    patch: bytes | None = None,
) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="residency-handoff-") as directory:
        checkout = Path(directory)
        repo.materialize(commit, checkout)
        if patch is not None:
            patch_path = checkout / ".handoff-red.patch"
            patch_path.write_bytes(patch)
            applied = subprocess.run(
                [
                    "git",
                    "apply",
                    "--whitespace=nowarn",
                    "--unidiff-zero",
                    str(patch_path),
                ],
                cwd=checkout,
                capture_output=True,
                text=True,
                check=False,
            )
            patch_path.unlink()
            if applied.returncode:
                fail(f"red fixture patch does not apply: {applied.stderr.strip()}")
        try:
            return subprocess.run(
                command,
                cwd=checkout,
                capture_output=True,
                text=True,
                check=False,
                timeout=120,
                env={**_without_git_environment(), "PYTHONDONTWRITEBYTECODE": "1"},
            )
        except subprocess.TimeoutExpired:
            fail(f"fixture command timed out: {command!r}")


def _json_object_bytes(source: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(
            source.decode("utf-8"),
            object_pairs_hook=lambda pairs: _pairs_without_duplicates(pairs, label),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"{label} is invalid: {error}")
    return _mapping(value, label)


def _patch_paths(patch: bytes, label: str) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="residency-handoff-numstat-") as directory:
        patch_path = Path(directory) / "fixture.patch"
        patch_path.write_bytes(patch)
        result = subprocess.run(
            [
                "git",
                "apply",
                "--whitespace=nowarn",
                "--unidiff-zero",
                "--numstat",
                str(patch_path),
            ],
            cwd=directory,
            capture_output=True,
            text=True,
            check=False,
        )
    if result.returncode:
        fail(f"{label} cannot be inspected: {result.stderr.strip()}")
    paths: list[str] = []
    for index, line in enumerate(result.stdout.splitlines()):
        fields = line.split("\t", 2)
        if len(fields) != 3 or fields[0] == "-" or fields[1] == "-":
            fail(f"{label} contains an unsupported binary or malformed change")
        paths.append(_path(fields[2], f"{label} changed path {index}"))
    if not paths or len(paths) != len(set(paths)):
        fail(f"{label} must change a non-empty unique path set")
    return paths


def _red_fixture_metadata(
    repo: GitRepository,
    evidence_commit: str,
    metadata_path: str,
    task_id: str,
    task_base: str,
    command: list[str],
    failure_signature: str,
    label: str,
) -> int:
    metadata = _json_object_bytes(
        repo.blob(evidence_commit, metadata_path), f"{label} red fixture metadata"
    )
    _exact_keys(
        metadata,
        {
            "schema",
            "task_id",
            "task_base_commit",
            "command",
            "environment",
            "observed_result",
        },
        f"{label} red fixture metadata",
    )
    if metadata["schema"] != "portable_residency_red_fixture_result/v1":
        fail(f"{label} red fixture metadata schema is unsupported")
    if metadata["task_id"] != task_id or metadata["task_base_commit"] != task_base:
        fail(f"{label} red fixture metadata identity does not match the task")
    if _command(metadata["command"], f"{label} metadata.command") != command:
        fail(f"{label} red fixture metadata command does not match the record")
    environment = _mapping(metadata["environment"], f"{label} metadata.environment")
    _exact_keys(
        environment,
        {"platform", "python_version", "toolchain"},
        f"{label} metadata.environment",
    )
    _string(environment["platform"], f"{label} metadata.environment.platform")
    _string(
        environment["python_version"],
        f"{label} metadata.environment.python_version",
    )
    toolchain = [
        _string(item, f"{label} metadata.environment.toolchain[{index}]")
        for index, item in enumerate(
            _list(environment["toolchain"], f"{label} metadata.environment.toolchain")
        )
    ]
    if not toolchain or len(toolchain) != len(set(toolchain)):
        fail(f"{label} metadata.environment.toolchain must be non-empty and unique")
    observed = _mapping(
        metadata["observed_result"], f"{label} metadata.observed_result"
    )
    _exact_keys(
        observed,
        {"exit_code", "failure_signature"},
        f"{label} metadata.observed_result",
    )
    exit_code = observed["exit_code"]
    if isinstance(exit_code, bool) or not isinstance(exit_code, int) or exit_code == 0:
        fail(f"{label} metadata.observed_result.exit_code must be a nonzero integer")
    if observed["failure_signature"] != failure_signature:
        fail(
            f"{label} red fixture metadata failure signature does not match the record"
        )
    return exit_code


def _validate_task_evidence(
    repo: GitRepository,
    raw: Any,
    label: str,
    implementation_base: str,
) -> str:
    task = _mapping(raw, label)
    _exact_keys(
        task,
        {
            "task_id",
            "task_base_commit",
            "outputs",
            "red_fixture",
            "green_checkpoint",
            "fallback_state",
        },
        label,
    )
    task_id = _string(task["task_id"], f"{label}.task_id")
    if not TASK_ID.fullmatch(task_id) or task_id == "TASK-005":
        fail(f"{label}.task_id is invalid for red-fixture evidence")
    base = _commit(repo, task["task_base_commit"], f"{label}.task_base_commit")
    if not repo.is_ancestor(implementation_base, base):
        fail(f"{label}.task_base_commit must descend from implementation_base.commit")
    red = _mapping(task["red_fixture"], f"{label}.red_fixture")
    _exact_keys(
        red,
        {
            "evidence_commit",
            "evidence_tree",
            "bundle_sha256",
            "patch_path",
            "patch_sha256",
            "metadata_path",
            "test_paths",
            "command",
            "failure_signature",
        },
        f"{label}.red_fixture",
    )
    evidence = _commit(
        repo, red["evidence_commit"], f"{label}.red_fixture.evidence_commit"
    )
    _tree(repo, evidence, red["evidence_tree"], f"{label}.red_fixture.evidence_tree")
    if repo.parents(evidence) != [base]:
        fail(
            f"{label}.red_fixture.evidence_commit must have task_base_commit as sole parent"
        )
    prefix = f"plan/evidence/red-fixtures/{task_id}/"
    changed = repo.changed_paths(base, evidence)
    if not changed or any(not path.startswith(prefix) for path in changed):
        fail(f"{label}.red_fixture.evidence_commit changed paths outside {prefix}")
    if _bundle_digest(repo, evidence, prefix) != _digest(
        red["bundle_sha256"], f"{label}.red_fixture.bundle_sha256"
    ):
        fail(f"{label}.red_fixture.bundle_sha256 does not match the evidence bundle")
    patch_path = _path(red["patch_path"], f"{label}.red_fixture.patch_path")
    if not patch_path.startswith(prefix):
        fail(f"{label}.red_fixture.patch_path must be under {prefix}")
    if patch_path not in changed:
        fail(f"{label}.red_fixture.patch_path was not changed by evidence_commit")
    patch = repo.blob(evidence, patch_path)
    if sha256_bytes(patch) != _digest(
        red["patch_sha256"], f"{label}.red_fixture.patch_sha256"
    ):
        fail(f"{label}.red_fixture.patch_sha256 does not match the patch")
    test_paths = [
        _path(item, f"{label}.red_fixture.test_paths[{index}]")
        for index, item in enumerate(
            _list(red["test_paths"], f"{label}.red_fixture.test_paths")
        )
    ]
    if not test_paths or len(test_paths) != len(set(test_paths)):
        fail(f"{label}.red_fixture.test_paths must be non-empty and unique")
    if any(not path.startswith("test/") for path in test_paths):
        fail(f"{label}.red_fixture.test_paths must be under the public test/ root")
    if any(path.startswith(prefix) for path in test_paths):
        fail(f"{label}.red_fixture.test_paths cannot be evidence-bundle paths")
    if _patch_paths(patch, f"{label}.red_fixture.patch") != test_paths:
        fail(f"{label}.red_fixture patch paths must exactly match test_paths")
    red_command = _command(red["command"], f"{label}.red_fixture.command")
    failure_signature = _string(
        red["failure_signature"], f"{label}.red_fixture.failure_signature"
    )
    if len(failure_signature) > 500:
        fail(f"{label}.red_fixture.failure_signature is too long")
    metadata_path = _path(red["metadata_path"], f"{label}.red_fixture.metadata_path")
    if not metadata_path.startswith(prefix):
        fail(f"{label}.red_fixture.metadata_path must be under {prefix}")
    if metadata_path not in changed:
        fail(f"{label}.red_fixture.metadata_path was not changed by evidence_commit")
    observed_exit_code = _red_fixture_metadata(
        repo,
        evidence,
        metadata_path,
        task_id,
        base,
        red_command,
        failure_signature,
        label,
    )
    red_result = _run_at_commit(repo, base, red_command, patch)
    red_output = red_result.stdout + red_result.stderr
    if (
        red_result.returncode != observed_exit_code
        or red_result.returncode == 0
        or failure_signature not in red_output
    ):
        fail(f"{label}.red_fixture did not reproduce its frozen failure signature")

    green = _mapping(task["green_checkpoint"], f"{label}.green_checkpoint")
    _exact_keys(
        green, {"commit", "tree", "command", "result"}, f"{label}.green_checkpoint"
    )
    green_commit = _commit(repo, green["commit"], f"{label}.green_checkpoint.commit")
    _tree(repo, green_commit, green["tree"], f"{label}.green_checkpoint.tree")
    if not repo.is_ancestor(evidence, green_commit):
        fail(f"{label}.green_checkpoint.commit does not descend from evidence_commit")
    green_command = _command(green["command"], f"{label}.green_checkpoint.command")
    if green_command != red_command:
        fail(f"{label} red and green commands must be identical")
    if green["result"] != "pass":
        fail(f"{label}.green_checkpoint.result must be 'pass'")
    with tempfile.TemporaryDirectory(prefix="residency-handoff-patch-") as directory:
        reconstructed = Path(directory)
        repo.materialize(base, reconstructed)
        patch_file = reconstructed / ".handoff.patch"
        patch_file.write_bytes(patch)
        applied = subprocess.run(
            [
                "git",
                "apply",
                "--whitespace=nowarn",
                "--unidiff-zero",
                str(patch_file),
            ],
            cwd=reconstructed,
            capture_output=True,
            text=True,
            check=False,
        )
        patch_file.unlink()
        if applied.returncode:
            fail(
                f"{label}.red_fixture patch does not reconstruct: {applied.stderr.strip()}"
            )
        for test_path in test_paths:
            reconstructed_bytes = (reconstructed / test_path).read_bytes()
            if reconstructed_bytes != repo.blob(green_commit, test_path):
                fail(
                    f"{label} test patch is not byte-identical at green checkpoint: {test_path}"
                )
    green_result = _run_at_commit(repo, green_commit, green_command)
    if green_result.returncode:
        fail(
            f"{label}.green_checkpoint command failed: "
            f"{(green_result.stdout + green_result.stderr)[-1000:]}"
        )
    _outputs(repo, task["outputs"], green_commit, f"{label}.outputs")
    _fallback(task["fallback_state"], f"{label}.fallback_state")
    return green_commit


def _manifest_at(repo: GitRepository, commit: str, path: str) -> dict[str, Any]:
    try:
        value = json.loads(
            repo.blob(commit, path).decode("utf-8"),
            object_pairs_hook=lambda pairs: _pairs_without_duplicates(
                pairs, f"{commit}:{path}"
            ),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"historical manifest {commit}:{path} is invalid: {error}")
    return _mapping(value, f"historical manifest {commit}:{path}")


def _pairs_without_duplicates(
    pairs: list[tuple[str, Any]], label: str
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"{label} contains duplicate object key {key!r}")
        result[key] = value
    return result


def _find_phase_append_commits(
    repo: GitRepository,
    manifest: dict[str, Any],
    manifest_path: str,
    records: list[dict[str, Any]],
) -> list[str]:
    head = repo.resolve("HEAD")
    if repo.blob(head, manifest_path) != Path(repo.root / manifest_path).read_bytes():
        fail("manifest working bytes do not match HEAD")
    manifest_commits = str(
        repo.run("log", "--all", "--format=%H", "--reverse", "--", manifest_path)
    ).splitlines()
    append_commits: list[str] = []
    for index, record in enumerate(records):
        checkpoint = record["phase_checkpoint_commit"]
        candidates: list[str] = []
        for commit in manifest_commits:
            if repo.parents(commit) != [checkpoint]:
                continue
            current = _manifest_at(repo, commit, manifest_path)
            if current.get("phase_records") != records[: index + 1]:
                continue
            previous = _manifest_at(repo, checkpoint, manifest_path)
            if previous.get("phase_records") != records[:index]:
                continue
            previous_fixed = {
                key: value for key, value in previous.items() if key != "phase_records"
            }
            current_fixed = {
                key: value for key, value in current.items() if key != "phase_records"
            }
            if previous_fixed != current_fixed:
                continue
            changed_paths = repo.changed_paths(checkpoint, commit)
            if changed_paths != [manifest_path]:
                fail(
                    f"phase_records[{index}] append commit must change exactly "
                    "the manifest path"
                )
            candidates.append(commit)
        if len(candidates) != 1:
            fail(
                f"phase_records[{index}] must have exactly one append-only evidence commit"
            )
        if append_commits and not repo.is_ancestor(append_commits[-1], checkpoint):
            fail(
                f"phase_records[{index}] checkpoint does not descend from the prior append"
            )
        append_commits.append(candidates[0])
    if append_commits[-1] != head:
        fail(
            "latest phase record must be committed directly after its phase checkpoint"
        )
    return append_commits


def _validate_phases(
    repo: GitRepository,
    manifest: dict[str, Any],
    manifest_path: str,
    requested_phase: int,
    bootstrap_checkpoint: str,
    implementation_base: str,
) -> None:
    if requested_phase < 0:
        fail("--phase must be non-negative")
    records = _list(manifest["phase_records"], "phase_records")
    phases = [
        record.get("phase") if isinstance(record, dict) else None for record in records
    ]
    if phases != list(range(requested_phase + 1)):
        fail(f"phase_records must contain exactly phases 0 through {requested_phase}")
    typed_records = [
        _mapping(record, f"phase_records[{index}]")
        for index, record in enumerate(records)
    ]
    for index, record in enumerate(typed_records):
        label = f"phase_records[{index}]"
        _exact_keys(
            record,
            {
                "phase",
                "task_ids",
                "task_evidence",
                "phase_checkpoint_commit",
                "phase_checkpoint_tree",
                "record_digest",
            },
            label,
        )
        if canonical_digest(record) != _digest(
            record["record_digest"], f"{label}.record_digest"
        ):
            fail(f"{label}.record_digest does not match its canonical payload")
    append_commits: list[str] = []
    for index, raw in enumerate(records):
        label = f"phase_records[{index}]"
        record = _mapping(raw, label)
        task_ids = _list(record["task_ids"], f"{label}.task_ids")
        if any(
            not isinstance(task_id, str) or not TASK_ID.fullmatch(task_id)
            for task_id in task_ids
        ):
            fail(f"{label}.task_ids contains an invalid task ID")
        expected_task_ids = EXPECTED_PHASE_TASKS.get(index)
        if expected_task_ids is None or tuple(task_ids) != expected_task_ids:
            fail(f"{label}.task_ids must be {list(expected_task_ids or ())}")
        task_evidence = _list(record["task_evidence"], f"{label}.task_evidence")
        if [
            item.get("task_id") if isinstance(item, dict) else None
            for item in task_evidence
        ] != task_ids:
            fail(f"{label}.task_evidence must match task_ids in order")
        green_commits: list[str] = []
        for task_index, task in enumerate(task_evidence):
            task_label = f"{label}.task_evidence[{task_index}]"
            green = _validate_task_evidence(repo, task, task_label, implementation_base)
            base = task["task_base_commit"]
            expected_parent = (
                bootstrap_checkpoint
                if index == 0 and task_index == 0
                else append_commits[index - 1] if task_index == 0 else green_commits[-1]
            )
            if expected_parent is not None and base != expected_parent:
                fail(f"{task_label}.task_base_commit does not continue the task chain")
            green_commits.append(green)
        checkpoint = _commit(
            repo, record["phase_checkpoint_commit"], f"{label}.phase_checkpoint_commit"
        )
        _tree(
            repo,
            checkpoint,
            record["phase_checkpoint_tree"],
            f"{label}.phase_checkpoint_tree",
        )
        if not repo.is_ancestor(implementation_base, checkpoint):
            fail(
                f"{label}.phase_checkpoint_commit must descend from "
                "implementation_base.commit"
            )
        if not repo.is_ancestor(green_commits[-1], checkpoint):
            fail(
                f"{label}.phase_checkpoint_commit does not descend from the final green checkpoint"
            )
        if index == 0:
            append_commits = _find_phase_append_commits(
                repo, manifest, manifest_path, typed_records
            )


def validate_manifest(manifest_path: Path, requested_phase: int) -> None:
    manifest = load_manifest(manifest_path)
    _exact_keys(
        manifest,
        {
            "schema",
            "research_closure",
            "stable_release",
            "implementation_base",
            "scout_runs",
            "legacy_inventory",
            "source_seam_dispositions",
            "later_cell_issues",
            "pre_handoff_records",
            "implementation_base_bootstrap",
            "phase_records",
        },
        "manifest",
    )
    if manifest["schema"] != SCHEMA:
        fail(f"manifest.schema must be {SCHEMA!r}")
    repo = GitRepository.discover(Path.cwd())
    resolved_manifest = manifest_path.resolve()
    try:
        manifest_relative = resolved_manifest.relative_to(repo.root).as_posix()
    except ValueError:
        fail("manifest must be inside the current Git repository")
    if manifest_relative != "plan/portable-residency-implementation-base.json":
        fail("manifest must use plan/portable-residency-implementation-base.json")
    base = _validate_fixed_identity(repo, manifest)
    _validate_scouts(repo, manifest["scout_runs"], base)
    _validate_source_closure(repo, manifest, base)
    _validate_issues(repo, manifest["later_cell_issues"])
    pre_handoff_checkpoint = _validate_pre_handoff(
        repo, manifest["pre_handoff_records"], base
    )
    bootstrap_checkpoint = _validate_bootstrap(
        repo,
        manifest["implementation_base_bootstrap"],
        manifest,
        base,
        pre_handoff_checkpoint,
    )
    _validate_phases(
        repo,
        manifest,
        manifest_relative,
        requested_phase,
        bootstrap_checkpoint,
        base,
    )
