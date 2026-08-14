"""Validate atomic campaign gates against their source documents."""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .contract import (
    OperationApplicability,
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)
from .markdown_sections import scan_markdown_headings, slice_markdown_section

EXPECTED_GATE_SOURCE_LOCATORS = {
    "hatchery_base": {
        "document": "docs/research/hatchery-residency-validation-profile.md",
        "section": "Accepted Hatchery validation matrix",
    },
    "hatchery_overlay": {
        "document": "docs/research/hatchery-campaign-parameters.md",
        "section": "Atomic overlay gate definitions",
    },
}
EXPECTED_OVERLAY_GATES = {
    "H-EXT-01",
    "H-NPU-CON-01",
    "H-NPU-CON-02",
    "H-NPU-REC-01",
    "H-NPU-TOP-01",
    "H-NPU-PROT-01",
    "H-NPU-PROT-02",
} | {f"H-LIV-01{suffix}" for suffix in "abcdefg"}
GATE_APPLICATIONS = {
    "exact_runtime",
    "exact_synthetic",
    "compatibility_synthetic",
}
EXPECTED_EVIDENCE_COMMON = {"H-EXP-01"} | {f"H-LIV-01{suffix}" for suffix in "abcdefg"}
EXPECTED_HATCHERY_COMMON = EXPECTED_EVIDENCE_COMMON | {"H-TOP-01"}
EXPECTED_COMPATIBILITY_GATES = EXPECTED_EVIDENCE_COMMON | {
    "H-NPU-TOP-01",
    "H-NPU-01",
    "H-NPU-PROT-01",
    "H-NPU-PROT-02",
    "H-NPU-CON-01",
    "H-NPU-CON-02",
    "H-EVD-01",
    "H-NPU-REC-01",
}
EXPECTED_RUNTIME_GATE_EXPANSIONS = {
    "hatchery_rocm_adm_v1": EXPECTED_HATCHERY_COMMON
    | {
        "H-FP-01",
        "H-ADM-01",
        "H-ADM-02",
        "H-ADM-03",
        "H-ADM-04",
        "H-GROW-01",
        "H-PROT-01",
        "H-PROT-02",
        "H-ORD-01",
        "H-CON-01",
        "H-CON-02",
        "H-EVD-01",
        "H-REC-01",
    },
    "hatchery_rocm_pre_v1": EXPECTED_HATCHERY_COMMON
    | {
        "H-FP-01",
        "H-PRE-01",
        "H-PRE-02",
        "H-PRE-03",
        "H-PRE-04",
        "H-EXT-01",
        "H-PROT-01",
        "H-PROT-02",
        "H-ORD-01",
        "H-CON-02",
        "H-EVD-01",
        "H-REC-01",
    },
    "hatchery_rocm_sta_v1": EXPECTED_HATCHERY_COMMON
    | {"H-FP-01", "H-STA-01", "H-CON-01", "H-CON-02", "H-EVD-01", "H-REC-01"},
    "hatchery_rocm_rec_v1": EXPECTED_HATCHERY_COMMON
    | {"H-EVD-01", "H-CON-02", "H-REC-01"},
}
EXPECTED_GATE_SUITES = {
    "H-TOP-01": {"PT-ID", "PT-TOP"},
    "H-FP-01": {"PT-FP"},
    **{f"H-ADM-0{index}": {"PT-ADM"} for index in range(1, 5)},
    "H-GROW-01": {"PT-ADM"},
    **{f"H-PRE-0{index}": {"PT-PRE"} for index in range(1, 5)},
    "H-PROT-01": {"PT-ADM", "PT-PRE"},
    "H-PROT-02": {"PT-ADM", "PT-PRE"},
    "H-ORD-01": {"PT-ADM", "PT-PRE"},
    "H-CON-01": {"PT-CON"},
    "H-CON-02": {"PT-CON"},
    "H-EVD-01": {"PT-SIG"},
    "H-STA-01": {"PT-STA"},
    "H-NPU-01": {"PT-NPU"},
    "H-REC-01": {"PT-REC", "PT-ART"},
    "H-EXP-01": {"PT-EXP"},
    "H-EXT-01": {"PT-PRE"},
    "H-NPU-TOP-01": {"PT-ID", "PT-TOP"},
    "H-NPU-PROT-01": {"PT-NPU"},
    "H-NPU-PROT-02": {"PT-NPU"},
    "H-NPU-CON-01": {"PT-CON"},
    "H-NPU-CON-02": {"PT-CON"},
    "H-NPU-REC-01": {"PT-REC"},
    **{f"H-LIV-01{suffix}": {"PT-LIV"} for suffix in "abcdefg"},
}
DEFAULT_GATE_APPLICATIONS = frozenset({"exact_runtime"})
GATE_APPLICATION_OVERRIDES = {
    "H-ADM-03": frozenset({"exact_runtime", "exact_synthetic"}),
    "H-ORD-01": frozenset({"exact_runtime", "exact_synthetic"}),
    "H-REC-01": frozenset({"exact_runtime", "exact_synthetic"}),
    "H-CON-01": frozenset({"exact_runtime", "exact_synthetic"}),
    "H-CON-02": frozenset({"exact_runtime", "exact_synthetic"}),
    **{gate_id: frozenset(GATE_APPLICATIONS) for gate_id in ("H-EVD-01", "H-EXP-01")},
    **{
        gate_id: frozenset({"compatibility_synthetic"})
        for gate_id in (
            "H-NPU-01",
            "H-NPU-CON-01",
            "H-NPU-CON-02",
            "H-NPU-TOP-01",
            "H-NPU-PROT-01",
            "H-NPU-PROT-02",
            "H-NPU-REC-01",
        )
    },
    **{f"H-LIV-01{suffix}": frozenset(GATE_APPLICATIONS) for suffix in "abcdefg"},
}
EXPECTED_GATE_APPLICATIONS = {
    gate_id: GATE_APPLICATION_OVERRIDES.get(gate_id, DEFAULT_GATE_APPLICATIONS)
    for gate_id in EXPECTED_GATE_SUITES
}

