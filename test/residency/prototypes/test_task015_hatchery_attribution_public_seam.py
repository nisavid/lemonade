import hashlib
import importlib.util
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

FAILURE = "TASK-015 Hatchery causal attribution prototype is unavailable"
TASK_BASE = "e8ddc859387fdff11a9452d60a7e4f1b133537bd"
TASK_ID = "TASK-015"
SOURCE_PATH = "test/cpp/test_residency_prototype_task015.cpp"
PROTOTYPE_ID = "hatchery_causal_attribution"
ATTEST_RECORDED_OBSERVATION = "--attest-recorded-observation"


def parse_replay_mode(arguments: list[str]) -> bool:
    if not arguments:
        return False
    if arguments == [ATTEST_RECORDED_OBSERVATION]:
        return True
    print(
        f"{Path(__file__).name}: unsupported arguments; expected no arguments or "
        f"{ATTEST_RECORDED_OBSERVATION}",
        file=sys.stderr,
    )
    raise SystemExit(2)


UNIT_IDS = [
    "H-ROCM-ADM-GTT-HOST-v1",
    "H-ROCM-PRE-GTT-HOST-v1",
    "H-ROCM-REC-GTT-HOST-OWN-v1",
    "H-ROCM-STA-GTT-HOST-v1",
]
FALLBACK_BINDINGS = [
    {
        "unit_id": "H-ROCM-ADM-GTT-HOST-v1",
        "fallbacks": {
            "insufficient_capacity_authority": "hatchery_rocm_admission_refuse_unknown_capacity_v1"
        },
    },
    {
        "unit_id": "H-ROCM-PRE-GTT-HOST-v1",
        "fallbacks": {
            "invalid_reporting_evidence": "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
            "valid_reporting_without_action_authority": "hatchery_rocm_pressure_report_only_v1",
        },
    },
    {
        "unit_id": "H-ROCM-REC-GTT-HOST-OWN-v1",
        "fallbacks": {"unproven_release": "hatchery_rocm_recovery_block_readiness_v1"},
    },
    {
        "unit_id": "H-ROCM-STA-GTT-HOST-v1",
        "fallbacks": {
            "insufficient_startup_authority": "hatchery_rocm_startup_block_group_v1"
        },
    },
]
FALLBACK_IDS = [
    "hatchery_rocm_admission_refuse_unknown_capacity_v1",
    "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
    "hatchery_rocm_pressure_report_only_v1",
    "hatchery_rocm_recovery_block_readiness_v1",
    "hatchery_rocm_startup_block_group_v1",
]

SYNTHETIC_OUTPUT_ROWS = [
    "projection.known_zero=passed",
    "projection.missing=unknown",
    "projection.malformed=unknown",
    "projection.overflow=unknown",
    "projection.stale=unknown",
    "projection.skew=unknown",
    "projection.pid_reuse=unknown",
    "projection.cgroup_mismatch=unknown",
    "projection.device_mismatch=unknown",
    "projection.topology_alias=passed",
    "projection.topology_generation_mismatch=unknown",
    "projection.dependency_identity_mismatch=unknown",
    "gtt.headroom_signed=passed",
    "gtt.client_attribution_only=passed",
    "gtt.cgroup_rollup=passed",
    "host.memavailable_independent=passed",
    "host.shared_bytes_not_doubled=passed",
    "ownership.unowned_not_adopted=passed",
    "attribution.incoherent_total=unknown",
    "synthetic.causal_attribution=passed",
]
HATCHERY_NATIVE_OUTPUT_ROWS = [
    "hatchery.native_profile=exact",
    "native.global_gtt=observed",
    "native.host_memory=observed",
    "native.boot_identity=observed",
    "native.device_identity=observed",
    "native.process_fdinfo=unknown",
    "native.cgroup_membership=unknown",
    "hatchery.native_causal_attribution=fallback",
]
FALLBACK_OUTPUT_ROWS = [
    "fallback.admission=hatchery_rocm_admission_refuse_unknown_capacity_v1",
    "fallback.pressure=hatchery_rocm_pressure_disabled_invalid_evidence_v1",
    "fallback.startup=hatchery_rocm_startup_block_group_v1",
    "fallback.recovery=hatchery_rocm_recovery_block_readiness_v1",
]
SYNTHETIC_EVIDENCE_ROWS = sorted(SYNTHETIC_OUTPUT_ROWS)
SOURCE_TOKENS = (
    "/proc/sys/kernel/hostname",
    "/sys/class/drm/card1/device/uevent",
    "0000:c6:00.0",
    "0x1002",
    "0x1586",
    "mem_info_gtt_total",
    "mem_info_gtt_used",
    "MemAvailable",
    "boot_id",
    "fdinfo",
    "cgroup.procs",
    "mount_id",
    "inode",
    "memory.current",
    "drm_pdev",
)
FORBIDDEN_SOURCE_TOKENS = (
    "get_memory_usage_gb(",
    "parse_memory_sysfs(",
    "get_vram_usage_gb(",
    "get_global_vram_usage_pct(",
)


