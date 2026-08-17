import hashlib
import importlib.util
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

FAILURE = "TASK-014 durable-root and process-ownership prototype is unavailable"
TASK_BASE = "63619c3cc45549fd277d0a83802074b54b9f4c24"


def fail_unavailable() -> None:
    print(FAILURE, file=sys.stderr)
    raise SystemExit(1)


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def load_result_contract(repo_root: Path):
    path = repo_root / "test/residency/prototypes/result_contract.py"
    if not path.is_file():
        fail_unavailable()
    spec = importlib.util.spec_from_file_location("prototype_result_contract", path)
    if spec is None or spec.loader is None:
        fail_unavailable()
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_json_object(raw: bytes) -> dict:
    def object_from_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise AssertionError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=object_from_pairs)
    require(type(value) is dict, "prototype result must be an object")
    return value


def compiler_command(
    compiler: str, source: str, output: str, platform_id: str
) -> list[str]:
    if platform_id == "windows":
        return [
            compiler,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            source,
            f"/Fe:{output}",
        ]
    return [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-pthread",
        source,
        "-o",
        output,
    ]


def normalized_architecture() -> str:
    machine = platform.machine().strip().lower()
    return {
        "amd64": "x86_64",
        "arm64": "aarch64",
        "x64": "x86_64",
    }.get(machine, machine)


def compiler_version(compiler: str, platform_id: str) -> str:
    compiler_name = Path(compiler).name.lower()
    command = (
        [compiler]
        if platform_id == "windows" and compiler_name in {"cl", "cl.exe"}
        else [compiler, "--version"]
    )
    completed = subprocess.run(
        command, check=False, capture_output=True, text=True, timeout=30
    )
    lines = [
        line.strip()
        for line in (completed.stdout + completed.stderr).splitlines()
        if line.strip()
    ]
    require(lines, f"recorded compiler {compiler} emitted no version")
    return lines[0]


def clone_json(value):
    return json.loads(json.dumps(value))


def require_contract_rejects(
    contract, repo_root: Path, result, mutate, label: str
) -> None:
    candidate = clone_json(result)
    mutate(candidate, contract)
    try:
        contract.validate_result(repo_root, candidate)
    except contract.PrototypeResultError:
        return
    raise AssertionError(f"result contract accepted {label}")


def mutate_arbitrary_evidence_token(candidate, _contract) -> None:
    candidate["claims"][0]["evidence"] = ["containment.escape_detected"]


def mutate_missing_observation(candidate, _contract) -> None:
    candidate["observations"] = []


def mutate_duplicate_observation(candidate, _contract) -> None:
    candidate["observations"].append(clone_json(candidate["observations"][0]))


def mutate_unreferenced_observation(candidate, contract) -> None:
    extra = clone_json(candidate["observations"][0])
    extra["toolchain"]["version"] += " unreferenced-negative-case"
    extra["id"] = contract.observation_address(extra)
    candidate["observations"].append(extra)
    candidate["observations"].sort(key=lambda item: item["id"])


def mutate_duplicate_evidence_observation(candidate, _contract) -> None:
    evidence = candidate["claims"][0]["evidence"]
    evidence.append(clone_json(evidence[0]))


def mutate_arbitrary_observation_row(candidate, _contract) -> None:
    rows = candidate["claims"][0]["evidence"][0]["rows"]
    rows.append("arbitrary.evidence=passed")
    rows.sort()


def mutate_mismatched_output_digest(candidate, _contract) -> None:
    candidate["observations"][0]["output"]["stdout_sha256"] = "0" * 64


def mutate_nonzero_evidence_observation(candidate, contract) -> None:
    observation = candidate["observations"][0]
    old_id = observation["id"]
    observation["exit_code"] = 1
    observation["id"] = contract.observation_address(observation)
    for claim in candidate["claims"]:
        for evidence in claim["evidence"]:
            if evidence["observation_id"] == old_id:
                evidence["observation_id"] = observation["id"]


def mutate_cross_wired_fallback(candidate, _contract) -> None:
    claim = next(
        item
        for item in candidate["claims"]
        if item["id"] == "linux_crash_durable_root_publication"
    )
    first, second = claim["fallback_bindings"]
    first["fallbacks"], second["fallbacks"] = (
        second["fallbacks"],
        first["fallbacks"],
    )