GATE_TABLE_HEADER = ["ID", "Fixture", "Applications", "Trigger", "Required result"]
EXPECTED_COMPATIBILITY_SAFETY_SEMANTICS = {
    "H-NPU-01": (
        "S NPU compatibility relation fixture",
        (
            "Every source-derived platform case and incoming direction with "
            "unpinned-idle, pinned, and in-use incumbents"
        ),
        (
            "Classify the `npu_cross_family` conflict, preserve every incumbent, and "
            "refuse the incoming load before eviction or backend spawn; "
            "unpinned-idle state grants no displacement authority."
        ),
    ),
    "H-NPU-TOP-01": (
        "S from frozen XDNA2 source closure",
        "Derive the FLM and exclusive-NPU platform intersections",
        (
            "Prove the exact shared XDNA2 compatibility domain without inferring "
            "byte capacity, pressure, readiness, or recovery authority."
        ),
    ),
    "H-NPU-PROT-01": (
        "S NPU compatibility fixture",
        "A conflicting incumbent is pinned",
        (
            "Preserve the incumbent and refuse the incoming load before eviction or "
            "backend spawn."
        ),
    ),
    "H-NPU-PROT-02": (
        "S NPU compatibility fixture",
        "A conflicting incumbent is in use",
        (
            "Preserve the request and incumbent and refuse the incoming load before "
            "eviction or backend spawn."
        ),
    ),
    "H-NPU-CON-01": (
        "S NPU compatibility concurrency fixture",
        (
            "Concurrent conflict decisions over the same relation, covering both "
            "incoming directions"
        ),
        (
            "Serialize relation classification and return the registered fail-closed "
            "preserve/refuse decision for each attempt with zero eviction and zero "
            "backend spawn; no partial relation state may publish."
        ),
    ),
    "H-NPU-CON-02": (
        "S NPU compatibility stale-relation fixture",
        "Relation evidence changes after classification and before decision commit",
        (
            "Reject the stale relation token, recompute from current relation evidence, "
            "and fail closed by preserving every incumbent and refusing the incoming "
            "load with zero eviction and zero backend spawn."
        ),
    ),
    "H-NPU-REC-01": (
        "S NPU compatibility recovery-state fixture",
        "Simulated relation-evidence loss or unknown relation state",
        (
            "Return the registered fail-closed compatibility response and preserve "
            "every participant; do not clean up a runtime, replay claims, recover a "
            "participant, or assert live recovery authority."
        ),
    ),
}
EXPECTED_CAMPAIGN_BASE_DOCUMENT = (
    "docs/research/hatchery-residency-validation-profile.md"
)
CAMPAIGN_DOCUMENT = "docs/research/hatchery-campaign-parameters.md"