def expected_output_rows(
    platform_id: str, native_rows: list[str] | None = None
) -> list[str]:
    if native_rows is None:
        native_rows = HATCHERY_NATIVE_OUTPUT_ROWS
    return [
        *SYNTHETIC_OUTPUT_ROWS,
        *native_rows,
        *FALLBACK_OUTPUT_ROWS,
        f"platform.current={platform_id}",
        "runtime_authority=none",
    ]


EXPECTED_ROW_KEYS = {row.split("=", 1)[0] for row in expected_output_rows("linux")}


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


def claim_expectations() -> dict:
    return {
        "hatchery_admission_native_attribution": {
            "status": "fallback",
            "units": ["H-ROCM-ADM-GTT-HOST-v1"],
            "fallbacks": ["hatchery_rocm_admission_refuse_unknown_capacity_v1"],
            "bindings": [FALLBACK_BINDINGS[0]],
            "rows": sorted(
                [
                    "fallback.admission=hatchery_rocm_admission_refuse_unknown_capacity_v1",
                    *HATCHERY_NATIVE_OUTPUT_ROWS,
                ]
            ),
            "limitations": [
                "Native Hatchery attribution is incomplete, so capacity authority remains insufficient."
            ],
        },
        "hatchery_pressure_native_attribution": {
            "status": "fallback",
            "units": ["H-ROCM-PRE-GTT-HOST-v1"],
            "fallbacks": [
                "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
                "hatchery_rocm_pressure_report_only_v1",
            ],
            "bindings": [FALLBACK_BINDINGS[1]],
            "rows": sorted(
                [
                    "fallback.pressure=hatchery_rocm_pressure_disabled_invalid_evidence_v1",
                    *HATCHERY_NATIVE_OUTPUT_ROWS,
                ]
            ),
            "limitations": [
                "Native Hatchery attribution is incomplete, so invalid-evidence pressure automation remains disabled."
            ],
        },
        "hatchery_recovery_native_attribution": {
            "status": "fallback",
            "units": ["H-ROCM-REC-GTT-HOST-OWN-v1"],
            "fallbacks": ["hatchery_rocm_recovery_block_readiness_v1"],
            "bindings": [FALLBACK_BINDINGS[2]],
            "rows": sorted(
                [
                    "fallback.recovery=hatchery_rocm_recovery_block_readiness_v1",
                    *HATCHERY_NATIVE_OUTPUT_ROWS,
                ]
            ),
            "limitations": [
                "Native Hatchery attribution is incomplete, so ownership and release remain unproved."
            ],
        },
        "hatchery_startup_native_attribution": {
            "status": "fallback",
            "units": ["H-ROCM-STA-GTT-HOST-v1"],
            "fallbacks": ["hatchery_rocm_startup_block_group_v1"],
            "bindings": [FALLBACK_BINDINGS[3]],
            "rows": sorted(
                [
                    "fallback.startup=hatchery_rocm_startup_block_group_v1",
                    *HATCHERY_NATIVE_OUTPUT_ROWS,
                ]
            ),
            "limitations": [
                "Native Hatchery attribution is incomplete, so grouped startup authority remains insufficient."
            ],
        },
        "synthetic_causal_attribution_contract": {
            "status": "passed",
            "units": UNIT_IDS,
            "fallbacks": FALLBACK_IDS,
            "bindings": FALLBACK_BINDINGS,
            "rows": SYNTHETIC_EVIDENCE_ROWS,
            "limitations": [
                "Injected observations prove type and composition behavior only; they grant no physical Hatchery or runtime authority."
            ],
        },
    }


