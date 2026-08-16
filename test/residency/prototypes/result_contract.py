import hashlib
import json
import re
from pathlib import Path

ROOT_KEYS = {
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
}
CLAIM_KEYS = {
    "id",
    "status",
    "platforms",
    "affected_units",
    "fallback_ids",
    "fallback_bindings",
    "evidence",
    "limitations",
}
SOURCE_KEYS = {"path", "sha256"}
OBSERVATION_KEYS = {
    "id",
    "compile_command",
    "command",
    "environment",
    "toolchain",
    "exit_code",
    "output",
}
OBSERVATION_ENVIRONMENT_KEYS = {"platform", "architecture"}
OBSERVATION_TOOLCHAIN_KEYS = {"compiler", "version"}
OBSERVATION_OUTPUT_KEYS = {"stdout_sha256", "stderr_sha256", "rows"}
EVIDENCE_KEYS = {"observation_id", "rows"}
FALLBACK_BINDING_KEYS = {"unit_id", "fallbacks"}
TASK_RE = re.compile(r"TASK-[0-9]{3}")
TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9_.-]{0,127}")
TOOL_RE = re.compile(r"[a-z0-9][a-z0-9_.+-]{0,127}")
SHA1_RE = re.compile(r"[0-9a-f]{40}")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
OBSERVATION_ID_RE = re.compile(r"sha256-[0-9a-f]{64}")
ROW_RE = re.compile(r"[a-z0-9][a-z0-9_.-]{0,127}=[a-z0-9][a-z0-9_.-]{0,127}")
PROTOTYPE_SOURCE_RE = re.compile(r"test_residency_prototype_(task[0-9]{3})[.]cpp")
STATUSES = {"passed", "deferred", "fallback"}
OUTCOMES = {"passed", "mixed", "deferred", "fallback"}
PLATFORMS = {"linux", "macos", "windows"}
EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


class PrototypeResultError(ValueError):
    pass


def _fail(message: str) -> None:
    raise PrototypeResultError(message)


def _object_from_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            _fail(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _parse(raw: bytes, label: str):
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_object_from_pairs)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"{label} is not strict UTF-8 JSON: {error}")
    return value


def _mapping(value, label: str) -> dict:
    if type(value) is not dict:
        _fail(f"{label} must be an object")
    return value


def _array(value, label: str) -> list:
    if type(value) is not list:
        _fail(f"{label} must be an array")
    return value


def _string(value, label: str) -> str:
    if type(value) is not str or not value or any(ord(char) < 32 for char in value):
        _fail(f"{label} must be a nonempty control-free string")
    return value


def _integer(value, label: str) -> int:
    if type(value) is not int:
        _fail(f"{label} must be an integer")
    return value


def _exact_keys(value: dict, expected: set[str], label: str) -> None:
    if set(value) != expected:
        _fail(
            f"{label} fields differ; missing={sorted(expected - set(value))}, extra={sorted(set(value) - expected)}"
        )


def _sorted_unique_strings(value, label: str, allow_empty: bool = False) -> list[str]:
    items = [_string(item, f"{label}[]") for item in _array(value, label)]
    if not allow_empty and not items:
        _fail(f"{label} must not be empty")
    if items != sorted(set(items)):
        _fail(f"{label} must be unique and ASCII-sorted")
    return items


def _known_contract_ids(repo_root: Path) -> tuple[set[str], dict[str, dict[str, str]]]:
    path = repo_root / "docs/research/portable-residency-capability-inventory.json"
    root = _mapping(_parse(path.read_bytes(), str(path)), str(path))
    fallbacks = set(_mapping(root.get("fallback_registry"), "fallback_registry"))
    units = {}
    for collection, id_key in (
        ("exact_cells", "cell_id"),
        ("compatibility_contracts", "contract_id"),
        ("later_promotion_roster", "unit_id"),
    ):
        for row_value in _array(root.get(collection), collection):
            row = _mapping(row_value, f"{collection}[]")
            unit_id = _string(row.get(id_key), f"{collection}[].{id_key}")
            if unit_id in units:
                _fail(f"duplicate inventory promotion unit: {unit_id}")
            fallback_map = _mapping(row.get("fallbacks"), f"{collection}[].fallbacks")
            normalized = {}
            for reason, fallback_id in fallback_map.items():
                reason = _string(reason, f"{collection}[].fallbacks key")
                if TOKEN_RE.fullmatch(reason) is None:
                    _fail(f"{collection}[].fallbacks has an invalid reason")
                fallback_id = _string(fallback_id, f"{collection}[].fallbacks.{reason}")
                if fallback_id not in fallbacks:
                    _fail(f"{collection}[].fallbacks references an unknown fallback")
                normalized[reason] = fallback_id
            if not normalized:
                _fail(f"{collection}[].fallbacks must not be empty")
            units[unit_id] = normalized
    return fallbacks, units