NEGATIVE_CASES = (
    (mutate_arbitrary_evidence_token, "an arbitrary evidence token"),
    (mutate_missing_observation, "a missing observation ID"),
    (mutate_duplicate_observation, "a duplicate observation ID"),
    (mutate_unreferenced_observation, "an unreferenced observation ID"),
    (mutate_duplicate_evidence_observation, "a duplicate evidence observation ID"),
    (mutate_arbitrary_observation_row, "an unobserved evidence row"),
    (mutate_mismatched_output_digest, "a mismatched observed-output digest"),
    (mutate_nonzero_evidence_observation, "a nonzero-exit evidence observation"),
    (mutate_cross_wired_fallback, "a union-preserving cross-wired fallback mapping"),
)


def require_contract_negative_cases(contract, repo_root: Path, result) -> None:
    for mutate, label in NEGATIVE_CASES:
        require_contract_rejects(contract, repo_root, result, mutate, label)


def require_result(repo_root: Path):
    contract = load_result_contract(repo_root)
    result = contract.load_task_result(repo_root, "TASK-014")
    result_root = repo_root / "docs/research/residency-prototype-results/sha256"
    matches = []
    for path in sorted(result_root.glob("*.json")):
        raw = path.read_bytes()
        require(
            path.stem == hashlib.sha256(raw).hexdigest(),
            f"{path.name} is not addressed by bytes",
        )
        candidate = parse_json_object(raw)
        if candidate.get("task_id") == "TASK-014":
            matches.append(candidate)
    require(matches == [result], "TASK-014 must have one byte-addressed result")
    canonical = (
        json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    require(
        any(path.read_bytes() == canonical for path in result_root.glob("*.json")),
        "TASK-014 result is not canonical UTF-8 JSON",
    )
    require(
        set(result)
        == {
            "schema",
            "task_id",
            "prototype_id",
            "task_base_commit",
            "source",
            "outcome",
            "runtime_authority",
            "fallback_state",
            "observations",
            "claims",
        },
        "TASK-014 result fields are not closed",
    )
    require(
        result["schema"] == "residency_prototype_result/v1", "result schema changed"
    )
    require(
        result["task_base_commit"] == TASK_BASE, "TASK-014 result changed its task base"
    )
    require(
        result["prototype_id"] == "durable_root_and_process_ownership",
        "TASK-014 result changed its prototype identity",
    )
    source = repo_root / result["source"]["path"]
    require(
        result["source"]
        == {
            "path": "test/cpp/test_residency_prototype_task014.cpp",
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        },
        "TASK-014 result does not bind its source bytes",
    )
    require(result["outcome"] == "mixed", "TASK-014 result must remain mixed")
    require(
        result["runtime_authority"] == "none", "prototype granted runtime authority"
    )
    require(
        result["fallback_state"] == {"authority": "legacy_runtime", "status": "active"},
        "prototype changed the active fallback authority",
    )

    observations = result["observations"]
    require(len(observations) == 1, "TASK-014 must publish one native observation")
    observation = observations[0]
    observation_id = observation["id"]
    require(
        observation["environment"]["platform"] == "linux",
        "TASK-014 initial native observation must remain Linux-scoped",
    )
    require(
        "platform.current=linux" in observation["output"]["rows"],
        "TASK-014 observation does not bind its executed platform",
    )

    hatchery_units = [
        "H-ROCM-REC-GTT-HOST-OWN-v1",
        "H-VULKAN-REC-GTT-HOST-OWN-v1",
    ]
    windows_units = [
        "W-XDNA2-FLM-NPU-EMBEDDING-REC-v1",
        "W-XDNA2-FLM-NPU-LLM-REC-v1",
        "W-XDNA2-FLM-NPU-TRANSCRIPTION-REC-v1",
        "W-XDNA2-RYZENAI-LLM-NPU-LLM-REC-v1",
        "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-REC-v1",
    ]
    hatchery_bindings = [
        {
            "unit_id": "H-ROCM-REC-GTT-HOST-OWN-v1",
            "fallbacks": {
                "unproven_release": "hatchery_rocm_recovery_block_readiness_v1"
            },
        },
        {
            "unit_id": "H-VULKAN-REC-GTT-HOST-OWN-v1",
            "fallbacks": {
                "unproven_release": "residency_recovery_block_unproven_release_v1"
            },
        },
    ]
    windows_bindings = [
        {
            "unit_id": unit_id,
            "fallbacks": {
                "unproven_release": "residency_recovery_block_unproven_release_v1"
            },
        }
        for unit_id in windows_units
    ]
    hatchery_fallbacks = [
        "hatchery_rocm_recovery_block_readiness_v1",
        "residency_recovery_block_unproven_release_v1",
    ]
    generic_fallback = ["residency_recovery_block_unproven_release_v1"]

    expected = {
        "linux_complete_descendant_containment": {
            "status": "fallback",
            "platforms": ["linux"],
            "units": hatchery_units,
            "fallbacks": hatchery_fallbacks,
            "bindings": hatchery_bindings,
            "rows": [
                "containment.escape_detected=passed",
                "process_containment=fallback",
            ],
        },
        "linux_cooperative_process_group": {
            "status": "passed",
            "platforms": ["linux"],
            "units": hatchery_units,
            "fallbacks": hatchery_fallbacks,
            "bindings": hatchery_bindings,
            "rows": [
                "identity.birth_token=passed",
                "identity.mismatch_signal=blocked",
                "membership.descendant=passed",
                "membership.direct=passed",
                "ownership.prepared_before_spawn=passed",
                "termination.containment=passed",
                "termination.membership_empty=passed",
            ],
        },
        "linux_crash_durable_root_publication": {
            "status": "passed",
            "platforms": ["linux"],
            "units": hatchery_units,
            "fallbacks": hatchery_fallbacks,
            "bindings": hatchery_bindings,
            "rows": [
                "durable.corrupt_candidate_publication=blocked",
                "durable.crash_after_replace_complete_root_valid=passed",
                "durable.crash_before_replace_old_root_valid=passed",
                "durable.flush_failure_publication=blocked",
                "durable.parent_flushed=passed",
                "durable.root_replaced=passed",
                "durable.stage_file_flushed=passed",
            ],
        },
        "macos_crash_durable_root_publication": {
            "status": "deferred",
            "platforms": ["macos"],
            "units": [],
            "fallbacks": generic_fallback,
            "bindings": [],
            "rows": ["platform.current=linux"],
        },
        "macos_native_subprocess_tree": {
            "status": "deferred",
            "platforms": ["macos"],
            "units": [],
            "fallbacks": generic_fallback,
            "bindings": [],
            "rows": ["platform.current=linux"],
        },
        "windows_crash_durable_root_publication": {
            "status": "deferred",
            "platforms": ["windows"],
            "units": windows_units,
            "fallbacks": generic_fallback,
            "bindings": windows_bindings,
            "rows": ["platform.current=linux"],
        },
        "windows_job_object_tree": {
            "status": "deferred",
            "platforms": ["windows"],
            "units": windows_units,
            "fallbacks": generic_fallback,
            "bindings": windows_bindings,
            "rows": ["platform.current=linux"],
        },
    }
    claims = {claim["id"]: claim for claim in result["claims"]}
    require(len(claims) == len(result["claims"]), "TASK-014 claim IDs must be unique")
    require(set(claims) == set(expected), "TASK-014 result changed its claim closure")
    for claim_id, contract_row in expected.items():
        claim = claims[claim_id]
        require(
            set(claim)
            == {
                "id",
                "status",
                "platforms",
                "affected_units",
                "fallback_ids",
                "fallback_bindings",
                "evidence",
                "limitations",
            },
            f"{claim_id} fields are not closed",
        )
        require(claim["status"] == contract_row["status"], f"{claim_id} changed status")
        require(
            claim["platforms"] == contract_row["platforms"],
            f"{claim_id} changed platform scope",
        )
        require(
            claim["affected_units"] == contract_row["units"],
            f"{claim_id} changed its promotion-unit closure",
        )
        require(
            claim["fallback_ids"] == contract_row["fallbacks"],
            f"{claim_id} changed its fallback closure",
        )
        require(
            claim["fallback_bindings"] == contract_row["bindings"],
            f"{claim_id} cross-wired an inventory fallback",
        )
        require(
            claim["evidence"]
            == [
                {
                    "observation_id": observation_id,
                    "rows": contract_row["rows"],
                }
            ],
            f"{claim_id} changed its exact observed evidence",
        )
        require(claim["limitations"], f"{claim_id} has no bounded limitation")
    fallbacks = {
        fallback for claim in result["claims"] for fallback in claim["fallback_ids"]
    }
    require(
        fallbacks
        == {
            "hatchery_rocm_recovery_block_readiness_v1",
            "residency_recovery_block_unproven_release_v1",
        },
        "TASK-014 result changed its catalog fallback closure",
    )
    require_contract_negative_cases(contract, repo_root, result)
    return contract, result


def current_platform() -> str:
    return {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(
        platform.system(), ""
    )


def parse_probe_output(stdout: bytes) -> tuple[list[str], dict[str, str]]:
    require(stdout.endswith(b"\n"), "native probe stdout lacks its final newline")
    text = stdout.decode("utf-8")
    lines = text.splitlines()
    require(lines, "native probe emitted no observation rows")
    rows = {}
    for line in lines:
        require(line.count("=") == 1, f"native probe emitted a malformed row: {line}")
        key, value = line.split("=", 1)
        require(key and value, f"native probe emitted an empty row field: {line}")
        require(key not in rows, f"native probe emitted duplicate row: {key}")
        rows[key] = value
    return lines, rows


SOURCE_TOKENS = (
    "fsync",
    "rename",
    "FlushFileBuffers",
    "MoveFileExW",
    "MOVEFILE_REPLACE_EXISTING",
    "MOVEFILE_WRITE_THROUGH",
    "setpgid",
    "/proc/sys/kernel/random/boot_id",
    "CreateJobObjectW",
    "AssignProcessToJobObject",
    "CREATE_SUSPENDED",
    "GetProcessTimes",
    "TerminateJobObject",
    "QueryInformationJobObject",
)
EXPECTED_ROW_KEYS = {
    "durable_root_publication",
    "durable.crash_before_replace_old_root_valid",
    "durable.crash_after_replace_complete_root_valid",
    "durable.flush_failure_publication",
    "durable.corrupt_candidate_publication",
    "durable.stage_file_flushed",
    "durable.root_replaced",
    "durable.parent_flushed",
    "ownership.prepared_before_spawn",
    "identity.birth_token",
    "membership.direct",
    "membership.descendant",
    "identity.mismatch_signal",
    "termination.containment",
    "termination.membership_empty",
    "containment.escape_detected",
    "process_containment",
    "platform.current",
    "runtime_authority",
}
DURABLE_CRASH_ROWS = {
    "durable.crash_before_replace_old_root_valid": "passed",
    "durable.crash_after_replace_complete_root_valid": "passed",
    "durable.flush_failure_publication": "blocked",
    "durable.corrupt_candidate_publication": "blocked",
}


def require_probe_source(source: Path) -> None:
    source_text = source.read_text(encoding="utf-8")
    for token in SOURCE_TOKENS:
        require(token in source_text, f"native probe omits {token}")


def recorded_observation_for_platform(result, platform_id: str):
    observed_ids = {
        evidence["observation_id"]
        for claim in result["claims"]
        if claim["status"] in {"passed", "fallback"}
        and platform_id in claim["platforms"]
        for evidence in claim["evidence"]
    }
    if not observed_ids:
        return None
    observations = [
        observation
        for observation in result["observations"]
        if observation["id"] in observed_ids
        and observation["environment"]["platform"] == platform_id
    ]
    require(
        len(observations) == 1,
        "current passed/fallback claims do not select one recorded observation",
    )
    return observations[0]


def run_native_probe(repo_root: Path, source: Path, recorded_observation):
    platform_id = current_platform()
    require(platform_id, "native probe ran on an unsupported platform")
    compiler = (
        recorded_observation["toolchain"]["compiler"]
        if recorded_observation is not None
        else os.environ.get("CXX", "cl" if platform_id == "windows" else "c++")
    )
    version = compiler_version(compiler, platform_id)
    suffix = ".exe" if platform_id == "windows" else ""
    executable_name = f"task014{suffix}"
    logical_source = source.relative_to(repo_root).as_posix()
    logical_output = f"$TMPDIR/{executable_name}"
    logical_compile_command = compiler_command(
        compiler, logical_source, logical_output, platform_id
    )
    with tempfile.TemporaryDirectory(prefix="residency-task014-") as directory:
        executable = Path(directory) / executable_name
        actual_compile_command = compiler_command(
            compiler, str(source), str(executable), platform_id
        )
        subprocess.run(actual_compile_command, check=True, timeout=30)
        run_command = [
            (
                f".{os.sep}{executable.name}"
                if platform_id == "windows"
                else f"./{executable.name}"
            )
        ]
        completed = subprocess.run(
            run_command,
            cwd=directory,
            check=False,
            capture_output=True,
            timeout=30,
        )
    lines, rows = parse_probe_output(completed.stdout)
    binding = {
        "compile_command": logical_compile_command,
        "command": run_command,
        "environment": {
            "platform": platform_id,
            "architecture": normalized_architecture(),
        },
        "toolchain": {"compiler": compiler, "version": version},
        "exit_code": completed.returncode,
        "output": {
            "stdout_sha256": hashlib.sha256(completed.stdout).hexdigest(),
            "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest(),
            "rows": lines,
        },
    }
    return completed, rows, binding


def require_unsupported_directory_sync_deferred(source: Path, compiler: str) -> None:
    if platform.system() != "Linux":
        return
    interceptor_source = r"""
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

int fsync(int file) {
    static int directory_sync_interrupted = 0;
    struct stat metadata;
    const char* root = getenv("TASK014_DIRECTORY_SYNC_REJECT_ROOT");
    if (root != NULL && fstat(file, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
        char link_path[64];
        char target[4096];
        const int link_length = snprintf(
            link_path, sizeof(link_path), "/proc/self/fd/%d", file);
        if (link_length > 0 && (size_t)link_length < sizeof(link_path)) {
            const ssize_t target_length = readlink(
                link_path, target, sizeof(target) - 1);
            if (target_length >= 0) {
                target[target_length] = '\0';
                const size_t root_length = strlen(root);
                if (strncmp(target, root, root_length) == 0 &&
                    target[root_length] == '/') {
                    if (directory_sync_interrupted == 0) {
                        directory_sync_interrupted = 1;
                        errno = EINTR;
                        return -1;
                    }
                    errno = EINVAL;
                    return -1;
                }
            }
        }
    }
    return (int)syscall(SYS_fsync, file);
}
"""
    with tempfile.TemporaryDirectory(
        prefix="residency-task014-adversary-"
    ) as directory:
        root = Path(directory)
        executable = root / "task014"
        interceptor = root / "directory_fsync_einval.c"
        library = root / "directory_fsync_einval.so"
        interceptor.write_text(interceptor_source, encoding="utf-8")
        subprocess.run(
            compiler_command(compiler, str(source), str(executable), "linux"),
            check=True,
            timeout=30,
        )
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-shared",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(interceptor),
                "-o",
                str(library),
            ],
            check=True,
            timeout=30,
        )
        environment = os.environ.copy()
        environment.update(
            {
                "LD_PRELOAD": str(library),
                "TASK014_DIRECTORY_SYNC_REJECT_ROOT": directory,
                "TMPDIR": directory,
            }
        )
        completed = subprocess.run(
            [f"./{executable.name}"],
            cwd=directory,
            env=environment,
            check=False,
            capture_output=True,
            timeout=30,
        )
    _, rows = parse_probe_output(completed.stdout)
    require(
        completed.returncode == 0,
        "unsupported directory synchronization failed the native probe",
    )
    require(completed.stderr == b"", "directory-sync adversary emitted stderr")
    require(set(rows) == EXPECTED_ROW_KEYS, "directory-sync adversary changed rows")
    require_exact_rows(rows, DURABLE_CRASH_ROWS, "directory-sync adversary")
    require_exact_rows(
        rows,
        {
            "durable_root_publication": "deferred",
            "durable.stage_file_flushed": "passed",
            "durable.root_replaced": "passed",
            "durable.parent_flushed": "deferred",
            "ownership.prepared_before_spawn": "passed",
            "identity.birth_token": "passed",
            "membership.direct": "passed",
            "membership.descendant": "passed",
            "identity.mismatch_signal": "blocked",
            "termination.containment": "passed",
            "termination.membership_empty": "passed",
            "containment.escape_detected": "passed",
            "process_containment": "fallback",
            "platform.current": "linux",
            "runtime_authority": "none",
        },
        "directory-sync adversary",
    )