def require_result_identity(repo_root: Path, result: dict) -> None:
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
        "TASK-015 result fields are not closed",
    )
    require(
        result["schema"] == "residency_prototype_result/v1",
        "result schema changed",
    )
    require(result["task_id"] == TASK_ID, "TASK-015 result changed its task ID")
    require(
        result["task_base_commit"] == TASK_BASE,
        "TASK-015 result changed its task base",
    )
    require(
        result["prototype_id"] == PROTOTYPE_ID,
        "TASK-015 result changed its prototype identity",
    )
    source = repo_root / SOURCE_PATH
    require(
        result["source"]
        == {
            "path": SOURCE_PATH,
            "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        },
        "TASK-015 result does not bind its source bytes",
    )
    require(result["outcome"] == "mixed", "TASK-015 result must remain mixed")
    require(
        result["runtime_authority"] == "none",
        "TASK-015 prototype granted runtime authority",
    )
    require(
        result["fallback_state"] == {"authority": "legacy_runtime", "status": "active"},
        "TASK-015 prototype changed the active fallback authority",
    )


def require_result_claims(result: dict, observation_id: str) -> None:
    expected = claim_expectations()
    claims = {claim["id"]: claim for claim in result["claims"]}
    require(len(claims) == len(result["claims"]), "TASK-015 claim IDs must be unique")
    require(set(claims) == set(expected), "TASK-015 result changed its claim closure")
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
        require(claim["platforms"] == ["linux"], f"{claim_id} changed platform scope")
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
            == [{"observation_id": observation_id, "rows": contract_row["rows"]}],
            f"{claim_id} changed its exact observed evidence",
        )
        require(
            claim["limitations"] == contract_row["limitations"],
            f"{claim_id} changed its bounded limitation",
        )