@dataclass(frozen=True)
class GateDefinition:
    """One complete atomic row parsed from a bound Markdown source section."""

    gate_id: str
    fixture: str
    applications: tuple[str, ...]
    trigger: str
    required_result: str

    @property
    def semantic_key(self) -> tuple[str, str, str]:
        """Return semantics whose duplication would make two IDs ambiguous."""

        return (self.fixture, self.trigger, self.required_result)


@dataclass(frozen=True)
class GateValidation:
    """Fully expanded atomic gates and the promotion sets that consume them."""

    flattened_sets: dict[str, list[str]]
    used_gate_sets: set[str]
    source_bindings: dict[str, dict[str, Any]]
    registry: dict[str, dict[str, Any]]
    campaign_base_binding: dict[str, str]


@dataclass(frozen=True)
class PromotionGateRequirement:
    """One promotion unit's operation, suites, and gate-set binding."""

    unit_id: str
    unit_kind: str
    operation: str
    suite_set_id: str
    gate_set_id: str


def reject_range_shorthand(value: Any, label: str) -> None:
    if isinstance(value, str):
        if ".." in value:
            fail(f"{label} may not contain '..' range shorthand")
        return
    if isinstance(value, list):
        for index, member in enumerate(value):
            reject_range_shorthand(member, f"{label}[{index}]")
        return
    if isinstance(value, dict):
        for key, member in value.items():
            reject_range_shorthand(key, f"{label} key")
            reject_range_shorthand(member, f"{label}.{key}")


def markdown_section(document: str, title: str, label: str) -> str:
    headings = scan_markdown_headings(document)
    matches = [heading for heading in headings if heading.title == title]
    if len(matches) != 1:
        fail(f"{label} must contain exactly one section named {title!r}")
    return slice_markdown_section(document, headings, matches[0], include_heading=False)


def _table_cells(line: str, label: str) -> list[str]:
    if not line.startswith("|") or not line.rstrip().endswith("|"):
        fail(f"{label} must be a pipe-delimited Markdown table row")
    cells = [cell.strip() for cell in line.strip().split("|")[1:-1]]
    if len(cells) != len(GATE_TABLE_HEADER):
        fail(f"{label} must contain exactly {len(GATE_TABLE_HEADER)} columns")
    return cells


def gate_table_rows(source_id: str, section: str) -> list[tuple[int, str]]:
    """Return numbered atomic rows from the source section's sole gate table."""

    lines = section.splitlines()
    header_indexes = [
        index
        for index, line in enumerate(lines)
        if line.startswith("|")
        and _table_cells(line, f"gate source {source_id} table header")
        == GATE_TABLE_HEADER
    ]
    if len(header_indexes) != 1:
        fail(f"gate source {source_id} must contain exactly one gate definition table")
    header_index = header_indexes[0]
    if header_index + 1 >= len(lines):
        fail(f"gate source {source_id} gate table has no separator")
    separator = _table_cells(
        lines[header_index + 1], f"gate source {source_id} table separator"
    )
    if any(re.fullmatch(r":?-{3,}:?", cell) is None for cell in separator):
        fail(f"gate source {source_id} gate table separator is malformed")

    rows: list[tuple[int, str]] = []
    for row_index, line in enumerate(lines[header_index + 2 :], start=1):
        if not line.startswith("|"):
            break
        rows.append((row_index, line))
    if not rows:
        fail(f"gate source {source_id} section contains no atomic gate rows")
    return rows