def _canonical_bytes(value) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def observation_address(observation) -> str:
    body = dict(_mapping(observation, "observation"))
    body.pop("id", None)
    return "sha256-" + hashlib.sha256(_canonical_bytes(body)).hexdigest()


def _bounded_command(value, label: str) -> list[str]:
    command = [_string(item, f"{label}[]") for item in _array(value, label)]
    if not command or any(len(item.encode("utf-8")) > 512 for item in command):
        _fail(f"{label} must contain bounded exact arguments")
    return command


def _expected_commands(
    source_path: str, execution_platform: str, compiler: str
) -> tuple[list[str], list[str]]:
    match = PROTOTYPE_SOURCE_RE.fullmatch(Path(source_path).name)
    if match is None:
        _fail("source.path must identify a TASK-NNN prototype source")
    executable = match.group(1) + (".exe" if execution_platform == "windows" else "")
    output = f"$TMPDIR/{executable}"
    if execution_platform == "windows":
        compile_command = [
            compiler,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            source_path,
            f"/Fe:{output}",
        ]
        return compile_command, [".\\" + executable]
    compile_command = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-pthread",
        source_path,
        "-o",
        output,
    ]
    return compile_command, [f"./{executable}"]


def _validate_commands(
    observation: dict,
    label: str,
    source_path: str,
    execution_platform: str,
    compiler: str,
) -> None:
    compile_command = _bounded_command(
        observation["compile_command"], f"{label}.compile_command"
    )
    command = _bounded_command(observation["command"], f"{label}.command")
    if compile_command[0] != compiler:
        _fail(f"{label}.compile_command compiler differs from toolchain.compiler")
    expected_compile, expected_run = _expected_commands(
        source_path, execution_platform, compiler
    )
    if compile_command != expected_compile or command != expected_run:
        _fail(f"{label} compile source, output, and run command are not closed")


def _validate_environment(observation: dict, label: str) -> str:
    environment = _mapping(observation["environment"], f"{label}.environment")
    _exact_keys(environment, OBSERVATION_ENVIRONMENT_KEYS, f"{label}.environment")
    execution_platform = _string(
        environment["platform"], f"{label}.environment.platform"
    )
    if execution_platform not in PLATFORMS:
        _fail(f"{label}.environment.platform is unknown")
    architecture = _string(
        environment["architecture"], f"{label}.environment.architecture"
    )
    if TOKEN_RE.fullmatch(architecture.lower()) is None:
        _fail(f"{label}.environment.architecture must be a stable token")
    return execution_platform


def _validate_toolchain(observation: dict, label: str) -> str:
    toolchain = _mapping(observation["toolchain"], f"{label}.toolchain")
    _exact_keys(toolchain, OBSERVATION_TOOLCHAIN_KEYS, f"{label}.toolchain")
    compiler = _string(toolchain["compiler"], f"{label}.toolchain.compiler")
    if TOOL_RE.fullmatch(compiler.lower()) is None:
        _fail(f"{label}.toolchain.compiler must be a stable token")
    version = _string(toolchain["version"], f"{label}.toolchain.version")
    if len(version.encode("utf-8")) > 256:
        _fail(f"{label}.toolchain.version must be at most 256 UTF-8 bytes")
    return compiler


def _validate_exit_code(observation: dict, label: str) -> None:
    exit_code = _integer(observation["exit_code"], f"{label}.exit_code")
    if exit_code < 0 or exit_code > 255:
        _fail(f"{label}.exit_code must be in [0, 255]")


