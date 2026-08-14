"""Crash-consistent updates for generated residency inventory documents."""

from __future__ import annotations

import hashlib
import json
import os
import re
import secrets
import stat
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

from .contract import (
    ResidencyInventoryError,
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_string,
)
from .documents import (
    BEGIN_MARKER,
    CAMPAIGN_BEGIN_MARKER,
    CAMPAIGN_END_MARKER,
    END_MARKER,
    replace_marked_block,
    validate_marked_document,
    validate_rendered_projections,
)

try:
    import fcntl
except ImportError:  # pragma: no cover - selected on Windows.
    fcntl = None  # type: ignore[assignment]

try:
    import msvcrt
except ImportError:  # pragma: no cover - selected on POSIX.
    msvcrt = None  # type: ignore[assignment]

JOURNAL_SUFFIX = ".update-v1.json"
JOURNAL_VERSION = 1
TARGET_ORDER = ("matrix", "campaign")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
TRANSACTION_ID_PATTERN = re.compile(r"[0-9a-f]{32}")


@dataclass(frozen=True)
class TransactionTarget:
    """One changed document and its content-addressed transaction state."""

    name: str
    path: Path
    old_sha256: str
    new_sha256: str
    mode: int
    old_artifact: Path
    new_artifact: Path


@dataclass(frozen=True)
class UpdateJournal:
    """Strictly validated durable transaction intent."""

    transaction_id: str
    repository_root: Path
    inventory_path: Path
    phase: str
    targets: tuple[TransactionTarget, ...]


def journal_path_for(inventory_path: Path) -> Path:
    return inventory_path.with_name(f".{inventory_path.name}{JOURNAL_SUFFIX}")


@contextmanager
def inventory_lock(inventory_path: Path, *, exclusive: bool) -> Iterator[None]:
    """Lock validator readers together and exclude a generated-document updater."""

    open_mode = os.O_RDWR if exclusive and msvcrt is not None else os.O_RDONLY
    descriptor = os.open(
        inventory_path,
        open_mode | getattr(os, "O_BINARY", 0) | getattr(os, "O_CLOEXEC", 0),
    )
    windows_locked = False
    try:
        if fcntl is not None:
            operation = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
            fcntl.flock(descriptor, operation)
        elif msvcrt is not None:
            os.lseek(descriptor, 0, os.SEEK_SET)
            operation = msvcrt.LK_LOCK if exclusive else msvcrt.LK_RLCK
            msvcrt.locking(descriptor, operation, 1)
            windows_locked = True
        else:
            fail("portable advisory locking is unavailable")
        yield
    finally:
        try:
            if windows_locked:
                os.lseek(descriptor, 0, os.SEEK_SET)
                msvcrt.locking(descriptor, msvcrt.LK_UNLCK, 1)
        finally:
            os.close(descriptor)


def require_no_pending_update(inventory_path: Path) -> None:
    if journal_path_for(inventory_path).exists():
        fail(
            "pending generated-document update transaction; run the validator with --update to recover it"
        )