def parse_gate_definition(source_id: str, row_index: int, line: str) -> GateDefinition:
    """Parse and validate one complete atomic gate row."""

    gate_id, fixture, raw_applications, trigger, required_result = _table_cells(
        line, f"gate source {source_id} row {row_index}"
    )
    if re.fullmatch(r"H-[A-Z]+(?:-[A-Z]+)*-[0-9]+[a-z]?", gate_id) is None:
        fail(f"gate source {source_id} row {row_index} has invalid gate ID")
    if not fixture or not trigger or not required_result:
        fail(f"gate source {source_id} gate {gate_id} has an empty semantic field")
    applications = tuple(
        application.strip() for application in raw_applications.split(",")
    )
    if not applications or any(not application for application in applications):
        fail(f"gate source {source_id} gate {gate_id} has no applications")
    if len(applications) != len(set(applications)):
        fail(f"gate source {source_id} gate {gate_id} repeats an application")
    unknown = set(applications) - GATE_APPLICATIONS
    if unknown:
        fail(
            f"gate source {source_id} gate {gate_id} has unknown applications "
            f"{sorted(unknown)}"
        )
    return GateDefinition(
        gate_id=gate_id,
        fixture=fixture,
        applications=applications,
        trigger=trigger,
        required_result=required_result,
    )


def parse_source_gates(source_id: str, section: str) -> dict[str, GateDefinition]:
    """Parse the source section into a closed, unambiguous atomic registry."""

    definitions: dict[str, GateDefinition] = {}
    semantics: dict[tuple[str, str, str], str] = {}
    for row_index, line in gate_table_rows(source_id, section):
        definition = parse_gate_definition(source_id, row_index, line)
        gate_id = definition.gate_id
        if gate_id in definitions:
            fail(f"gate source {source_id} repeats atomic gate ID {gate_id}")
        if definition.semantic_key in semantics:
            fail(
                f"gate source {source_id} gates {semantics[definition.semantic_key]} "
                f"and {gate_id} duplicate gate definition semantics"
            )
        semantics[definition.semantic_key] = gate_id
        definitions[gate_id] = definition
    return definitions


def validate_compatibility_safety_semantics(
    definitions: dict[str, GateDefinition],
) -> None:
    """Exact-bind compatibility gates that could otherwise imply live effects."""

    for gate_id, expected in EXPECTED_COMPATIBILITY_SAFETY_SEMANTICS.items():
        definition = definitions[gate_id]
        actual = (
            definition.fixture,
            definition.trigger,
            definition.required_result,
        )
        if actual == expected:
            continue
        if gate_id == "H-NPU-REC-01":
            fail(
                "H-NPU-REC-01 must equal its accepted relation-only recovery "
                "definition"
            )
        fail(f"{gate_id} must equal its accepted compatibility safety definition")


def validate_gate_source(
    repo: Path, source_id: str, raw_source: Any
) -> tuple[dict[str, GateDefinition], dict[str, Any]]:
    """Validate one content-bound gate source and return its atomic rows."""

    source = require_mapping(raw_source, f"gate_sources.{source_id}")
    require_exact_keys(
        source,
        {"document", "section", "section_sha256"},
        f"gate_sources.{source_id}",
    )
    document_path = require_string(
        source["document"], f"gate_sources.{source_id}.document"
    )
    section_title = require_string(
        source["section"], f"gate_sources.{source_id}.section"
    )
    locator = {"document": document_path, "section": section_title}
    if locator != EXPECTED_GATE_SOURCE_LOCATORS[source_id]:
        fail(
            f"gate_sources.{source_id} must equal its accepted document and "
            "section locator"
        )
    reject_range_shorthand(source, f"gate_sources.{source_id}")
    path = Path(document_path)
    if path.is_absolute() or ".." in path.parts:
        fail(f"gate_sources.{source_id}.document must be repository-relative")
    try:
        document = (repo / path).read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read gate source {document_path}: {error}")
    section = markdown_section(document, section_title, f"gate source {document_path}")
    reject_range_shorthand(section, f"gate source {document_path} section")
    expected_sha256 = require_string(
        source["section_sha256"], f"gate_sources.{source_id}.section_sha256"
    )
    if re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is None:
        fail(f"gate_sources.{source_id}.section_sha256 must be a full SHA-256")
    actual_sha256 = hashlib.sha256(section.encode()).hexdigest()
    if actual_sha256 != expected_sha256:
        fail(f"gate_sources.{source_id}.section_sha256 does not match its section")
    return parse_source_gates(source_id, section), dict(source)