def _validate_output(observation: dict, label: str, execution_platform: str) -> None:
    output = _mapping(observation["output"], f"{label}.output")
    _exact_keys(output, OBSERVATION_OUTPUT_KEYS, f"{label}.output")
    stdout_digest = _string(output["stdout_sha256"], f"{label}.output.stdout_sha256")
    stderr_digest = _string(output["stderr_sha256"], f"{label}.output.stderr_sha256")
    if (
        SHA256_RE.fullmatch(stdout_digest) is None
        or SHA256_RE.fullmatch(stderr_digest) is None
    ):
        _fail(f"{label}.output digests must be 64 lowercase hex")
    if stderr_digest != EMPTY_SHA256:
        _fail(f"{label}.output must record empty stderr")
    rows = [
        _string(item, f"{label}.output.rows[]")
        for item in _array(output["rows"], f"{label}.output.rows")
    ]
    if not rows or any(ROW_RE.fullmatch(row) is None for row in rows):
        _fail(f"{label}.output.rows must contain only exact key=value rows")
    row_keys = [row.split("=", 1)[0] for row in rows]
    if len(row_keys) != len(set(row_keys)):
        _fail(f"{label}.output.rows contains a duplicate row key")
    stdout = ("\n".join(rows) + "\n").encode("utf-8")
    if hashlib.sha256(stdout).hexdigest() != stdout_digest:
        _fail(f"{label}.output.stdout_sha256 does not match exact rows")
    if f"platform.current={execution_platform}" not in rows:
        _fail(f"{label}.output.rows does not bind its execution platform")


def _validate_observation(value, label: str, source_path: str) -> dict:
    observation = _mapping(value, label)
    _exact_keys(observation, OBSERVATION_KEYS, label)
    observation_id = _string(observation["id"], f"{label}.id")
    if OBSERVATION_ID_RE.fullmatch(observation_id) is None:
        _fail(f"{label}.id must be a sha256 content address")
    execution_platform = _validate_environment(observation, label)
    compiler = _validate_toolchain(observation, label)
    _validate_commands(observation, label, source_path, execution_platform, compiler)
    _validate_exit_code(observation, label)
    _validate_output(observation, label, execution_platform)
    if observation_address(observation) != observation_id:
        _fail(f"{label}.id does not address its exact observation body")
    return observation


def _expected_outcome(statuses: list[str]) -> str:
    distinct = set(statuses)
    if distinct == {"passed"}:
        return "passed"
    if distinct == {"deferred"}:
        return "deferred"
    if distinct == {"fallback"}:
        return "fallback"
    return "mixed"


def _validate_result_identity(root: dict) -> None:
    if root["schema"] != "residency_prototype_result/v1":
        _fail("prototype result schema must be residency_prototype_result/v1")
    task_id = _string(root["task_id"], "task_id")
    if TASK_RE.fullmatch(task_id) is None:
        _fail("task_id must be TASK-NNN")
    if TOKEN_RE.fullmatch(_string(root["prototype_id"], "prototype_id")) is None:
        _fail("prototype_id must be a stable token")
    if SHA1_RE.fullmatch(_string(root["task_base_commit"], "task_base_commit")) is None:
        _fail("task_base_commit must be 40 lowercase hex")
    if root["runtime_authority"] != "none":
        _fail("runtime_authority must remain none")
    if root["fallback_state"] != {"authority": "legacy_runtime", "status": "active"}:
        _fail("fallback_state must remain legacy_runtime active")


def _validate_source(repo_root: Path, root: dict) -> str:
    source = _mapping(root["source"], "source")
    _exact_keys(source, SOURCE_KEYS, "source")
    source_path = _string(source["path"], "source.path")
    if not source_path.startswith("test/cpp/") or ".." in Path(source_path).parts:
        _fail("source.path must be under test/cpp")
    task_stem = root["task_id"].lower().replace("-", "")
    expected_name = f"test_residency_prototype_{task_stem}.cpp"
    if Path(source_path).name != expected_name:
        _fail("source.path must identify the result task")
    source_digest = _string(source["sha256"], "source.sha256")
    if SHA256_RE.fullmatch(source_digest) is None:
        _fail("source.sha256 must be 64 lowercase hex")
    source_bytes = (repo_root / source_path).read_bytes()
    if hashlib.sha256(source_bytes).hexdigest() != source_digest:
        _fail("source.sha256 does not match source bytes")
    return source_path


def _validate_observations(
    root: dict, source_path: str
) -> tuple[list[dict], dict[str, dict]]:
    observations = [
        _validate_observation(item, "observations[]", source_path)
        for item in _array(root["observations"], "observations")
    ]
    if not observations:
        _fail("observations must not be empty")
    observation_ids = [observation["id"] for observation in observations]
    if observation_ids != sorted(set(observation_ids)):
        _fail("observation IDs must be unique and ASCII-sorted")
    observations_by_id = {
        observation["id"]: observation for observation in observations
    }
    return observations, observations_by_id