def _sha256(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def _path_sha256(path: Path) -> str:
    return _sha256(path.read_bytes())


def _artifact_path(path: Path, transaction_id: str, kind: str) -> Path:
    return path.with_name(f".{path.name}.residency-{transaction_id}.{kind}")


def _repository_relative_path(repository_root: Path, path: Path, label: str) -> str:
    try:
        relative = path.relative_to(repository_root)
    except ValueError:
        fail(f"{label} must be inside the current repository")
    value = relative.as_posix()
    if not value or value == ".":
        fail(f"{label} must identify a repository file")
    return value


def _recorded_repository_path(repository_root: Path, value: Any, label: str) -> Path:
    recorded = require_string(value, label)
    relative = PurePosixPath(recorded)
    if (
        relative.is_absolute()
        or recorded != relative.as_posix()
        or "\\" in recorded
        or any(part in {"", ".", ".."} for part in relative.parts)
        or (relative.parts and ":" in relative.parts[0])
    ):
        fail(f"{label} must be a canonical repository-relative path")
    resolved_root = repository_root.resolve()
    resolved = (resolved_root / Path(*relative.parts)).resolve()
    if not resolved.is_relative_to(resolved_root):
        fail(f"{label} must be a canonical repository-relative path")
    return resolved


def _role_path_mapping(
    repository_root: Path, role: str, path: Path, label: str
) -> dict[str, str]:
    return {
        "path": _repository_relative_path(repository_root, path, label),
        "role": role,
    }


def _target_mapping(repository_root: Path, target: TransactionTarget) -> dict[str, Any]:
    return {
        "mode": target.mode,
        "new_sha256": target.new_sha256,
        "old_sha256": target.old_sha256,
        **_role_path_mapping(
            repository_root,
            target.name,
            target.path,
            f"transaction target {target.name}",
        ),
    }


def _journal_mapping(journal: UpdateJournal) -> dict[str, Any]:
    return {
        "inventory": _role_path_mapping(
            journal.repository_root,
            "inventory",
            journal.inventory_path,
            "transaction inventory",
        ),
        "phase": journal.phase,
        "targets": [
            _target_mapping(journal.repository_root, target)
            for target in journal.targets
        ],
        "transaction_id": journal.transaction_id,
        "version": JOURNAL_VERSION,
    }


def _strict_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"transaction journal contains duplicate object key {key!r}")
        result[key] = value
    return result


def _write_all(descriptor: int, content: bytes) -> None:
    view = memoryview(content)
    while view:
        written = os.write(descriptor, view)
        if written == 0:
            raise OSError("short write while persisting generated-document transaction")
        view = view[written:]


def _write_exclusive(path: Path, content: bytes, mode: int) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
        mode,
    )
    try:
        os.fchmod(descriptor, mode)
        _write_all(descriptor, content)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(
        path,
        os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_CLOEXEC", 0),
    )
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _sync_directories(paths: set[Path]) -> None:
    for path in sorted(paths, key=str):
        _fsync_directory(path)


def _require_durable_update_support(
    inventory_path: Path, directories: set[Path]
) -> None:
    """Prove required file and directory synchronization before staging state."""

    descriptor: int | None = None
    try:
        descriptor = os.open(inventory_path, os.O_RDONLY | getattr(os, "O_CLOEXEC", 0))
        os.fsync(descriptor)
        _sync_directories(directories)
    except (AttributeError, OSError) as error:
        fail(f"durable generated-document updates are unsupported: {error}")
    finally:
        if descriptor is not None:
            os.close(descriptor)


def _write_journal(
    path: Path, journal: UpdateJournal, *, replace_existing: bool
) -> None:
    content = (
        json.dumps(_journal_mapping(journal), indent=2, sort_keys=True) + "\n"
    ).encode()
    temporary_path = path.with_name(
        f".{path.name}.residency-{journal.transaction_id}.tmp"
    )
    try:
        _write_exclusive(temporary_path, content, 0o600)
        if replace_existing:
            os.replace(temporary_path, path)
        else:
            os.link(temporary_path, path)
        _fsync_directory(path.parent)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()
            _fsync_directory(path.parent)


def _require_sha256(value: Any, label: str) -> str:
    value = require_string(value, label)
    if SHA256_PATTERN.fullmatch(value) is None:
        fail(f"{label} must be a lowercase SHA-256 digest")
    return value