def validate_gate_definition_closure(
    parsed: dict[str, dict[str, GateDefinition]],
) -> None:
    """Validate source ownership, semantics, and accepted applications together."""

    expected_ids = {
        "hatchery_base": set(EXPECTED_GATE_SUITES) - EXPECTED_OVERLAY_GATES,
        "hatchery_overlay": EXPECTED_OVERLAY_GATES,
    }
    for source_id, definitions in parsed.items():
        if set(definitions) != expected_ids[source_id]:
            fail(f"gate source {source_id} does not contain its accepted atomic IDs")
    all_definitions = {
        gate_id: definition
        for definitions in parsed.values()
        for gate_id, definition in definitions.items()
    }
    semantic_ids: dict[tuple[str, str, str], str] = {}
    for gate_id, definition in all_definitions.items():
        if definition.semantic_key in semantic_ids:
            fail(
                f"gate sources define {semantic_ids[definition.semantic_key]} and "
                f"{gate_id} with duplicate gate definition semantics"
            )
        semantic_ids[definition.semantic_key] = gate_id
    actual_applications = {
        gate_id: set(definition.applications)
        for gate_id, definition in all_definitions.items()
    }
    if actual_applications != EXPECTED_GATE_APPLICATIONS:
        fail("gate definitions must equal their accepted applications")
    validate_compatibility_safety_semantics(all_definitions)


def validate_gate_sources(
    repo: Path, gate_sources: dict[str, Any]
) -> tuple[dict[str, dict[str, GateDefinition]], dict[str, dict[str, Any]]]:
    require_registry_keys(gate_sources, "gate_sources")
    if set(gate_sources) != set(EXPECTED_GATE_SOURCE_LOCATORS):
        fail("gate_sources must contain exactly hatchery_base and hatchery_overlay")
    parsed: dict[str, dict[str, GateDefinition]] = {}
    normalized_sources: dict[str, dict[str, Any]] = {}
    for source_id, raw_source in gate_sources.items():
        parsed[source_id], normalized_sources[source_id] = validate_gate_source(
            repo, source_id, raw_source
        )
    validate_gate_definition_closure(parsed)
    return parsed, normalized_sources


def validate_campaign_base_binding(
    repo: Path, inventory: dict[str, Any]
) -> dict[str, str]:
    """Bind the campaign overlay to the complete canonical base-profile text."""

    binding = require_mapping(
        inventory.get("campaign_base_binding"), "campaign_base_binding"
    )
    require_exact_keys(binding, {"document", "sha256"}, "campaign_base_binding")
    document = require_string(binding["document"], "campaign_base_binding.document")
    expected_sha256 = require_string(binding["sha256"], "campaign_base_binding.sha256")
    if document != EXPECTED_CAMPAIGN_BASE_DOCUMENT:
        fail("campaign_base_binding.document must equal the accepted base profile")
    if re.fullmatch(r"[0-9a-f]{64}", expected_sha256) is None:
        fail("campaign_base_binding.sha256 must be a full SHA-256")
    try:
        with (repo / document).open(encoding="utf-8", newline=None) as stream:
            base_profile = stream.read()
        with (repo / CAMPAIGN_DOCUMENT).open(encoding="utf-8", newline=None) as stream:
            campaign = stream.read()
        actual_sha256 = hashlib.sha256(base_profile.encode("utf-8")).hexdigest()
    except (OSError, UnicodeDecodeError) as error:
        fail(f"cannot read campaign base binding inputs: {error}")
    if actual_sha256 != expected_sha256:
        fail("campaign_base_binding.sha256 does not match the complete base profile")
    citation = (
        "[Hatchery residency validation profile]"
        "(hatchery-residency-validation-profile.md) at SHA-256 "
        f"`{expected_sha256}`"
    )
    if campaign.count(citation) != 1:
        fail("campaign base profile citation must equal campaign_base_binding")
    return {"document": document, "sha256": expected_sha256}