def require_exact_rows(
    rows: dict[str, str], expected: dict[str, str], label: str
) -> None:
    for key, value in expected.items():
        require(rows.get(key) == value, f"{label} changed {key}")


def require_deferred_or_exact_rows(
    rows: dict[str, str], expected: dict[str, str], label: str
) -> None:
    for key, value in expected.items():
        require(rows.get(key) in {value, "deferred"}, f"{label} changed {key}")


def require_linux_probe_rows(rows: dict[str, str]) -> None:
    require(
        rows.get("durable_root_publication") == "passed",
        "Linux durable root probe failed",
    )
    require_exact_rows(rows, DURABLE_CRASH_ROWS, "durable crash probe")
    require_exact_rows(
        rows,
        {
            "durable.stage_file_flushed": "passed",
            "durable.root_replaced": "passed",
            "durable.parent_flushed": "passed",
            "ownership.prepared_before_spawn": "passed",
            "identity.birth_token": "passed",
            "membership.direct": "passed",
            "membership.descendant": "passed",
            "identity.mismatch_signal": "blocked",
            "termination.containment": "passed",
            "termination.membership_empty": "passed",
            "containment.escape_detected": "passed",
            "process_containment": "fallback",
        },
        "Linux probe",
    )


def require_windows_probe_rows(rows: dict[str, str]) -> None:
    require(
        rows.get("durable_root_publication") in {"passed", "deferred"},
        "Windows durable probe omitted its native disposition",
    )
    require_deferred_or_exact_rows(rows, DURABLE_CRASH_ROWS, "Windows durable probe")
    for key in (
        "ownership.prepared_before_spawn",
        "identity.birth_token",
        "membership.direct",
        "membership.descendant",
        "identity.mismatch_signal",
        "termination.containment",
        "termination.membership_empty",
        "containment.escape_detected",
    ):
        require(key in rows, f"Windows probe omitted {key}")
        if key == "identity.mismatch_signal":
            expected = {"blocked", "deferred"}
        elif key == "containment.escape_detected":
            expected = {"deferred"}
        else:
            expected = {"passed", "deferred"}
        require(rows[key] in expected, f"Windows probe changed {key}")
    require(
        rows.get("process_containment") in {"passed", "deferred"},
        "Windows probe omitted its native disposition",
    )