def _parse_target(
    value: Any,
    label: str,
    repository_root: Path,
    transaction_id: str,
    expected_name: str,
    expected_path: Path,
) -> TransactionTarget:
    mapping = require_mapping(value, label)
    require_exact_keys(
        mapping,
        {
            "mode",
            "new_sha256",
            "old_sha256",
            "path",
            "role",
        },
        label,
    )
    name = require_string(mapping["role"], f"{label}.role")
    path = _recorded_repository_path(repository_root, mapping["path"], f"{label}.path")
    mode = mapping["mode"]
    if isinstance(mode, bool) or not isinstance(mode, int) or not 0 <= mode <= 0o7777:
        fail(f"{label}.mode must be a valid permission mode")
    if name != expected_name or path != expected_path:
        fail(f"{label} does not match the requested generated document")
    return TransactionTarget(
        name=name,
        path=path,
        old_sha256=_require_sha256(mapping["old_sha256"], f"{label}.old_sha256"),
        new_sha256=_require_sha256(mapping["new_sha256"], f"{label}.new_sha256"),
        mode=mode,
        old_artifact=_artifact_path(path, transaction_id, "old"),
        new_artifact=_artifact_path(path, transaction_id, "new"),
    )


def _parse_journal_header(
    mapping: dict[str, Any], repository_root: Path, inventory_path: Path
) -> tuple[str, str]:
    if (
        isinstance(mapping["version"], bool)
        or not isinstance(mapping["version"], int)
        or mapping["version"] != JOURNAL_VERSION
    ):
        fail("generated-document transaction journal has an unsupported version")
    transaction_id = require_string(
        mapping["transaction_id"], "transaction journal transaction_id"
    )
    if TRANSACTION_ID_PATTERN.fullmatch(transaction_id) is None:
        fail("transaction journal transaction_id must be 32 lowercase hex characters")
    inventory_mapping = require_mapping(
        mapping["inventory"], "transaction journal inventory"
    )
    require_exact_keys(
        inventory_mapping,
        {"path", "role"},
        "transaction journal inventory",
    )
    inventory_role = require_string(
        inventory_mapping["role"], "transaction journal inventory.role"
    )
    recorded_inventory = _recorded_repository_path(
        repository_root,
        inventory_mapping["path"],
        "transaction journal inventory.path",
    )
    if inventory_role != "inventory" or recorded_inventory != inventory_path:
        fail("generated-document transaction journal belongs to another inventory")
    phase = require_string(mapping["phase"], "transaction journal phase")
    if phase not in {"staging", "prepared"}:
        fail("transaction journal phase must be staging or prepared")
    return transaction_id, phase


def _parse_journal_targets(
    values: Any,
    repository_root: Path,
    transaction_id: str,
    matrix_path: Path,
    campaign_path: Path,
) -> tuple[TransactionTarget, ...]:
    target_values = require_list(values, "transaction journal targets")
    if not 1 <= len(target_values) <= len(TARGET_ORDER):
        fail("transaction journal targets must contain one or two documents")
    expected_paths = {"matrix": matrix_path, "campaign": campaign_path}
    names: list[str] = []
    targets: list[TransactionTarget] = []
    for index, target_value in enumerate(target_values):
        label = f"transaction journal targets[{index}]"
        target_mapping = require_mapping(target_value, label)
        name = target_mapping.get("role")
        if not isinstance(name, str) or name not in expected_paths:
            fail(f"{label}.role is unknown")
        names.append(name)
        targets.append(
            _parse_target(
                target_mapping,
                label,
                repository_root,
                transaction_id,
                name,
                expected_paths[name],
            )
        )
    if len(set(names)) != len(names):
        fail("transaction journal contains duplicate targets")
    if names != sorted(names, key=TARGET_ORDER.index):
        fail("transaction journal targets are not in deterministic order")
    if any(target.old_sha256 == target.new_sha256 for target in targets):
        fail("transaction journal targets must describe changed documents")
    return tuple(targets)