def flatten_gate_sets(
    gate_sets: dict[str, Any], gate_registry: dict[str, Any]
) -> tuple[dict[str, list[str]], dict[str, set[str]]]:
    require_registry_keys(gate_sets, "gate_sets")
    normalized: dict[str, tuple[list[str], list[str]]] = {}
    for gate_set_id, raw_gate_set in gate_sets.items():
        gate_set = require_mapping(raw_gate_set, f"gate_sets.{gate_set_id}")
        require_exact_keys(gate_set, {"extends", "members"}, f"gate_sets.{gate_set_id}")
        extends = require_string_list(
            gate_set["extends"], f"gate_sets.{gate_set_id}.extends", nonempty=False
        )
        members = require_string_list(
            gate_set["members"], f"gate_sets.{gate_set_id}.members", nonempty=False
        )
        reject_range_shorthand(gate_set, f"gate_sets.{gate_set_id}")
        unknown_members = set(members) - set(gate_registry)
        if unknown_members:
            fail(
                f"gate set {gate_set_id} references unknown gates "
                f"{sorted(unknown_members)}"
            )
        normalized[gate_set_id] = (extends, members)

    flattened: dict[str, list[str]] = {}
    dependencies: dict[str, set[str]] = {}
    active: list[str] = []

    def visit(gate_set_id: str) -> list[str]:
        if gate_set_id in flattened:
            return flattened[gate_set_id]
        if gate_set_id not in normalized:
            fail(f"gate set references unknown parent {gate_set_id}")
        if gate_set_id in active:
            cycle = " -> ".join(active + [gate_set_id])
            fail(f"gate set inheritance cycle: {cycle}")
        active.append(gate_set_id)
        extends, members = normalized[gate_set_id]
        expanded: list[str] = []
        dependencies[gate_set_id] = {gate_set_id}
        for parent_id in extends:
            expanded.extend(visit(parent_id))
            dependencies[gate_set_id].update(dependencies[parent_id])
        expanded.extend(members)
        if len(expanded) != len(set(expanded)):
            fail(f"gate set {gate_set_id} expands to duplicate atomic gates")
        active.pop()
        flattened[gate_set_id] = expanded
        return expanded

    for gate_set_id in normalized:
        visit(gate_set_id)
    return flattened, dependencies


def collect_source_gate_ownership(
    parsed_sources: dict[str, dict[str, GateDefinition]],
) -> tuple[dict[str, str], dict[str, GateDefinition]]:
    """Flatten source registries while rejecting cross-source atomic ID reuse."""

    expected_sources: dict[str, str] = {}
    definitions: dict[str, GateDefinition] = {}
    for source_id, source_definitions in parsed_sources.items():
        gate_ids = set(source_definitions)
        overlap = set(expected_sources) & gate_ids
        if overlap:
            fail(f"gate sources repeat atomic IDs {sorted(overlap)}")
        expected_sources.update({gate_id: source_id for gate_id in gate_ids})
        definitions.update(source_definitions)
    return expected_sources, definitions