def require_macos_probe_rows(rows: dict[str, str]) -> None:
    require(
        rows.get("durable_root_publication") in {"passed", "deferred"},
        "macOS durable probe omitted its native disposition",
    )
    require_deferred_or_exact_rows(rows, DURABLE_CRASH_ROWS, "macOS durable probe")
    require_exact_rows(
        rows,
        {
            "ownership.prepared_before_spawn": "deferred",
            "identity.birth_token": "deferred",
            "membership.direct": "deferred",
            "membership.descendant": "deferred",
            "identity.mismatch_signal": "deferred",
            "termination.containment": "deferred",
            "termination.membership_empty": "deferred",
            "containment.escape_detected": "deferred",
            "process_containment": "deferred",
        },
        "macOS containment probe",
    )


def require_platform_probe_rows(rows: dict[str, str], system_name: str) -> None:
    if system_name == "Linux":
        require_linux_probe_rows(rows)
    elif system_name == "Windows":
        require_windows_probe_rows(rows)
    else:
        require_macos_probe_rows(rows)


def observation_body(observation) -> dict:
    body = dict(observation)
    body.pop("id")
    return body


def observation_matches_binding(observation, binding: dict) -> bool:
    return observation_body(observation) == binding


def require_fabricated_provenance_rejected(
    contract, repo_root: Path, result, recorded_observation, binding: dict
) -> None:
    candidate = clone_json(result)
    observation = next(
        item
        for item in candidate["observations"]
        if item["id"] == recorded_observation["id"]
    )
    old_id = observation["id"]
    observation["environment"]["architecture"] = "fabricated_arch"
    observation["toolchain"] = {
        "compiler": "fabricated-cxx",
        "version": "fabricated-cxx 0",
    }
    observation["compile_command"][0] = "fabricated-cxx"
    observation["id"] = contract.observation_address(observation)
    for claim in candidate["claims"]:
        for evidence in claim["evidence"]:
            if evidence["observation_id"] == old_id:
                evidence["observation_id"] = observation["id"]
    contract.validate_result(repo_root, candidate)
    require(
        not observation_matches_binding(observation, binding),
        "full public binding accepted fabricated re-addressed provenance",
    )