def _read_journal(
    repository_root: Path,
    inventory_path: Path,
    matrix_path: Path,
    campaign_path: Path,
) -> UpdateJournal:
    path = journal_path_for(inventory_path)
    try:
        value = json.loads(
            path.read_text(encoding="utf-8"), object_pairs_hook=_strict_json_object
        )
    except json.JSONDecodeError as error:
        fail(f"generated-document transaction journal is invalid JSON: {error}")
    mapping = require_mapping(value, "generated-document transaction journal")
    require_exact_keys(
        mapping,
        {"inventory", "phase", "targets", "transaction_id", "version"},
        "generated-document transaction journal",
    )
    transaction_id, phase = _parse_journal_header(
        mapping, repository_root, inventory_path
    )
    targets = _parse_journal_targets(
        mapping["targets"],
        repository_root,
        transaction_id,
        matrix_path,
        campaign_path,
    )
    return UpdateJournal(
        transaction_id=transaction_id,
        repository_root=repository_root,
        inventory_path=inventory_path,
        phase=phase,
        targets=targets,
    )


def _unlink_if_present(path: Path) -> bool:
    try:
        path.unlink()
    except FileNotFoundError:
        return False
    return True


def _cleanup_transaction(journal_path: Path, journal: UpdateJournal) -> None:
    changed_directories: set[Path] = set()
    for target in journal.targets:
        for artifact in (target.old_artifact, target.new_artifact):
            if _unlink_if_present(artifact):
                changed_directories.add(artifact.parent)
    if changed_directories:
        _sync_directories(changed_directories)
    if _unlink_if_present(journal_path):
        _fsync_directory(journal_path.parent)


def _require_regular_file(path: Path, label: str) -> os.stat_result:
    metadata = path.lstat()
    if not stat.S_ISREG(metadata.st_mode):
        fail(f"{label} must be a regular file")
    return metadata


def _require_current_target_state(target: TransactionTarget) -> str:
    metadata = _require_regular_file(
        target.path, f"generated-document target {target.name}"
    )
    current_sha256 = _path_sha256(target.path)
    if current_sha256 not in {target.old_sha256, target.new_sha256}:
        fail(
            f"refusing generated-document recovery after foreign edit to {target.name}"
        )
    current_mode = stat.S_IMODE(metadata.st_mode)
    if current_mode != target.mode:
        fail(
            f"refusing generated-document recovery after foreign mode edit to {target.name}"
        )
    return current_sha256


def _require_artifact(
    path: Path, expected_sha256: str, expected_mode: int, label: str
) -> None:
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        fail(f"generated-document recovery is missing {label} artifact")
    if not stat.S_ISREG(metadata.st_mode):
        fail(f"generated-document recovery found a non-regular {label} artifact")
    if stat.S_IMODE(metadata.st_mode) != expected_mode:
        fail(f"generated-document recovery found a mode-changed {label} artifact")
    if _path_sha256(path) != expected_sha256:
        fail(f"generated-document recovery found a corrupt {label} artifact")


def _roll_forward_prepared(journal: UpdateJournal) -> None:
    for target in journal.targets:
        current_sha256 = _require_current_target_state(target)
        if current_sha256 == target.old_sha256:
            _require_artifact(
                target.new_artifact,
                target.new_sha256,
                target.mode,
                target.name,
            )
            os.replace(target.new_artifact, target.path)
            _fsync_directory(target.path.parent)
    for target in journal.targets:
        if _require_current_target_state(target) != target.new_sha256:
            fail(f"generated-document recovery did not commit {target.name}")


def _rollback_prepared(journal: UpdateJournal) -> None:
    for target in reversed(journal.targets):
        current_sha256 = _require_current_target_state(target)
        if current_sha256 == target.new_sha256:
            _require_artifact(
                target.old_artifact,
                target.old_sha256,
                target.mode,
                target.name,
            )
            os.replace(target.old_artifact, target.path)
            _fsync_directory(target.path.parent)
    for target in journal.targets:
        if _require_current_target_state(target) != target.old_sha256:
            fail(f"generated-document recovery did not roll back {target.name}")