def validate_gate_registry(
    gate_registry: dict[str, Any],
    parsed_sources: dict[str, dict[str, GateDefinition]],
    suite_operations: dict[str, OperationApplicability],
) -> tuple[dict[str, set[str]], dict[str, dict[str, Any]], dict[str, GateDefinition]]:
    """Validate atomic source and suite assignments and normalize the registry."""

    require_registry_keys(gate_registry, "gate_registry")
    reject_range_shorthand(gate_registry, "gate_registry")
    expected_sources, definitions = collect_source_gate_ownership(parsed_sources)
    if set(gate_registry) != set(expected_sources):
        fail("gate_registry does not exactly match the atomic IDs in gate_sources")
    gate_suites: dict[str, set[str]] = {}
    for gate_id, raw_gate in gate_registry.items():
        gate = require_mapping(raw_gate, f"gate_registry.{gate_id}")
        require_exact_keys(gate, {"source", "suites"}, f"gate_registry.{gate_id}")
        source_id = require_string(gate["source"], f"gate_registry.{gate_id}.source")
        if source_id != expected_sources[gate_id]:
            fail(f"gate_registry.{gate_id}.source disagrees with its source document")
        suites = set(
            require_string_list(gate["suites"], f"gate_registry.{gate_id}.suites")
        )
        unknown_suites = suites - set(suite_operations)
        if unknown_suites:
            fail(
                f"gate_registry.{gate_id} references unknown suites "
                f"{sorted(unknown_suites)}"
            )
        gate_suites[gate_id] = suites
    if gate_suites != EXPECTED_GATE_SUITES:
        fail("gate_registry suites must equal the accepted atomic gate assignments")
    normalized_registry = {
        gate_id: {
            "source": gate_registry[gate_id]["source"],
            "applications": list(definitions[gate_id].applications),
            "suites": list(gate_registry[gate_id]["suites"]),
        }
        for gate_id in gate_registry
    }
    return gate_suites, normalized_registry, definitions


def validate_gate_set_contract(
    flattened: dict[str, list[str]],
) -> tuple[set[str], set[str]]:
    """Validate common and operation-specific gate-set expansions."""

    for common_set_id in ("evidence_common_v1", "hatchery_common_v1"):
        if common_set_id not in flattened:
            fail(f"gate_sets must define {common_set_id}")
    evidence_common = set(flattened["evidence_common_v1"])
    hatchery_common = set(flattened["hatchery_common_v1"])
    if evidence_common != EXPECTED_EVIDENCE_COMMON:
        fail("evidence_common_v1 must equal the accepted evidence-common gates")
    if hatchery_common != EXPECTED_HATCHERY_COMMON:
        fail("hatchery_common_v1 must equal evidence_common_v1 plus H-TOP-01")
    for gate_set_id, expected_gates in EXPECTED_RUNTIME_GATE_EXPANSIONS.items():
        if set(flattened.get(gate_set_id, [])) != expected_gates:
            fail(f"gate set {gate_set_id} must equal its accepted runtime expansion")
    return evidence_common, hatchery_common


def validate_compatibility_gate_atoms(
    requirement: PromotionGateRequirement, unit_gates: set[str]
) -> None:
    """Keep compatibility proof atoms separate from runtime-cell topology."""

    if requirement.unit_kind != "compatibility_contract":
        return
    if "H-TOP-01" in unit_gates:
        fail(f"promotion unit {requirement.unit_id} may not import H-TOP-01")
    if unit_gates != EXPECTED_COMPATIBILITY_GATES:
        fail(
            f"promotion unit {requirement.unit_id} must equal the accepted "
            "compatibility gate expansion"
        )


def validate_requirement_suite_coverage(
    requirement: PromotionGateRequirement,
    unit_gates: set[str],
    *,
    gate_suites: dict[str, set[str]],
    suite_operations: dict[str, OperationApplicability],
    normalized_suite_sets: dict[str, list[str]],
) -> None:
    """Require atomic gates to cover every applicable proof suite."""

    applicable_suites = {
        suite_id
        for suite_id in normalized_suite_sets[requirement.suite_set_id]
        if suite_operations[suite_id].is_wildcard
        or requirement.operation in suite_operations[suite_id].operations
    }
    covered_suites = set().union(*(gate_suites[gate_id] for gate_id in unit_gates))
    missing_suites = applicable_suites - covered_suites
    if missing_suites:
        fail(
            f"promotion unit {requirement.unit_id} gates do not cover suites "
            f"{sorted(missing_suites)}"
        )