def require_observation_binding(
    contract, repo_root: Path, result, recorded_observation, binding: dict
) -> None:
    if recorded_observation is None:
        return
    require(
        observation_matches_binding(recorded_observation, binding),
        "current passed/fallback claims are not bound to exact compile "
        "and native output provenance",
    )
    require_fabricated_provenance_rejected(
        contract, repo_root, result, recorded_observation, binding
    )


def require_native_probe(repo_root: Path, contract, result) -> None:
    source = repo_root / "test/cpp/test_residency_prototype_task014.cpp"
    if not source.is_file():
        fail_unavailable()
    require_probe_source(source)
    platform_id = current_platform()
    recorded_observation = recorded_observation_for_platform(result, platform_id)
    completed, rows, binding = run_native_probe(repo_root, source, recorded_observation)
    require(completed.returncode == 0, "native probe returned a nonzero exit code")
    require(completed.stderr == b"", "native probe emitted stderr")
    require(
        rows.get("platform.current") == platform_id,
        "native probe did not bind its current platform",
    )
    require(set(rows) == EXPECTED_ROW_KEYS, "native probe changed its row closure")
    require_platform_probe_rows(rows, platform.system())
    require(rows.get("runtime_authority") == "none", "native probe granted authority")
    require_observation_binding(
        contract, repo_root, result, recorded_observation, binding
    )
    require_unsupported_directory_sync_deferred(
        source, binding["toolchain"]["compiler"]
    )


