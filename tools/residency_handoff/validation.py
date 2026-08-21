"""Git-backed validation for the portable residency implementation handoff."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tarfile
import tempfile
from collections.abc import Iterable, Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path, PurePosixPath
from typing import Any

from .contract import canonical_digest, fail, load_manifest, sha256_bytes

SCHEMA = "portable_residency_implementation_handoff/v1"
SOURCE_REVISION_SCHEMA = "portable_residency_source_revision/v1"
FORK_BINDING_SCHEMA = "portable_residency_maintained_fork_binding/v1"
RED_FIXTURE_SCHEMA_V1 = "portable_residency_red_fixture_result/v1"
RED_FIXTURE_SCHEMA_V2 = "portable_residency_red_fixture_result/v2"
HEX_DIGEST = re.compile(r"^[0-9a-f]{64}$")
GIT_OBJECT = re.compile(r"^(?:[0-9a-f]{40}|[0-9a-f]{64})$")
TASK_ID = re.compile(r"^TASK-[0-9]{3}$")
ALLOWED_COMMANDS = {"cmake", "ctest", "python3"}
TASK_020_COMMAND = (
    "python3",
    "-B",
    "test/residency/recovery/test_durable_journal_public_seam.py",
)
TASK_020_FAILURE_SIGNATURE = (
    "TASK-020 durable residency persistence contract is unavailable"
)
TASK_020_FALLBACK_STATE = "production_cutover_inactive"
TASK_020_TASK_BASE_COMMIT = "0c93411bc9b0463eee0f56d38cd97fffb0bae633"
TASK_020_EVIDENCE_COMMIT = "14a737a4f80510cb86f1e4e30a9a4d0d6ccc9c5a"
TASK_020_BUNDLE_SHA256 = (
    "4e9ff6284e599bfb3869d929416b2f915524eab14506620abd1d7fe95f93a650"
)
TASK_020_PATCH_PATH = "plan/evidence/red-fixtures/TASK-020/test.patch"
TASK_020_PATCH_SHA256 = (
    "0e1dc4d01f848c19c41aade640b14f9ea656d00f358b8d82a58f8099fda12213"
)
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
LATER_ROSTER_PATH = "docs/research/portable-residency-capability-inventory.json"
EXPECTED_PHASE_TASKS = {
    0: ("TASK-093", "TASK-006", "TASK-007", "TASK-108", "TASK-109"),
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


@dataclass(frozen=True)
class RedFixtureObservation:
    exit_code: int
    stdout: bytes | None = None
    stderr: bytes | None = None


def _without_git_environment() -> dict[str, str]:
    return {
        key: value for key, value in os.environ.items() if not key.startswith("GIT_")
    }


def _git_environment(global_config: Path) -> dict[str, str]:
    return {
        **_without_git_environment(),
        "GIT_ATTR_NOSYSTEM": "1",
        "GIT_CONFIG_GLOBAL": str(global_config),
        "GIT_CONFIG_NOSYSTEM": "1",
        "GIT_NO_REPLACE_OBJECTS": "1",
    }


@contextmanager
def _isolated_git_environment() -> Iterator[dict[str, str]]:
    with tempfile.TemporaryDirectory(
        prefix="residency-handoff-git-config-"
    ) as directory:
        yield _git_environment(Path(directory) / "global-config")


class GitRepository:
    def __init__(self, root: Path):
        self.root = root.resolve()

    @classmethod
    def discover(cls, start: Path) -> GitRepository:
        with _isolated_git_environment() as environment:
            result = subprocess.run(
                ["git", "-C", str(start), "rev-parse", "--show-toplevel"],
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )
        if result.returncode:
            fail(f"cannot locate Git repository: {result.stderr.strip()}")
        return cls(Path(result.stdout.strip()))

    def run(self, *args: str, text: bool = True) -> str | bytes:
        with _isolated_git_environment() as environment:
            result = subprocess.run(
                ["git", "-C", str(self.root), *args],
                capture_output=True,
                text=text,
                check=False,
                env=environment,
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

    def blob_or_none(self, commit: str, path: str) -> bytes | None:
        listing = bytes(
            self.run(
                "ls-tree",
                "-z",
                "--full-tree",
                commit,
                "--",
                path,
                text=False,
            )
        )
        if not listing:
            return None
        entries = listing.removesuffix(b"\0").split(b"\0")
        if len(entries) != 1 or b"\t" not in entries[0]:
            fail(f"repository path lookup for {path!r} is ambiguous")
        metadata, encoded_path = entries[0].split(b"\t", 1)
        fields = metadata.split()
        if len(fields) != 3 or fields[1] != b"blob":
            fail(f"repository path {path!r} does not identify a blob")
        if encoded_path != path.encode("utf-8"):
            fail(f"repository path lookup for {path!r} returned another path")
        object_id = fields[2].decode("ascii")
        with _isolated_git_environment() as environment:
            result = subprocess.run(
                ["git", "-C", str(self.root), "cat-file", "blob", object_id],
                capture_output=True,
                check=False,
                env=environment,
            )
        if result.returncode:
            fail(
                f"cannot read repository blob for {path!r}: "
                f"{result.stderr.decode('utf-8', 'replace').strip()}"
            )
        return result.stdout

    def parents(self, commit: str) -> list[str]:
        line = str(self.run("rev-list", "--parents", "-n", "1", commit)).strip()
        return line.split()[1:]

    def is_ancestor(self, ancestor: str, descendant: str) -> bool:
        with _isolated_git_environment() as environment:
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
                env=environment,
            )
        return result.returncode == 0

    def changed_paths(self, parent: str, commit: str) -> list[str]:
        output = str(
            self.run("diff-tree", "--no-commit-id", "--name-only", "-r", parent, commit)
        )
        return [line for line in output.splitlines() if line]

    def index_matches_head(self, path: str) -> bool:
        with _isolated_git_environment() as environment:
            result = subprocess.run(
                [
                    "git",
                    "-C",
                    str(self.root),
                    "diff",
                    "--quiet",
                    "--cached",
                    "--no-ext-diff",
                    "HEAD",
                    "--",
                    path,
                ],
                capture_output=True,
                check=False,
                env=environment,
            )
        if result.returncode not in {0, 1}:
            fail(
                f"cannot compare repository path {path!r} with HEAD: "
                f"{result.stderr.decode('utf-8', 'replace').strip()}"
            )
        return result.returncode == 0

    def materialize(self, commit: str, destination: Path) -> None:
        resolved_commit = self.resolve(commit)
        object_format = str(self.run("rev-parse", "--show-object-format")).strip()
        if object_format not in {"sha1", "sha256"}:
            fail(f"unsupported source repository object format {object_format!r}")
        object_path = Path(str(self.run("rev-parse", "--git-path", "objects")).strip())
        if not object_path.is_absolute():
            object_path = self.root / object_path

        with (
            tempfile.TemporaryDirectory(
                prefix="residency-handoff-archive-repository-"
            ) as archive_directory,
            tempfile.TemporaryDirectory(
                prefix="residency-handoff-empty-git-template-"
            ) as template_directory,
            tempfile.TemporaryDirectory(
                prefix="residency-handoff-git-config-"
            ) as config_directory,
        ):
            archive_repository = Path(archive_directory)
            environment = _git_environment(Path(config_directory) / "global-config")
            initialized_archive = subprocess.run(
                [
                    "git",
                    "init",
                    "-q",
                    "--bare",
                    f"--object-format={object_format}",
                    f"--template={template_directory}",
                ],
                cwd=archive_repository,
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )
            if initialized_archive.returncode:
                fail(
                    "cannot initialize archive repository: "
                    f"{initialized_archive.stderr.strip()}"
                )
            archive_alternates = archive_repository / "objects" / "info" / "alternates"
            archive_alternates.write_text(
                f"{object_path.resolve()}\n",
                encoding="utf-8",
            )
            bound_index = subprocess.run(
                [
                    "git",
                    f"--git-dir={archive_repository}",
                    "read-tree",
                    resolved_commit,
                ],
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )
            if bound_index.returncode:
                fail(
                    "cannot bind archive attributes to source tree: "
                    f"{bound_index.stderr.strip()}"
                )
            listed_paths = subprocess.run(
                [
                    "git",
                    f"--git-dir={archive_repository}",
                    "ls-tree",
                    "-r",
                    "-t",
                    "-z",
                    "--name-only",
                    resolved_commit,
                ],
                capture_output=True,
                check=False,
                env=environment,
            )
            if listed_paths.returncode:
                fail(
                    "cannot enumerate source tree for archive attributes: "
                    f"{listed_paths.stderr.decode('utf-8', 'replace').strip()}"
                )
            checked_attributes = subprocess.run(
                [
                    "git",
                    f"--git-dir={archive_repository}",
                    "check-attr",
                    "--cached",
                    "-z",
                    "export-subst",
                    "export-ignore",
                    "--stdin",
                ],
                input=listed_paths.stdout,
                capture_output=True,
                check=False,
                env=environment,
            )
            if checked_attributes.returncode:
                fail(
                    "cannot inspect source tree archive attributes: "
                    f"{checked_attributes.stderr.decode('utf-8', 'replace').strip()}"
                )
            attribute_fields = checked_attributes.stdout.split(b"\0")
            if attribute_fields[-1:] != [b""]:
                fail("malformed Git archive attribute output")
            attribute_fields.pop()
            if len(attribute_fields) % 3:
                fail("malformed Git archive attribute output")
            for index in range(0, len(attribute_fields), 3):
                path, attribute, value = attribute_fields[index : index + 3]
                if value not in {b"unspecified", b"unset"}:
                    fail(
                        f"committed Git attribute {attribute.decode('ascii')} "
                        f"is set for archive path {path!r}"
                    )
            archived = subprocess.run(
                [
                    "git",
                    "-c",
                    "core.attributesFile=",
                    f"--git-dir={archive_repository}",
                    "archive",
                    "--format=tar",
                    resolved_commit,
                ],
                capture_output=True,
                check=False,
                env=environment,
            )
            if archived.returncode:
                fail(
                    "cannot archive replay repository: "
                    f"{archived.stderr.decode('utf-8', 'replace').strip()}"
                )
            archive = archived.stdout

            initialized = subprocess.run(
                [
                    "git",
                    "init",
                    "-q",
                    f"--object-format={object_format}",
                    f"--template={template_directory}",
                ],
                cwd=destination,
                capture_output=True,
                text=True,
                check=False,
                env=environment,
            )
        if initialized.returncode:
            fail(f"cannot initialize replay repository: {initialized.stderr.strip()}")

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
        alternates = destination / ".git" / "objects" / "info" / "alternates"
        alternates.write_text(f"{object_path.resolve()}\n", encoding="utf-8")
        for arguments in (
            ("update-ref", "--no-deref", "HEAD", resolved_commit),
            ("read-tree", resolved_commit),
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


def _repository_path(repo: GitRepository, path: Path, label: str) -> str:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(repo.root).as_posix()
    except ValueError:
        fail(f"{label} must be inside the current Git repository")
    return _path(relative, label)


def _json_mapping(source: bytes, label: str, origin: str) -> dict[str, Any]:
    try:
        value = json.loads(
            source.decode("utf-8"),
            object_pairs_hook=lambda pairs: _pairs_without_duplicates(pairs, origin),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"{label} is invalid JSON: {error}")
    return _mapping(value, label)


def _require_working_path_matches_committed(
    repo: GitRepository,
    working_path: Path,
    relative_path: str,
    committed: bytes,
    label: str,
) -> None:
    try:
        working = working_path.read_bytes()
    except OSError as error:
        fail(f"cannot read {label}: {error}")
    if working.replace(b"\r\n", b"\n") != committed.replace(
        b"\r\n", b"\n"
    ) or not repo.index_matches_head(relative_path):
        fail(f"{label} differs from its committed checkpoint")


def _committed_path_checkpoint(
    repo: GitRepository,
    working_path: Path,
    label: str,
    *,
    path_only: bool,
    first_introduction: bool,
) -> tuple[str, str, bytes]:
    relative_path = _repository_path(repo, working_path, f"{label}.path")
    head = repo.resolve("HEAD^{commit}")
    committed = repo.blob_or_none(head, relative_path)
    if committed is None:
        fail(f"{label} is absent from HEAD")
    _require_working_path_matches_committed(
        repo,
        working_path,
        relative_path,
        committed,
        label,
    )
    digest = sha256_bytes(committed)
    commits = str(repo.run("rev-list", head, "--", relative_path)).splitlines()
    candidates: list[str] = []
    for commit in commits:
        parents = repo.parents(commit)
        if len(parents) != 1:
            continue
        changed = repo.changed_paths(parents[0], commit)
        if relative_path not in changed:
            continue
        if path_only and changed != [relative_path]:
            continue
        if (
            first_introduction
            and repo.blob_or_none(parents[0], relative_path) is not None
        ):
            continue
        candidate = repo.blob_or_none(commit, relative_path)
        if candidate is not None and sha256_bytes(candidate) == digest:
            candidates.append(commit)
    if len(candidates) != 1:
        if first_introduction:
            requirement = (
                "a unique path-only first-introduction append"
                if path_only
                else "a unique first-introduction commit"
            )
        else:
            requirement = (
                "a unique path-only append" if path_only else "a unique commit"
            )
        fail(f"{label} must be {requirement} in HEAD history")
    return candidates[0], relative_path, committed


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
    if fallback["authority"] != "legacy_runtime":
        fail(f"{label}.authority must be 'legacy_runtime'")
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
        if recorded != peeled:
            fail(f"stable_release.{prefix}_ref_commit must equal peeled_commit")
    return peeled


def _validate_source_release(
    repo: GitRepository, value: Any, label: str = "upstream_release"
) -> str:
    release = _mapping(value, label)
    _exact_keys(
        release,
        {"tag", "release_url", "tag_object", "peeled_commit", "tree"},
        label,
    )
    tag = _string(release["tag"], f"{label}.tag")
    url = _string(release["release_url"], f"{label}.release_url")
    expected_url = f"https://github.com/lemonade-sdk/lemonade/releases/tag/{tag}"
    if url != expected_url:
        fail(f"{label}.release_url must be the canonical upstream release URL")
    peeled = _commit(repo, release["peeled_commit"], f"{label}.peeled_commit")
    _tree(repo, peeled, release["tree"], f"{label}.tree")
    ref = f"refs/tags/{tag}"
    ref_value = repo.resolve(ref)
    tag_object = release["tag_object"]
    if tag_object is None:
        if repo.object_type(ref_value) != "commit" or ref_value != peeled:
            fail(f"{label} lightweight tag identity is inconsistent")
    else:
        annotated = _object_id(tag_object, f"{label}.tag_object")
        if repo.object_type(annotated) != "tag" or annotated != ref_value:
            fail(f"{label}.tag_object does not match the annotated tag")
        if repo.resolve(f"{ref}^{{commit}}") != peeled:
            fail(f"{label} annotated tag does not peel to peeled_commit")
    return peeled


def _validate_predecessor_campaign(
    repo: GitRepository, value: Any
) -> tuple[str, dict[str, Any]]:
    label = "predecessor_campaign"
    predecessor = _mapping(value, label)
    _exact_keys(
        predecessor,
        {
            "manifest_path",
            "accepted_append_commit",
            "accepted_append_tree",
            "manifest_sha256",
            "terminal_phase",
            "terminal_record_digest",
            "stable_release_commit",
            "implementation_base_commit",
        },
        label,
    )
    manifest_path = _path(predecessor["manifest_path"], f"{label}.manifest_path")
    if manifest_path != "plan/portable-residency-implementation-base.json":
        fail(f"{label}.manifest_path must name the implementation handoff")
    append_commit = _commit(
        repo,
        predecessor["accepted_append_commit"],
        f"{label}.accepted_append_commit",
    )
    _tree(
        repo,
        append_commit,
        predecessor["accepted_append_tree"],
        f"{label}.accepted_append_tree",
    )
    manifest_bytes = repo.blob(append_commit, manifest_path)
    if sha256_bytes(manifest_bytes) != _digest(
        predecessor["manifest_sha256"], f"{label}.manifest_sha256"
    ):
        fail(f"{label}.manifest_sha256 does not match the accepted append")
    manifest = _manifest_at(repo, append_commit, manifest_path)
    if manifest.get("schema") != SCHEMA:
        fail(f"{label} does not bind a supported implementation handoff")
    records = _list(manifest.get("phase_records"), f"{label}.phase_records")
    terminal_phase = predecessor["terminal_phase"]
    if isinstance(terminal_phase, bool) or not isinstance(terminal_phase, int):
        fail(f"{label}.terminal_phase must be an integer")
    if not records or len(records) != terminal_phase + 1:
        fail(f"{label}.terminal_phase does not match the accepted manifest")
    terminal = _mapping(records[-1], f"{label}.terminal_record")
    if terminal.get("phase") != terminal_phase:
        fail(f"{label}.terminal_phase does not match the terminal record")
    terminal_digest = _digest(
        predecessor["terminal_record_digest"], f"{label}.terminal_record_digest"
    )
    if terminal.get("record_digest") != terminal_digest:
        fail(f"{label}.terminal_record_digest does not match the terminal record")
    if canonical_digest(terminal) != terminal_digest:
        fail(f"{label} terminal record digest is not canonical")
    stable_commit = _commit(
        repo,
        predecessor["stable_release_commit"],
        f"{label}.stable_release_commit",
    )
    stable_release = _mapping(manifest.get("stable_release"), f"{label}.stable_release")
    if stable_release.get("peeled_commit") != stable_commit:
        fail(f"{label}.stable_release_commit does not match the accepted manifest")
    implementation_base = _commit(
        repo,
        predecessor["implementation_base_commit"],
        f"{label}.implementation_base_commit",
    )
    accepted_base = _mapping(
        manifest.get("implementation_base"), f"{label}.implementation_base"
    )
    if accepted_base.get("commit") != implementation_base:
        fail(f"{label}.implementation_base_commit does not match the accepted manifest")
    checkpoint = _commit(
        repo,
        terminal.get("phase_checkpoint_commit"),
        f"{label}.terminal_record.phase_checkpoint_commit",
    )
    _tree(
        repo,
        checkpoint,
        terminal.get("phase_checkpoint_tree"),
        f"{label}.terminal_record.phase_checkpoint_tree",
    )
    if repo.parents(append_commit) != [checkpoint]:
        fail(f"{label}.accepted_append_commit is not the terminal evidence append")
    if repo.changed_paths(str(checkpoint), append_commit) != [manifest_path]:
        fail(f"{label}.accepted_append_commit changed more than the handoff manifest")
    return append_commit, manifest


def _validate_predecessor_scout(
    repo: GitRepository, value: Any
) -> tuple[str, dict[str, Any]]:
    label = "predecessor_scout"
    scout = _mapping(value, label)
    _exact_keys(
        scout,
        {"path", "checkpoint_commit", "checkpoint_tree", "sha256"},
        label,
    )
    path = _path(scout["path"], f"{label}.path")
    commit = _commit(repo, scout["checkpoint_commit"], f"{label}.checkpoint_commit")
    _tree(repo, commit, scout["checkpoint_tree"], f"{label}.checkpoint_tree")
    source = repo.blob(commit, path)
    if sha256_bytes(source) != _digest(scout["sha256"], f"{label}.sha256"):
        fail(f"{label}.sha256 does not match its checkpoint")
    report = _json_mapping(source, label, f"{commit}:{path}")
    if report.get("schema") != "portable_residency_stable_source_scout/v1":
        fail(f"{label} schema is unsupported")
    return commit, report


def _blob_digest_or_none(repo: GitRepository, commit: str, path: str) -> str | None:
    source = repo.blob_or_none(commit, path)
    return sha256_bytes(source) if source is not None else None


def _validate_accepted_predecessor_scout(
    manifest: dict[str, Any],
    scout_reference: Any,
    scout_commit: str,
    scout: dict[str, Any],
) -> dict[str, tuple[str, ...]]:
    reference = _mapping(scout_reference, "predecessor_scout")
    runs = _list(manifest.get("scout_runs"), "predecessor_campaign.scout_runs")
    matches = [
        _mapping(run, f"predecessor_campaign.scout_runs[{index}]")
        for index, run in enumerate(runs)
        if isinstance(run, dict) and run.get("id") == "implementation-base-source-scout"
    ]
    if len(matches) != 1:
        fail("accepted predecessor campaign must contain one implementation-base scout")
    run = matches[0]
    _exact_keys(run, {"id", "commit", "tree", "inputs", "outputs"}, "accepted scout")
    expected_path = _path(reference["path"], "predecessor_scout.path")
    expected_digest = _digest(reference["sha256"], "predecessor_scout.sha256")
    if run["commit"] != scout_commit or run["tree"] != reference["checkpoint_tree"]:
        fail("predecessor_scout does not match the accepted predecessor scout")
    outputs = _list(run["outputs"], "accepted scout.outputs")
    matching_outputs = []
    for index, raw in enumerate(outputs):
        output = _mapping(raw, f"accepted scout.outputs[{index}]")
        _exact_keys(output, {"path", "sha256"}, f"accepted scout.outputs[{index}]")
        if output["path"] == expected_path and output["sha256"] == expected_digest:
            matching_outputs.append(output)
    if len(matching_outputs) != 1:
        fail("predecessor_scout does not match the accepted predecessor scout")
    if run["inputs"] != scout.get("inputs"):
        fail(
            "predecessor_scout input roster differs from the accepted predecessor scout"
        )
    stable = _mapping(
        manifest.get("stable_release"), "predecessor_campaign.stable_release"
    )
    implementation_base = _mapping(
        manifest.get("implementation_base"),
        "predecessor_campaign.implementation_base",
    )
    if scout.get("stable_release_commit") != stable.get("peeled_commit"):
        fail("predecessor_scout stable release differs from the accepted campaign")
    if scout.get("implementation_base_commit") != implementation_base.get("commit"):
        fail("predecessor_scout implementation base differs from the accepted campaign")

    inputs: set[str] = set()
    for index, raw in enumerate(_list(scout.get("inputs"), "predecessor_scout.inputs")):
        label = f"predecessor_scout.inputs[{index}]"
        item = _mapping(raw, label)
        _exact_keys(item, {"path", "sha256"}, label)
        path = _path(item.get("path"), f"{label}.path")
        _digest(item.get("sha256"), f"{label}.sha256")
        if path in inputs:
            fail(f"predecessor_scout.inputs repeats {path!r}")
        inputs.add(path)
    claims: dict[str, set[str]] = {path: set() for path in inputs}
    for collection in ("source_facts", "source_seam_dispositions"):
        for index, raw in enumerate(
            _list(scout.get(collection), f"predecessor_scout.{collection}")
        ):
            item = _mapping(raw, f"predecessor_scout.{collection}[{index}]")
            path = _path(
                item.get("path"), f"predecessor_scout.{collection}[{index}].path"
            )
            claim = _string(
                item.get("id"), f"predecessor_scout.{collection}[{index}].id"
            )
            if path not in claims:
                fail(f"predecessor_scout claim {claim!r} names an untracked input")
            if claim in claims[path]:
                fail(f"predecessor_scout repeats claim {claim!r} for {path!r}")
            claims[path].add(claim)
    if any(not values for values in claims.values()):
        fail("every predecessor scout input must bind at least one claim")
    return {path: tuple(sorted(values)) for path, values in claims.items()}


def _validate_revalidation_evidence(
    repo: GitRepository,
    value: Any,
    revision: dict[str, Any],
    reviewed_commit: str,
    input_path: str,
    predecessor_digest: str,
    upstream_digest: str | None,
    fork_digest: str | None,
    expected_claims: tuple[str, ...],
    label: str,
) -> str:
    evidence = _mapping(value, label)
    _exact_keys(
        evidence,
        {"path", "checkpoint_commit", "checkpoint_tree", "sha256"},
        label,
    )
    path = _path(evidence["path"], f"{label}.path")
    if not path.startswith("plan/evidence/source-revisions/"):
        fail(f"{label}.path must be source-revision evidence")
    checkpoint = _commit(
        repo, evidence["checkpoint_commit"], f"{label}.checkpoint_commit"
    )
    _tree(repo, checkpoint, evidence["checkpoint_tree"], f"{label}.checkpoint_tree")
    if not repo.is_ancestor(reviewed_commit, checkpoint):
        fail(f"{label}.checkpoint_commit must descend from the reviewed fork")
    source = repo.blob(checkpoint, path)
    if sha256_bytes(source) != _digest(evidence["sha256"], f"{label}.sha256"):
        fail(f"{label}.sha256 does not match its checkpoint")
    payload = _json_mapping(source, label, f"{checkpoint}:{path}")
    _exact_keys(
        payload,
        {
            "schema",
            "revision_id",
            "source_revision_record_digest",
            "input_path",
            "predecessor_sha256",
            "selected_upstream_sha256",
            "reviewed_maintained_fork_commit",
            "reviewed_fork_sha256",
            "validated_claims",
            "result",
        },
        label,
    )
    if payload["schema"] != "portable_residency_source_revalidation/v1":
        fail(f"{label} schema is unsupported")
    if payload["revision_id"] != revision["revision_id"]:
        fail(f"{label} does not bind revision_id")
    if payload["source_revision_record_digest"] != revision["record_digest"]:
        fail(f"{label} does not bind source_revision_record_digest")
    if payload["input_path"] != input_path:
        fail(f"{label} does not bind input_path")
    if payload["predecessor_sha256"] != predecessor_digest:
        fail(f"{label} does not bind predecessor_sha256")
    if payload["selected_upstream_sha256"] != upstream_digest:
        fail(f"{label} does not bind selected_upstream_sha256")
    if payload["reviewed_maintained_fork_commit"] != reviewed_commit:
        fail(f"{label} does not bind the reviewed maintained-fork commit")
    if payload["reviewed_fork_sha256"] != fork_digest:
        fail(f"{label} does not bind reviewed_fork_sha256")
    claims = tuple(
        _string(claim, f"{label}.validated_claims[{index}]")
        for index, claim in enumerate(
            _list(payload["validated_claims"], f"{label}.validated_claims")
        )
    )
    if claims != expected_claims:
        fail(f"{label}.validated_claims does not match predecessor claims")
    if payload["result"] != "accepted":
        fail(f"{label}.result must be 'accepted'")
    return checkpoint


def _validate_fork_binding(
    repo: GitRepository,
    revision: dict[str, Any],
    source_checkpoint_commit: str,
    source_checkpoint_path: str,
    source_checkpoint_digest: str,
    binding_path: Path,
    release_commit: str,
    fork_base_commit: str,
    predecessor_inputs: dict[str, str],
    upstream_inputs: dict[str, str | None],
    predecessor_claims: dict[str, tuple[str, ...]],
) -> str:
    binding_commit, _, binding_bytes = _committed_path_checkpoint(
        repo,
        binding_path,
        "fork binding",
        path_only=True,
        first_introduction=True,
    )
    binding = _json_mapping(binding_bytes, "fork_binding", str(binding_path))
    _exact_keys(
        binding,
        {
            "schema",
            "revision_id",
            "source_revision",
            "reviewed_maintained_fork",
            "scout_input_dispositions",
            "fallback_state",
            "runtime_authority",
            "status",
            "record_digest",
        },
        "fork_binding",
    )
    if binding["schema"] != FORK_BINDING_SCHEMA:
        fail(f"fork_binding.schema must be {FORK_BINDING_SCHEMA!r}")
    if binding["revision_id"] != revision["revision_id"]:
        fail("fork_binding.revision_id does not match source_revision")
    if canonical_digest(binding) != _digest(
        binding["record_digest"], "fork_binding.record_digest"
    ):
        fail("fork_binding.record_digest does not match its canonical payload")
    source = _mapping(binding["source_revision"], "fork_binding.source_revision")
    _exact_keys(
        source,
        {"path", "checkpoint_commit", "checkpoint_tree", "sha256", "record_digest"},
        "fork_binding.source_revision",
    )
    source_path = _path(source["path"], "fork_binding.source_revision.path")
    if source_path != source_checkpoint_path:
        fail("fork_binding.source_revision.path does not name the validated revision")
    source_commit = _commit(
        repo,
        source["checkpoint_commit"],
        "fork_binding.source_revision.checkpoint_commit",
    )
    _tree(
        repo,
        source_commit,
        source["checkpoint_tree"],
        "fork_binding.source_revision.checkpoint_tree",
    )
    if source_commit != source_checkpoint_commit:
        fail(
            "fork_binding.source_revision.checkpoint_commit does not match "
            "the committed source revision checkpoint"
        )
    source_bytes = repo.blob(source_commit, source_path)
    source_digest = _digest(source["sha256"], "fork_binding.source_revision.sha256")
    if sha256_bytes(source_bytes) != source_digest:
        fail("fork_binding.source_revision.sha256 does not match its checkpoint")
    if source_digest != source_checkpoint_digest:
        fail("fork_binding.source_revision.sha256 does not match source revision")
    if source["record_digest"] != revision["record_digest"]:
        fail("fork_binding.source_revision.record_digest does not match")
    reviewed = _mapping(binding["reviewed_maintained_fork"], "reviewed_maintained_fork")
    _exact_keys(
        reviewed,
        {"commit", "tree", "review_url", "reviewed_commit"},
        "reviewed_maintained_fork",
    )
    reviewed_commit = _commit(
        repo, reviewed["commit"], "reviewed_maintained_fork.commit"
    )
    _tree(
        repo,
        reviewed_commit,
        reviewed["tree"],
        "reviewed_maintained_fork.tree",
    )
    review_url = _string(reviewed["review_url"], "reviewed_maintained_fork.review_url")
    if not re.fullmatch(
        r"https://github\.com/nisavid/lemonade/pull/[1-9][0-9]*", review_url
    ):
        fail("reviewed_maintained_fork.review_url must name a fork pull request")
    if reviewed["reviewed_commit"] != reviewed_commit:
        fail("reviewed_maintained_fork.reviewed_commit must equal commit")
    if reviewed_commit == release_commit:
        fail("reviewed maintained-fork commit must be distinct from upstream release")
    if reviewed_commit == source_commit:
        fail(
            "reviewed maintained-fork commit must follow the source revision checkpoint"
        )
    for ancestor, label in (
        (release_commit, "selected upstream release"),
        (fork_base_commit, "maintained fork base"),
        (source_commit, "source revision checkpoint"),
    ):
        if not repo.is_ancestor(ancestor, reviewed_commit):
            fail(f"reviewed maintained-fork commit must descend from {label}")
    reviewed_source = repo.blob_or_none(reviewed_commit, source_path)
    if reviewed_source is None or sha256_bytes(reviewed_source) != source_digest:
        fail("reviewed maintained-fork commit changed the bound source revision")
    dispositions = _list(
        binding["scout_input_dispositions"],
        "fork_binding.scout_input_dispositions",
    )
    seen: set[str] = set()
    unresolved = False
    evidence_checkpoints: set[str] = set()
    for index, raw in enumerate(dispositions):
        label = f"fork_binding.scout_input_dispositions[{index}]"
        item = _mapping(raw, label)
        _exact_keys(
            item,
            {"path", "fork_sha256", "disposition", "evidence"},
            label,
        )
        path = _path(item["path"], f"{label}.path")
        if path in seen:
            fail(f"fork_binding.scout_input_dispositions repeats {path!r}")
        seen.add(path)
        if path not in predecessor_inputs:
            fail(f"{label}.path is not a predecessor scout input")
        actual_fork = _blob_digest_or_none(repo, reviewed_commit, path)
        recorded_fork = item["fork_sha256"]
        if recorded_fork is not None:
            recorded_fork = _digest(recorded_fork, f"{label}.fork_sha256")
        if recorded_fork != actual_fork:
            fail(f"{label}.fork_sha256 does not match reviewed fork")
        can_carry_forward = (
            upstream_inputs[path] == predecessor_inputs[path]
            and actual_fork == predecessor_inputs[path]
        )
        if can_carry_forward:
            if item["disposition"] != "carried_forward" or item["evidence"] is not None:
                fail(f"{label} byte-identical input must be carried_forward")
        elif item["disposition"] == "revalidated":
            evidence_checkpoints.add(
                _validate_revalidation_evidence(
                    repo,
                    item["evidence"],
                    revision,
                    reviewed_commit,
                    path,
                    predecessor_inputs[path],
                    upstream_inputs[path],
                    actual_fork,
                    predecessor_claims[path],
                    f"{label}.evidence",
                )
            )
        elif item["disposition"] == "revalidation_required":
            if item["evidence"] is not None:
                fail(f"{label}.evidence must be null while revalidation is required")
            unresolved = True
        else:
            fail(
                f"{label}.disposition must be 'revalidated' or 'revalidation_required'"
            )
    if seen != set(predecessor_inputs):
        fail("fork_binding.scout_input_dispositions must cover every scout input")
    for ancestor, label in (
        (reviewed_commit, "reviewed maintained-fork commit"),
        *(
            (checkpoint, "revalidation evidence checkpoint")
            for checkpoint in sorted(evidence_checkpoints)
        ),
    ):
        if ancestor == binding_commit or not repo.is_ancestor(ancestor, binding_commit):
            fail(f"fork binding checkpoint must descend from {label}")
    _fallback(binding["fallback_state"], "fork_binding.fallback_state")
    if binding["runtime_authority"] != "none":
        fail("fork_binding.runtime_authority must be 'none'")
    expected_status = "awaiting_revalidation" if unresolved else "source_ready"
    if binding["status"] != expected_status:
        fail(f"fork_binding.status must be {expected_status!r}")
    return expected_status


def validate_source_revision(
    source_revision_path: Path, fork_binding_path: Path | None = None
) -> str:
    revision = load_manifest(source_revision_path)
    _exact_keys(
        revision,
        {
            "schema",
            "revision_id",
            "predecessor_campaign",
            "predecessor_scout",
            "maintained_fork_base",
            "upstream_release",
            "scout_input_dispositions",
            "fallback_state",
            "runtime_authority",
            "status",
            "record_digest",
        },
        "source_revision",
    )
    if revision["schema"] != SOURCE_REVISION_SCHEMA:
        fail(f"source_revision.schema must be {SOURCE_REVISION_SCHEMA!r}")
    _string(revision["revision_id"], "source_revision.revision_id")
    if canonical_digest(revision) != _digest(
        revision["record_digest"], "source_revision.record_digest"
    ):
        fail("source_revision.record_digest does not match its canonical payload")
    repo = GitRepository.discover(Path.cwd())
    source_checkpoint, source_path, source_bytes = _committed_path_checkpoint(
        repo,
        source_revision_path,
        "source revision",
        path_only=False,
        first_introduction=True,
    )
    append_commit, predecessor_manifest = _validate_predecessor_campaign(
        repo, revision["predecessor_campaign"]
    )
    scout_commit, scout = _validate_predecessor_scout(
        repo, revision["predecessor_scout"]
    )
    if not repo.is_ancestor(scout_commit, append_commit):
        fail("predecessor_scout must precede the accepted campaign append")
    predecessor_claims = _validate_accepted_predecessor_scout(
        predecessor_manifest,
        revision["predecessor_scout"],
        scout_commit,
        scout,
    )
    fork_base = _mapping(revision["maintained_fork_base"], "maintained_fork_base")
    _exact_keys(fork_base, {"commit", "tree"}, "maintained_fork_base")
    fork_commit = _commit(repo, fork_base["commit"], "maintained_fork_base.commit")
    _tree(repo, fork_commit, fork_base["tree"], "maintained_fork_base.tree")
    if not repo.is_ancestor(append_commit, fork_commit):
        fail("maintained_fork_base.commit must descend from the accepted campaign")
    if not repo.is_ancestor(fork_commit, source_checkpoint):
        fail("source revision checkpoint must descend from maintained_fork_base")
    release_commit = _validate_source_release(repo, revision["upstream_release"])
    dispositions = _list(
        revision["scout_input_dispositions"], "scout_input_dispositions"
    )
    prior_inputs = _list(scout.get("inputs"), "predecessor_scout.inputs")
    expected: dict[str, str] = {}
    upstream_inputs: dict[str, str | None] = {}
    for index, raw in enumerate(prior_inputs):
        item = _mapping(raw, f"predecessor_scout.inputs[{index}]")
        _exact_keys(item, {"path", "sha256"}, f"predecessor_scout.inputs[{index}]")
        path = _path(item["path"], f"predecessor_scout.inputs[{index}].path")
        if path in expected:
            fail(f"predecessor_scout.inputs repeats {path!r}")
        expected[path] = _digest(
            item["sha256"], f"predecessor_scout.inputs[{index}].sha256"
        )
    seen: set[str] = set()
    for index, raw in enumerate(dispositions):
        label = f"scout_input_dispositions[{index}]"
        item = _mapping(raw, label)
        _exact_keys(
            item,
            {
                "path",
                "predecessor_sha256",
                "upstream_sha256",
                "disposition",
            },
            label,
        )
        path = _path(item["path"], f"{label}.path")
        if path in seen:
            fail(f"scout_input_dispositions repeats {path!r}")
        seen.add(path)
        if path not in expected:
            fail(f"{label}.path is not a predecessor scout input")
        predecessor_digest = _digest(
            item["predecessor_sha256"], f"{label}.predecessor_sha256"
        )
        if predecessor_digest != expected[path]:
            fail(f"{label}.predecessor_sha256 does not match predecessor scout")
        actual_upstream = _blob_digest_or_none(repo, release_commit, path)
        recorded_upstream = item["upstream_sha256"]
        if recorded_upstream is not None:
            recorded_upstream = _digest(recorded_upstream, f"{label}.upstream_sha256")
        if recorded_upstream != actual_upstream:
            fail(f"{label}.upstream_sha256 does not match selected release")
        upstream_inputs[path] = actual_upstream
        expected_disposition = (
            "candidate_for_carry_forward"
            if actual_upstream == predecessor_digest
            else "revalidation_required"
        )
        if item["disposition"] != expected_disposition:
            fail(f"{label}.disposition must be {expected_disposition!r}")
    if seen != set(expected):
        fail("scout_input_dispositions must cover every predecessor scout input")
    _fallback(revision["fallback_state"], "source_revision.fallback_state")
    if revision["runtime_authority"] != "none":
        fail("source_revision.runtime_authority must be 'none'")
    if revision["status"] != "awaiting_maintained_fork_binding":
        fail("source_revision.status must be 'awaiting_maintained_fork_binding'")
    if fork_binding_path is None:
        return str(revision["status"])
    return _validate_fork_binding(
        repo,
        revision,
        source_checkpoint,
        source_path,
        sha256_bytes(source_bytes),
        fork_binding_path,
        release_commit,
        fork_commit,
        expected,
        upstream_inputs,
        predecessor_claims,
    )


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
    *,
    text: bool = True,
) -> subprocess.CompletedProcess[Any]:
    with tempfile.TemporaryDirectory(prefix="residency-handoff-") as directory:
        checkout = Path(directory)
        repo.materialize(commit, checkout)
        if patch is not None:
            _apply_patch(
                checkout,
                patch,
                "red fixture patch does not apply",
            )
        with _isolated_git_environment() as environment:
            try:
                return subprocess.run(
                    command,
                    cwd=checkout,
                    capture_output=True,
                    text=text,
                    check=False,
                    timeout=120,
                    env={**environment, "PYTHONDONTWRITEBYTECODE": "1"},
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
        with _isolated_git_environment() as environment:
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
                env=environment,
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


def _apply_patch(checkout: Path, patch: bytes, failure: str) -> None:
    with tempfile.TemporaryDirectory(
        prefix="residency-handoff-patch-file-"
    ) as directory:
        patch_path = Path(directory) / "fixture.patch"
        patch_path.write_bytes(patch)
        applied = subprocess.run(
            [
                "git",
                "-c",
                "core.autocrlf=false",
                "-c",
                "core.attributesFile=",
                "apply",
                "--no-index",
                "--whitespace=nowarn",
                "--unidiff-zero",
                str(patch_path),
            ],
            cwd=checkout,
            capture_output=True,
            text=True,
            check=False,
            env={
                **_git_environment(Path(directory) / "global-config"),
                "GIT_DIR": str(Path(directory) / "no-repository"),
            },
        )
    if applied.returncode:
        fail(f"{failure}: {applied.stderr.strip()}")


def _fixture_bytes(checkout: Path, path: str, label: str) -> bytes:
    try:
        return (checkout / path).read_bytes()
    except OSError as error:
        fail(f"{label} cannot be read: {error}")


def _stream_bytes(value: Any, label: str) -> bytes:
    stream = _mapping(value, label)
    _exact_keys(stream, {"byte_count", "hex", "sha256"}, label)
    byte_count = stream["byte_count"]
    if (
        isinstance(byte_count, bool)
        or not isinstance(byte_count, int)
        or byte_count < 0
    ):
        fail(f"{label}.byte_count must be a nonnegative integer")
    encoded = stream["hex"]
    if (
        not isinstance(encoded, str)
        or re.fullmatch(r"(?:[0-9a-f]{2})*", encoded) is None
    ):
        fail(f"{label}.hex must be lowercase hexadecimal bytes")
    decoded = bytes.fromhex(encoded)
    if len(decoded) != byte_count:
        fail(f"{label}.byte_count does not match its bytes")
    if sha256_bytes(decoded) != _digest(stream["sha256"], f"{label}.sha256"):
        fail(f"{label}.sha256 does not match its bytes")
    return decoded


def _patched_fixture_digests(
    repo: GitRepository,
    base: str,
    patch: bytes,
    test_paths: list[str],
    label: str,
) -> dict[str, str]:
    with tempfile.TemporaryDirectory(
        prefix="residency-handoff-v2-fixtures-"
    ) as directory:
        checkout = Path(directory)
        repo.materialize(base, checkout)
        _apply_patch(checkout, patch, f"{label} does not apply")
        return {
            path: sha256_bytes(
                _fixture_bytes(checkout, path, f"{label} fixture {path}")
            )
            for path in test_paths
        }


def _red_fixture_metadata_v2(
    repo: GitRepository,
    metadata: dict[str, Any],
    evidence_commit: str,
    bundle_sha256: str,
    task_id: str,
    task_base: str,
    command: list[str],
    failure_signature: str,
    patch_path: str,
    patch: bytes,
    test_paths: list[str],
    label: str,
) -> RedFixtureObservation:
    metadata_label = f"{label} red fixture metadata"
    _exact_keys(
        metadata,
        {
            "command",
            "environment",
            "fallback_state",
            "fixture_sha256",
            "observed_result",
            "patch",
            "schema",
            "task_base_commit",
            "task_id",
        },
        metadata_label,
    )
    if metadata["schema"] != RED_FIXTURE_SCHEMA_V2:
        fail(f"{metadata_label} schema is unsupported")
    if task_id != "TASK-020":
        fail(f"{metadata_label} v2 is reserved for TASK-020")
    if evidence_commit != TASK_020_EVIDENCE_COMMIT:
        fail(f"{label}.red_fixture.evidence_commit is not the frozen TASK-020 RED")
    if bundle_sha256 != TASK_020_BUNDLE_SHA256:
        fail(f"{label}.red_fixture.bundle_sha256 is not the frozen TASK-020 bundle")
    if task_base != TASK_020_TASK_BASE_COMMIT:
        fail(f"{label}.task_base_commit is not the frozen TASK-020 base")
    if metadata["task_id"] != task_id or metadata["task_base_commit"] != task_base:
        fail(f"{label} red fixture metadata identity does not match the task")
    metadata_command = _command(metadata["command"], f"{label} metadata.command")
    if metadata_command != command:
        fail(f"{label} red fixture metadata command does not match the record")
    if tuple(metadata_command) != TASK_020_COMMAND:
        fail(
            f"{label} red fixture metadata must use the exact accepted TASK-020 command"
        )

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

    if metadata["fallback_state"] != TASK_020_FALLBACK_STATE:
        fail(
            f"{label} red fixture metadata fallback_state must be "
            f"{TASK_020_FALLBACK_STATE!r}"
        )
    recorded_patch = _mapping(metadata["patch"], f"{label} metadata.patch")
    _exact_keys(recorded_patch, {"path", "sha256"}, f"{label} metadata.patch")
    if (
        patch_path != TASK_020_PATCH_PATH
        or sha256_bytes(patch) != TASK_020_PATCH_SHA256
    ):
        fail(f"{label}.red_fixture patch is not the frozen TASK-020 patch")
    if _path(
        recorded_patch["path"], f"{label} metadata.patch.path"
    ) != patch_path or _digest(
        recorded_patch["sha256"], f"{label} metadata.patch.sha256"
    ) != sha256_bytes(
        patch
    ):
        fail(f"{label} red fixture metadata patch identity does not match the record")

    if tuple(test_paths) != TASK_020_FIXTURE_PATHS:
        fail(f"{label}.red_fixture.test_paths must equal the TASK-020 fixture set")
    fixture_sha256 = _mapping(
        metadata["fixture_sha256"], f"{label} metadata.fixture_sha256"
    )
    _exact_keys(
        fixture_sha256,
        TASK_020_FIXTURE_PATHS,
        f"{label} metadata.fixture_sha256",
    )
    actual_fixture_sha256 = _patched_fixture_digests(
        repo,
        task_base,
        patch,
        test_paths,
        f"{label}.red_fixture.patch",
    )
    recorded_fixture_sha256 = {
        path: _digest(
            fixture_sha256[path],
            f"{label} metadata.fixture_sha256[{path!r}]",
        )
        for path in test_paths
    }
    if (
        recorded_fixture_sha256 != TASK_020_FIXTURE_SHA256
        or actual_fixture_sha256 != TASK_020_FIXTURE_SHA256
    ):
        fail(
            f"{label} red fixture metadata fixture_sha256 does not match patched bytes"
        )

    observed = _mapping(
        metadata["observed_result"], f"{label} metadata.observed_result"
    )
    _exact_keys(
        observed,
        {"exit_code", "failure_signature", "stderr", "stdout"},
        f"{label} metadata.observed_result",
    )
    exit_code = observed["exit_code"]
    if isinstance(exit_code, bool) or not isinstance(exit_code, int) or exit_code != 1:
        fail(f"{label} metadata.observed_result.exit_code must be 1")
    if (
        observed["failure_signature"] != failure_signature
        or failure_signature != TASK_020_FAILURE_SIGNATURE
    ):
        fail(
            f"{label} red fixture metadata failure signature does not match the record"
        )
    stdout = _stream_bytes(
        observed["stdout"], f"{label} metadata.observed_result.stdout"
    )
    stderr = _stream_bytes(
        observed["stderr"], f"{label} metadata.observed_result.stderr"
    )
    if stdout or stderr != f"{TASK_020_FAILURE_SIGNATURE}\n".encode():
        fail(
            f"{label} red fixture metadata streams differ from the accepted TASK-020 RED"
        )
    return RedFixtureObservation(exit_code=1, stdout=stdout, stderr=stderr)


def _red_fixture_metadata(
    repo: GitRepository,
    evidence_commit: str,
    bundle_sha256: str,
    metadata_path: str,
    task_id: str,
    task_base: str,
    command: list[str],
    failure_signature: str,
    patch_path: str,
    patch: bytes,
    test_paths: list[str],
    label: str,
) -> RedFixtureObservation:
    metadata = _json_object_bytes(
        repo.blob(evidence_commit, metadata_path), f"{label} red fixture metadata"
    )
    if task_id == "TASK-020" and metadata.get("schema") != RED_FIXTURE_SCHEMA_V2:
        fail(f"{label} red fixture metadata must use the TASK-020 v2 schema")
    if metadata.get("schema") == RED_FIXTURE_SCHEMA_V2:
        return _red_fixture_metadata_v2(
            repo,
            metadata,
            evidence_commit,
            bundle_sha256,
            task_id,
            task_base,
            command,
            failure_signature,
            patch_path,
            patch,
            test_paths,
            label,
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
    if metadata["schema"] != RED_FIXTURE_SCHEMA_V1:
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
    return RedFixtureObservation(exit_code=exit_code)


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
    bundle_sha256 = _digest(red["bundle_sha256"], f"{label}.red_fixture.bundle_sha256")
    if _bundle_digest(repo, evidence, prefix) != bundle_sha256:
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
    observation = _red_fixture_metadata(
        repo,
        evidence,
        bundle_sha256,
        metadata_path,
        task_id,
        base,
        red_command,
        failure_signature,
        patch_path,
        patch,
        test_paths,
        label,
    )
    if observation.stdout is None or observation.stderr is None:
        red_result = _run_at_commit(repo, base, red_command, patch)
        red_output = red_result.stdout + red_result.stderr
        if (
            red_result.returncode != observation.exit_code
            or red_result.returncode == 0
            or failure_signature not in red_output
        ):
            fail(f"{label}.red_fixture did not reproduce its frozen failure signature")
    else:
        red_result = _run_at_commit(repo, base, red_command, patch, text=False)
        if (
            red_result.returncode != observation.exit_code
            or red_result.stdout != observation.stdout
            or red_result.stderr != observation.stderr
        ):
            fail(f"{label}.red_fixture did not reproduce its frozen binary streams")

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
        _apply_patch(
            reconstructed,
            patch,
            f"{label}.red_fixture patch does not reconstruct",
        )
        for test_path in test_paths:
            reconstructed_bytes = _fixture_bytes(
                reconstructed,
                test_path,
                f"{label} reconstructed fixture {test_path}",
            )
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
    _require_working_path_matches_committed(
        repo,
        repo.root / manifest_path,
        manifest_path,
        repo.blob(head, manifest_path),
        "manifest",
    )
    manifest_commits = str(
        repo.run("log", "HEAD", "--format=%H", "--reverse", "--", manifest_path)
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
    latest_append = append_commits[-1]
    if not repo.is_ancestor(latest_append, head):
        fail("latest phase append is not an ancestor of HEAD")
    if repo.blob(latest_append, manifest_path) != repo.blob(head, manifest_path):
        fail("manifest changed after the latest phase append")
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