def require_result(repo_root: Path, contract=None):
    if contract is None:
        contract = load_result_contract(repo_root)
    result = contract.load_task_result(repo_root, TASK_ID)
    result_root = repo_root / "docs/research/residency-prototype-results/sha256"
    matches = []
    for path in sorted(result_root.glob("*.json")):
        raw = path.read_bytes()
        require(
            path.stem == hashlib.sha256(raw).hexdigest(),
            f"{path.name} is not addressed by bytes",
        )
        candidate = parse_json_object(raw)
        if candidate.get("task_id") == TASK_ID:
            matches.append(candidate)
    require(matches == [result], "TASK-015 must have one byte-addressed result")
    canonical = (
        json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    require(
        any(path.read_bytes() == canonical for path in result_root.glob("*.json")),
        "TASK-015 result is not canonical UTF-8 JSON",
    )
    require_result_identity(repo_root, result)
    observations = result["observations"]
    require(len(observations) == 1, "TASK-015 must publish one probe observation")
    observation = observations[0]
    require(
        observation["environment"]["platform"] == "linux",
        "TASK-015 initial observation must remain Linux-scoped",
    )
    require(
        observation["output"]["rows"] == expected_output_rows("linux"),
        "TASK-015 initial observation changed its exact row order or closure",
    )
    require_result_claims(result, observation["id"])
    return contract, result


def current_platform() -> str:
    return {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(
        platform.system(), ""
    )


def read_kernel_text(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeError):
        return None


def parse_decimal(value: str | None) -> int | None:
    if value is None or not value.isascii() or not value.isdecimal():
        return None
    try:
        parsed = int(value)
    except ValueError:
        return None
    return parsed if parsed <= (1 << 64) - 1 else None


def classify_hatchery_identity(
    hostname: str | None, device_matches: bool | None
) -> str:
    if hostname is None and device_matches is None:
        return "unavailable"
    if (hostname is not None and hostname != "hatchery") or device_matches is False:
        return "not_hatchery"
    if hostname == "hatchery" and device_matches is True:
        return "exact"
    return "incomplete"


def hatchery_device_matches() -> bool | None:
    drm_root = Path("/sys/class/drm")
    try:
        cards = sorted(
            path
            for path in drm_root.iterdir()
            if path.name.startswith("card") and path.name[4:].isdigit()
        )
    except OSError:
        return None
    if len(cards) != 1:
        return False
    device_root = cards[0] / "device"
    vendor = read_kernel_text(device_root / "vendor")
    device = read_kernel_text(device_root / "device")
    uevent_text = read_kernel_text(device_root / "uevent")
    if vendor is None or device is None or uevent_text is None:
        return None
    uevent = {}
    for line in uevent_text.splitlines():
        if line.count("=") != 1:
            return None
        key, value = line.split("=", 1)
        if not key or key in uevent:
            return None
        uevent[key] = value
    return (
        cards[0].name == "card1"
        and vendor.lower() == "0x1002"
        and device.lower() == "0x1586"
        and uevent.get("DRIVER") == "amdgpu"
        and uevent.get("PCI_ID", "").lower() == "1002:1586"
        and uevent.get("PCI_SLOT_NAME") == "0000:c6:00.0"
    )


def global_gtt_is_observed() -> bool:
    device_root = Path("/sys/class/drm/card1/device")
    total = parse_decimal(read_kernel_text(device_root / "mem_info_gtt_total"))
    used = parse_decimal(read_kernel_text(device_root / "mem_info_gtt_used"))
    return total is not None and total > 0 and used is not None and used <= total


def host_memory_is_observed() -> bool:
    meminfo = read_kernel_text(Path("/proc/meminfo"))
    cgroup = read_kernel_text(Path("/proc/self/cgroup"))
    if meminfo is None or cgroup is None:
        return False
    memavailable = None
    for line in meminfo.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0] == "MemAvailable:" and fields[2] == "kB":
            memavailable = parse_decimal(fields[1])
            break
    cgroup_lines = [line for line in cgroup.splitlines() if line.startswith("0::")]
    if len(cgroup_lines) != 1:
        return False
    relative = cgroup_lines[0][3:]
    relative_parts = Path(relative).parts
    if not relative.startswith("/") or ".." in relative_parts:
        return False
    current = parse_decimal(
        read_kernel_text(
            Path("/sys/fs/cgroup") / relative.lstrip("/") / "memory.current"
        )
    )
    return memavailable is not None and current is not None


def boot_identity_is_observed() -> bool:
    value = read_kernel_text(Path("/proc/sys/kernel/random/boot_id"))
    if value is None or len(value) != 36:
        return False
    return all(character in "0123456789abcdef-" for character in value.lower())


def native_output_rows(platform_id: str) -> list[str]:
    if platform_id != "linux":
        profile = "unavailable"
        observed = False
    else:
        profile = classify_hatchery_identity(
            read_kernel_text(Path("/proc/sys/kernel/hostname")),
            hatchery_device_matches(),
        )
        observed = profile == "exact"
    global_gtt = observed and global_gtt_is_observed()
    host_memory = observed and host_memory_is_observed()
    boot_identity = observed and boot_identity_is_observed()
    return [
        f"hatchery.native_profile={profile}",
        f"native.global_gtt={'observed' if global_gtt else 'unknown'}",
        f"native.host_memory={'observed' if host_memory else 'unknown'}",
        f"native.boot_identity={'observed' if boot_identity else 'unknown'}",
        f"native.device_identity={'observed' if observed else 'unknown'}",
        "native.process_fdinfo=unknown",
        "native.cgroup_membership=unknown",
        f"hatchery.native_causal_attribution={'fallback' if observed else 'deferred'}",
    ]


def require_native_profile_classifier() -> None:
    require(
        classify_hatchery_identity("hatchery", True) == "exact",
        "exact Hatchery identity was not recognized",
    )
    require(
        classify_hatchery_identity("hatchery", False) == "not_hatchery",
        "hostname-only identity was accepted",
    )
    require(
        classify_hatchery_identity("other", True) == "not_hatchery",
        "device-only identity was accepted",
    )
    require(
        classify_hatchery_identity("hatchery", None) == "incomplete",
        "incomplete Hatchery identity was accepted",
    )
    require(
        classify_hatchery_identity(None, None) == "unavailable",
        "unavailable Hatchery identity was accepted",
    )


def parse_probe_output(stdout: bytes) -> tuple[list[str], dict[str, str]]:
    require(stdout.endswith(b"\n"), "prototype probe stdout lacks its final newline")
    lines = stdout.decode("utf-8").splitlines()
    require(lines, "prototype probe emitted no observation rows")
    rows = {}
    for line in lines:
        require(
            line.count("=") == 1, f"prototype probe emitted a malformed row: {line}"
        )
        key, value = line.split("=", 1)
        require(key and value, f"prototype probe emitted an empty row field: {line}")
        require(key not in rows, f"prototype probe emitted duplicate row: {key}")
        rows[key] = value
    return lines, rows


def require_probe_source(source: Path) -> None:
    source_text = source.read_text(encoding="utf-8")
    source_text_lower = source_text.lower()
    for token in SOURCE_TOKENS:
        require(token.lower() in source_text_lower, f"prototype probe omits {token}")
    for token in FORBIDDEN_SOURCE_TOKENS:
        require(
            token not in source_text_lower,
            f"prototype probe uses fail-open source token {token}",
        )
    require(
        not any(
            line.lstrip().startswith('#include "') for line in source_text.splitlines()
        ),
        "prototype probe includes a non-standard-library header",
    )


def recorded_observation_for_platform(result: dict, platform_id: str):
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


def run_probe(repo_root: Path, source: Path, contract, recorded_observation):
    platform_id = current_platform()
    require(platform_id, "prototype probe ran on an unsupported platform")
    compiler_executable, compiler = contract.resolve_replay_compiler(
        recorded_observation, platform_id, os.environ
    )
    version = (
        contract.attest_recorded_observation_toolchain(
            recorded_observation, compiler_executable, compiler, platform_id
        )
        if recorded_observation is not None
        else compiler_version(compiler_executable, platform_id)
    )
    suffix = ".exe" if platform_id == "windows" else ""
    executable_name = f"task015{suffix}"
    logical_source = source.relative_to(repo_root).as_posix()
    logical_output = f"$TMPDIR/{executable_name}"
    logical_compile_command = compiler_command(
        compiler, logical_source, logical_output, platform_id
    )
    with tempfile.TemporaryDirectory(prefix="residency-task015-") as directory:
        executable = Path(directory) / executable_name
        actual_compile_command = compiler_command(
            compiler_executable, str(source), str(executable), platform_id
        )
        subprocess.run(
            actual_compile_command,
            cwd=directory,
            check=True,
            capture_output=True,
            timeout=30,
        )
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
    return completed, lines, rows, binding


def observation_body(observation: dict) -> dict:
    body = dict(observation)
    body.pop("id")
    return body


def observation_matches_binding(recorded_observation, binding: dict) -> bool:
    return (
        recorded_observation is not None
        and observation_body(recorded_observation) == binding
    )


def require_unbound_observation_regression() -> None:
    require(
        not observation_matches_binding(None, {}),
        "an absent observation authenticated an unbound replay",
    )


def require_observation_binding(
    contract, repo_root: Path, result: dict, recorded_observation, binding: dict
) -> None:
    require(
        recorded_observation is not None,
        "recorded-observation attestation selected no observation",
    )
    contract.require_recorded_observation_body(recorded_observation, binding)
    candidate = json.loads(json.dumps(result))
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
        observation_body(observation) != binding,
        "full public binding accepted fabricated re-addressed provenance",
    )