def _validate_claim_scope(
    claim: dict, known_fallbacks: set[str], known_units: dict[str, dict[str, str]]
) -> tuple[str, str, list[str], list[str], list[str]]:
    claim_id = _string(claim["id"], "claims[].id")
    if TOKEN_RE.fullmatch(claim_id) is None:
        _fail("claims[].id must be a stable token")
    status = _string(claim["status"], "claims[].status")
    if status not in STATUSES:
        _fail("claims[].status is unknown")
    platforms = _sorted_unique_strings(claim["platforms"], "claims[].platforms")
    if not set(platforms) <= PLATFORMS:
        _fail("claims[].platforms contains an unknown platform")
    units = _sorted_unique_strings(
        claim["affected_units"], "claims[].affected_units", allow_empty=True
    )
    if not set(units) <= set(known_units):
        _fail("claims[].affected_units contains an unknown promotion unit")
    fallbacks = _sorted_unique_strings(claim["fallback_ids"], "claims[].fallback_ids")
    if not set(fallbacks) <= known_fallbacks:
        _fail("claims[].fallback_ids contains an unknown fallback")
    return claim_id, status, platforms, units, fallbacks


def _validate_fallback_bindings(
    claim: dict,
    known_units: dict[str, dict[str, str]],
    units: list[str],
    fallbacks: list[str],
) -> None:
    bindings = [
        _mapping(item, "claims[].fallback_bindings[]")
        for item in _array(claim["fallback_bindings"], "claims[].fallback_bindings")
    ]
    binding_units = []
    bound_fallbacks = set()
    for binding in bindings:
        _exact_keys(binding, FALLBACK_BINDING_KEYS, "claims[].fallback_bindings[]")
        unit_id = _string(binding["unit_id"], "claims[].fallback_bindings[].unit_id")
        binding_units.append(unit_id)
        fallback_map = _mapping(
            binding["fallbacks"], "claims[].fallback_bindings[].fallbacks"
        )
        if unit_id not in known_units or fallback_map != known_units[unit_id]:
            _fail("claims[].fallback_bindings must exactly match the inventory mapping")
        bound_fallbacks.update(fallback_map.values())
    if binding_units != sorted(set(binding_units)) or binding_units != units:
        _fail("claims[].fallback_bindings must bind each affected unit exactly once")
    if units and set(fallbacks) != bound_fallbacks:
        _fail(
            "claims[].fallback_ids must equal the exact bound inventory fallback closure"
        )


def _validate_evidence(
    claim: dict, observations_by_id: dict[str, dict]
) -> tuple[list[str], list[tuple[str, str]]]:
    evidence = [
        _mapping(item, "claims[].evidence[]")
        for item in _array(claim["evidence"], "claims[].evidence")
    ]
    if not evidence:
        _fail("claims[].evidence must not be empty")
    evidence_observation_ids = []
    referenced_rows = []
    for reference in evidence:
        _exact_keys(reference, EVIDENCE_KEYS, "claims[].evidence[]")
        observation_id = _string(
            reference["observation_id"], "claims[].evidence[].observation_id"
        )
        if observation_id not in observations_by_id:
            _fail("claims[].evidence references a missing observation ID")
        if observations_by_id[observation_id]["exit_code"] != 0:
            _fail("claims[].evidence references a nonzero-exit observation")
        evidence_observation_ids.append(observation_id)
        rows = _sorted_unique_strings(reference["rows"], "claims[].evidence[].rows")
        observed_rows = set(observations_by_id[observation_id]["output"]["rows"])
        if not set(rows) <= observed_rows:
            _fail("claims[].evidence references a row not in the observation")
        referenced_rows.extend((observation_id, row) for row in rows)
    if evidence_observation_ids != sorted(set(evidence_observation_ids)):
        _fail("claims[].evidence observation IDs must be unique and ASCII-sorted")
    return evidence_observation_ids, referenced_rows