def recover_pending_update(
    repository_root: Path,
    inventory_path: Path,
    matrix_path: Path,
    campaign_path: Path,
) -> bool:
    """Recover a validated pending transaction while the inventory lock is held."""

    path = journal_path_for(inventory_path)
    if not path.exists():
        return False
    journal = _read_journal(repository_root, inventory_path, matrix_path, campaign_path)
    if journal.phase == "staging":
        _cleanup_transaction(path, journal)
        return True

    try:
        _roll_forward_prepared(journal)
    except (OSError, ResidencyInventoryError) as roll_forward_error:
        try:
            _rollback_prepared(journal)
        except (OSError, ResidencyInventoryError) as rollback_error:
            fail(
                "generated-document transaction recovery failed: "
                f"roll-forward failed: {roll_forward_error}; "
                f"rollback failed: {rollback_error}"
            )
    _cleanup_transaction(path, journal)
    return True


def _changed_target(
    name: str, path: Path, old_content: bytes, new_content: bytes, transaction_id: str
) -> TransactionTarget:
    return TransactionTarget(
        name=name,
        path=path,
        old_sha256=_sha256(old_content),
        new_sha256=_sha256(new_content),
        mode=stat.S_IMODE(path.stat().st_mode),
        old_artifact=_artifact_path(path, transaction_id, "old"),
        new_artifact=_artifact_path(path, transaction_id, "new"),
    )


def _stage_artifacts(
    journal: UpdateJournal, contents: dict[str, tuple[bytes, bytes]]
) -> None:
    for target in journal.targets:
        old_content, new_content = contents[target.name]
        _write_exclusive(target.old_artifact, old_content, target.mode)
        _write_exclusive(target.new_artifact, new_content, target.mode)
    _sync_directories({target.path.parent for target in journal.targets})


def _require_unchanged_sources(journal: UpdateJournal) -> None:
    for target in journal.targets:
        metadata = _require_regular_file(
            target.path, f"generated-document target {target.name}"
        )
        if _path_sha256(target.path) != target.old_sha256:
            fail(
                f"refusing generated-document update after foreign edit to {target.name}"
            )
        if stat.S_IMODE(metadata.st_mode) != target.mode:
            fail(
                f"refusing generated-document update after foreign mode edit to {target.name}"
            )


def _updated_document_text(
    matrix_path: Path,
    campaign_path: Path,
    rendered_inventory: str,
    rendered_campaign: str,
) -> tuple[dict[str, str], dict[str, str]]:
    original: dict[str, str] = {}
    for name, path in (("matrix", matrix_path), ("campaign", campaign_path)):
        with path.open(encoding="utf-8", newline="") as stream:
            original[name] = stream.read()
    updated = {
        "matrix": replace_marked_block(
            original["matrix"],
            rendered_inventory,
            BEGIN_MARKER,
            END_MARKER,
            "support inventory",
        ),
        "campaign": replace_marked_block(
            original["campaign"],
            rendered_campaign,
            CAMPAIGN_BEGIN_MARKER,
            CAMPAIGN_END_MARKER,
            "Hatchery exact cells",
        ),
    }
    validate_marked_document(
        updated["matrix"],
        rendered_inventory,
        BEGIN_MARKER,
        END_MARKER,
        "support inventory",
    )
    validate_marked_document(
        updated["campaign"],
        rendered_campaign,
        CAMPAIGN_BEGIN_MARKER,
        CAMPAIGN_END_MARKER,
        "Hatchery exact cells",
    )
    return original, updated


def _new_staging_journal(
    repository_root: Path,
    inventory_path: Path,
    matrix_path: Path,
    campaign_path: Path,
    original: dict[str, str],
    updated: dict[str, str],
) -> tuple[UpdateJournal, dict[str, tuple[bytes, bytes]]] | None:
    changed_names = [name for name in TARGET_ORDER if updated[name] != original[name]]
    if not changed_names:
        return None
    transaction_id = secrets.token_hex(16)
    paths = {"matrix": matrix_path, "campaign": campaign_path}
    contents = {
        name: (original[name].encode(), updated[name].encode())
        for name in changed_names
    }
    targets = tuple(
        _changed_target(
            name,
            paths[name],
            contents[name][0],
            contents[name][1],
            transaction_id,
        )
        for name in changed_names
    )
    return (
        UpdateJournal(
            transaction_id=transaction_id,
            repository_root=repository_root,
            inventory_path=inventory_path,
            phase="staging",
            targets=targets,
        ),
        contents,
    )