def require_exact_probe_rows(
    lines: list[str],
    rows: dict[str, str],
    platform_id: str,
    current_native_rows: list[str],
) -> None:
    expected = expected_output_rows(platform_id, current_native_rows)
    require(lines == expected, "prototype probe changed its exact row order or closure")
    require(
        set(rows) == EXPECTED_ROW_KEYS,
        "prototype probe changed its observation keys",
    )


def require_probe(
    repo_root: Path,
    contract,
    result: dict,
    attest_recorded_observation: bool,
) -> None:
    source = repo_root / SOURCE_PATH
    if not source.is_file():
        fail_unavailable()
    require_probe_source(source)
    platform_id = current_platform()
    recorded_observation = (
        recorded_observation_for_platform(result, platform_id)
        if attest_recorded_observation
        else None
    )
    require_unbound_observation_regression()
    require_native_profile_classifier()
    current_native_rows = native_output_rows(platform_id)
    if attest_recorded_observation:
        native_profile_row = next(
            row
            for row in current_native_rows
            if row.startswith("hatchery.native_profile=")
        )
        contract.require_recorded_observation_environment(
            recorded_observation,
            platform_id,
            normalized_architecture(),
            native_profile_row.split("=", 1)[1],
        )
    completed, lines, rows, binding = run_probe(
        repo_root, source, contract, recorded_observation
    )
    require(completed.returncode == 0, "prototype probe returned a nonzero exit code")
    require(completed.stderr == b"", "prototype probe emitted stderr")
    require_exact_probe_rows(lines, rows, platform_id, current_native_rows)
    require(
        rows.get("platform.current") == platform_id,
        "prototype probe did not bind its current platform",
    )
    require(rows.get("runtime_authority") == "none", "prototype granted authority")
    if attest_recorded_observation:
        require_observation_binding(
            contract, repo_root, result, recorded_observation, binding
        )