def require_cmake_and_plan(repo_root: Path) -> None:
    cmake = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    require(
        cmake.count(
            "add_cpp_ci_test(ResidencyPrototypeContractTask014 CI ON COMMAND "
            "test_residency_prototype_task014)"
        )
        == 1,
        "TASK-014 CTest registration is unavailable",
    )
    require(
        cmake.count("test/cpp/test_residency_prototype_task014.cpp") == 1,
        "TASK-014 C++ source is not registered once",
    )
    require(
        "CI ON COMMAND test_residency_prototype_task014" in cmake,
        "TASK-014 is not cpp-ci",
    )
    require(
        cmake.count(
            "set_tests_properties(ResidencyPrototypeContractTask014 "
            "PROPERTIES TIMEOUT 45)"
        )
        == 1,
        "TASK-014 CTest timeout is unavailable",
    )

    plan = (repo_root / "plan/architecture-portable-residency-1.md").read_text(
        encoding="utf-8"
    )
    task_row = next(
        (line for line in plan.splitlines() if line.startswith("| TASK-014 |")), ""
    )
    require(
        task_row.endswith("| ✅ | 2026-08-16 |"),
        "TASK-014 is not recorded complete",
    )
    require(
        "test/cpp/test_residency_prototype_task014.cpp" in plan,
        "Phase-2 output ownership omits the C++ seam",
    )


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    required = (
        repo_root / "test/residency/prototypes/result_contract.py",
        repo_root / "test/cpp/test_residency_prototype_task014.cpp",
    )
    if not all(path.is_file() for path in required):
        fail_unavailable()
    contract, result = require_result(repo_root)
    require_native_probe(repo_root, contract, result)
    require_cmake_and_plan(repo_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