def _prepared_journal(staging: UpdateJournal) -> UpdateJournal:
    return UpdateJournal(
        transaction_id=staging.transaction_id,
        repository_root=staging.repository_root,
        inventory_path=staging.inventory_path,
        phase="prepared",
        targets=staging.targets,
    )


def _commit_prepared(path: Path, prepared: UpdateJournal) -> None:
    _roll_forward_prepared(prepared)
    _cleanup_transaction(path, prepared)


def _recover_owned_transaction(
    error: Exception,
    staging: UpdateJournal,
    path: Path,
    matrix_path: Path,
    campaign_path: Path,
) -> None:
    try:
        if path.exists():
            pending = _read_journal(
                staging.repository_root,
                staging.inventory_path,
                matrix_path,
                campaign_path,
            )
            if pending.transaction_id != staging.transaction_id:
                fail("refusing to recover a foreign generated-document transaction")
        recovered = recover_pending_update(
            staging.repository_root,
            staging.inventory_path,
            matrix_path,
            campaign_path,
        )
    except (OSError, ResidencyInventoryError) as recovery_error:
        fail(
            f"{error}; generated-document transaction recovery failed: {recovery_error}"
        )
    if not recovered and path.exists():
        fail(
            f"{error}; generated-document transaction recovery did not consume its journal"
        )


def update_generated_documents(
    repository_root: Path,
    inventory_path: Path,
    matrix_path: Path,
    campaign_path: Path,
    rendered_inventory: str,
    rendered_campaign: str,
) -> None:
    """Run a locked, crash-consistent two-document update.

    Validator clients share the inventory lock. Independent raw readers are not
    isolated between the two durable document replacements.
    """

    require_distinct_paths(inventory_path, matrix_path, campaign_path)
    recover_pending_update(repository_root, inventory_path, matrix_path, campaign_path)
    validate_rendered_projections(rendered_inventory, rendered_campaign)
    original, updated = _updated_document_text(
        matrix_path,
        campaign_path,
        rendered_inventory,
        rendered_campaign,
    )
    transaction = _new_staging_journal(
        repository_root,
        inventory_path,
        matrix_path,
        campaign_path,
        original,
        updated,
    )
    if transaction is None:
        return

    _require_durable_update_support(
        inventory_path,
        {inventory_path.parent, matrix_path.parent, campaign_path.parent},
    )
    staging, contents = transaction
    path = journal_path_for(inventory_path)
    try:
        _write_journal(path, staging, replace_existing=False)
        _stage_artifacts(staging, contents)
        _require_unchanged_sources(staging)
        prepared = _prepared_journal(staging)
        _write_journal(path, prepared, replace_existing=True)
        _commit_prepared(path, prepared)
    except Exception as error:
        _recover_owned_transaction(
            error,
            staging,
            path,
            matrix_path,
            campaign_path,
        )
        raise


def require_distinct_paths(
    inventory_path: Path, matrix_path: Path, campaign_path: Path
) -> None:
    """Reject lexical and filesystem aliases before opening transaction state."""

    paths = (inventory_path, matrix_path, campaign_path)
    if len(set(paths)) != len(paths):
        fail("inventory, --matrix, and --campaign must identify different files")
    for index, left in enumerate(paths):
        for right in paths[index + 1 :]:
            try:
                aliases = left.samefile(right)
            except FileNotFoundError:
                aliases = False
            if aliases:
                fail(
                    "inventory, --matrix, and --campaign must identify different files"
                )