def require_cmake_and_plan(repo_root: Path) -> None:
    cmake = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    cmake_block = f"""set(_RESIDENCY_PROTOTYPE_TASK015_TEST_SRC
    "${{CMAKE_CURRENT_SOURCE_DIR}}/{SOURCE_PATH}"
)
if(BUILD_TESTING AND EXISTS "${{_RESIDENCY_PROTOTYPE_TASK015_TEST_SRC}}")
    add_executable(test_residency_prototype_task015
        ${{_RESIDENCY_PROTOTYPE_TASK015_TEST_SRC}}
    )
    add_cpp_ci_test(ResidencyPrototypeContractTask015 CI ON COMMAND test_residency_prototype_task015)
    set_tests_properties(ResidencyPrototypeContractTask015 PROPERTIES TIMEOUT 45)
endif()"""
    require(
        cmake.count(cmake_block) == 1,
        "TASK-015 CMake declaration is not one closed BUILD_TESTING block",
    )
    require(
        cmake.count(SOURCE_PATH) == 1,
        "TASK-015 C++ source is not registered once",
    )

    plan = (repo_root / "plan/architecture-portable-residency-1.md").read_text(
        encoding="utf-8"
    )
    task_row = next(
        (line for line in plan.splitlines() if line.startswith("| TASK-015 |")), ""
    )
    require(
        task_row.endswith("| ✅ | 2026-08-16 |"),
        "TASK-015 is not recorded complete",
    )
    output_rows = [
        line for line in plan.splitlines() if line.startswith("| TASK-014–TASK-018 |")
    ]
    require(
        len(output_rows) == 1,
        "Phase-2 output ownership row is unavailable or ambiguous",
    )
    output_row = output_rows[0]
    for required_text in (
        "CMakeLists.txt",
        SOURCE_PATH,
        "test/residency/prototypes/",
        "docs/research/residency-prototype-results/",
        "no production authority",
    ):
        require(
            required_text in output_row,
            f"Phase-2 output ownership row omits {required_text}",
        )


def main(arguments: list[str]) -> int:
    attest_recorded_observation = parse_replay_mode(arguments)
    repo_root = Path(__file__).resolve().parents[3]
    required = (
        repo_root / "test/residency/prototypes/result_contract.py",
        repo_root / SOURCE_PATH,
    )
    if not all(path.is_file() for path in required):
        fail_unavailable()
    contract = load_result_contract(repo_root)
    try:
        contract, result = require_result(repo_root, contract)
        require_probe(repo_root, contract, result, attest_recorded_observation)
        require_cmake_and_plan(repo_root)
    except contract.PrototypeResultError as error:
        print(
            f"{Path(__file__).name}: {contract.public_diagnostic(error)}",
            file=sys.stderr,
        )
        return 1
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(
            f"{Path(__file__).name}: "
            f"{contract.public_operational_failure(attest_recorded_observation, error)}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