def _validate_evidence_status(
    status: str,
    platforms: list[str],
    referenced_rows: list[tuple[str, str]],
    observations_by_id: dict[str, dict],
) -> None:
    matching_rows = [
        row
        for observation_id, row in referenced_rows
        if observations_by_id[observation_id]["environment"]["platform"] in platforms
    ]
    if status in {"passed", "fallback"}:
        if not matching_rows or not any(
            row.rsplit("=", 1)[1] == status for row in matching_rows
        ):
            _fail(
                f"claims[] status {status} lacks a same-platform observed {status} row"
            )
        return
    if matching_rows:
        if not any(row.rsplit("=", 1)[1] == "deferred" for row in matching_rows):
            _fail(
                "claims[] deferred status lacks a same-platform observed deferred row"
            )
        return
    if not any(
        row
        == "platform.current="
        + observations_by_id[observation_id]["environment"]["platform"]
        for observation_id, row in referenced_rows
    ):
        _fail("claims[] cross-platform deferral must cite its observed platform scope")


def _validate_limitations(claim: dict) -> None:
    limitations = _sorted_unique_strings(claim["limitations"], "claims[].limitations")
    if any(len(item.encode("utf-8")) > 256 for item in limitations):
        _fail("claims[].limitations entries must be at most 256 UTF-8 bytes")


def _validate_claim(
    claim: dict,
    known_fallbacks: set[str],
    known_units: dict[str, dict[str, str]],
    observations_by_id: dict[str, dict],
) -> tuple[str, str, list[str]]:
    _exact_keys(claim, CLAIM_KEYS, "claims[]")
    claim_id, status, platforms, units, fallbacks = _validate_claim_scope(
        claim, known_fallbacks, known_units
    )
    _validate_fallback_bindings(claim, known_units, units, fallbacks)
    evidence_ids, referenced_rows = _validate_evidence(claim, observations_by_id)
    _validate_evidence_status(status, platforms, referenced_rows, observations_by_id)
    _validate_limitations(claim)
    return claim_id, status, evidence_ids


def _validate_claims(
    root: dict,
    known_fallbacks: set[str],
    known_units: dict[str, dict[str, str]],
    observations_by_id: dict[str, dict],
) -> list[str]:

    claims = [_mapping(item, "claims[]") for item in _array(root["claims"], "claims")]
    if not claims:
        _fail("claims must not be empty")
    claim_ids = []
    statuses = []
    referenced_observation_ids = set()
    for claim in claims:
        claim_id, status, evidence_ids = _validate_claim(
            claim, known_fallbacks, known_units, observations_by_id
        )
        claim_ids.append(claim_id)
        statuses.append(status)
        referenced_observation_ids.update(evidence_ids)
    if claim_ids != sorted(set(claim_ids)):
        _fail("claim IDs must be unique and ASCII-sorted")
    if referenced_observation_ids != set(observations_by_id):
        _fail("every observation ID must be referenced by at least one claim")
    return statuses


def validate_result(repo_root: Path, value) -> dict:
    root = _mapping(value, "prototype result")
    _exact_keys(root, ROOT_KEYS, "prototype result")
    _validate_result_identity(root)
    source_path = _validate_source(repo_root, root)
    known_fallbacks, known_units = _known_contract_ids(repo_root)
    _, observations_by_id = _validate_observations(root, source_path)
    statuses = _validate_claims(root, known_fallbacks, known_units, observations_by_id)

    outcome = _string(root["outcome"], "outcome")
    if outcome not in OUTCOMES or outcome != _expected_outcome(statuses):
        _fail("outcome does not match claim statuses")
    return root


def load_task_result(repo_root: Path, task_id: str) -> dict:
    if TASK_RE.fullmatch(task_id) is None:
        _fail("requested task_id must be TASK-NNN")
    result_root = repo_root / "docs/research/residency-prototype-results/sha256"
    matches = []
    for path in sorted(result_root.glob("*.json")):
        raw = path.read_bytes()
        if (
            SHA256_RE.fullmatch(path.stem) is None
            or hashlib.sha256(raw).hexdigest() != path.stem
        ):
            _fail(f"{path.name} is not addressed by its exact bytes")
        value = validate_result(repo_root, _parse(raw, str(path)))
        canonical = (
            json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
            + "\n"
        ).encode("utf-8")
        if raw != canonical:
            _fail(f"{path.name} is not canonical UTF-8 JSON")
        if value["task_id"] == task_id:
            matches.append(value)
    if len(matches) != 1:
        _fail(f"{task_id} must have exactly one content-addressed result")
    return matches[0]