def validate_promotion_requirement(
    requirement: PromotionGateRequirement,
    *,
    flattened: dict[str, list[str]],
    dependencies: dict[str, set[str]],
    definitions: dict[str, GateDefinition],
    gate_suites: dict[str, set[str]],
    evidence_common: set[str],
    hatchery_common: set[str],
    suite_operations: dict[str, OperationApplicability],
    normalized_suite_sets: dict[str, list[str]],
) -> set[str]:
    """Validate one promotion unit's complete gate application and coverage."""

    if requirement.gate_set_id not in flattened:
        fail(
            f"promotion unit {requirement.unit_id} references unknown gate set "
            f"{requirement.gate_set_id}"
        )
    if requirement.suite_set_id not in normalized_suite_sets:
        fail(
            f"promotion unit {requirement.unit_id} references unknown suite set "
            f"{requirement.suite_set_id}"
        )
    unit_gates = set(flattened[requirement.gate_set_id])
    is_exact_cell = requirement.unit_kind == "exact_cell"
    required_application = (
        "exact_runtime" if is_exact_cell else "compatibility_synthetic"
    )
    ineligible_gates = sorted(
        gate_id
        for gate_id in unit_gates
        if required_application not in definitions[gate_id].applications
    )
    if ineligible_gates:
        fail(
            f"promotion unit {requirement.unit_id} uses gates ineligible for "
            f"{required_application}: {ineligible_gates}"
        )
    common_set_id = "hatchery_common_v1" if is_exact_cell else "evidence_common_v1"
    required_common = (
        hatchery_common if is_exact_cell else evidence_common | {"H-NPU-TOP-01"}
    )
    if common_set_id not in dependencies[requirement.gate_set_id]:
        fail(
            f"promotion unit {requirement.unit_id} does not inherit required "
            f"common gate set {common_set_id}"
        )
    if not required_common <= unit_gates:
        fail(f"promotion unit {requirement.unit_id} omits required common gate atoms")
    validate_compatibility_gate_atoms(requirement, unit_gates)
    validate_requirement_suite_coverage(
        requirement,
        unit_gates,
        gate_suites=gate_suites,
        suite_operations=suite_operations,
        normalized_suite_sets=normalized_suite_sets,
    )
    return unit_gates


def validate_gates(
    repo: Path,
    inventory: dict[str, Any],
    requirements: list[PromotionGateRequirement],
    *,
    suite_operations: dict[str, OperationApplicability],
    normalized_suite_sets: dict[str, list[str]],
) -> GateValidation:
    gate_sources = require_mapping(inventory.get("gate_sources"), "gate_sources")
    parsed_sources, normalized_sources = validate_gate_sources(repo, gate_sources)
    campaign_base_binding = validate_campaign_base_binding(repo, inventory)

    gate_registry = require_mapping(inventory.get("gate_registry"), "gate_registry")
    gate_suites, normalized_registry, definitions = validate_gate_registry(
        gate_registry, parsed_sources, suite_operations
    )

    gate_sets = require_mapping(inventory.get("gate_sets"), "gate_sets")
    flattened, dependencies = flatten_gate_sets(gate_sets, gate_registry)
    evidence_common, hatchery_common = validate_gate_set_contract(flattened)
    used_gate_sets: set[str] = set()
    used_atoms: set[str] = set()
    for requirement in requirements:
        unit_gates = validate_promotion_requirement(
            requirement,
            flattened=flattened,
            dependencies=dependencies,
            definitions=definitions,
            gate_suites=gate_suites,
            evidence_common=evidence_common,
            hatchery_common=hatchery_common,
            suite_operations=suite_operations,
            normalized_suite_sets=normalized_suite_sets,
        )
        used_gate_sets.update(dependencies[requirement.gate_set_id])
        used_atoms.update(unit_gates)
    if used_gate_sets != set(gate_sets):
        fail(
            "gate_sets contains orphan entries: "
            f"{sorted(set(gate_sets) - used_gate_sets)}"
        )
    if used_atoms != set(gate_registry):
        fail(
            "gate_registry contains unused atomic gates: "
            f"{sorted(set(gate_registry) - used_atoms)}"
        )
    return GateValidation(
        flattened_sets=flattened,
        used_gate_sets=used_gate_sets,
        source_bindings=normalized_sources,
        registry=normalized_registry,
        campaign_base_binding=campaign_base_binding,
    )
