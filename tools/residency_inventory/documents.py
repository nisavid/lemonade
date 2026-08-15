"""Render, verify, and prepare generated inventory projections for locked updates."""

from __future__ import annotations

import html
import json
import re
import unicodedata
from pathlib import Path
from typing import Any

from .contract import fail
from .source import source_support_from_mapping, source_support_text

BEGIN_MARKER = "<!-- BEGIN GENERATED SUPPORT INVENTORY -->"
END_MARKER = "<!-- END GENERATED SUPPORT INVENTORY -->"
CAMPAIGN_BEGIN_MARKER = "<!-- BEGIN GENERATED HATCHERY EXACT CELLS -->"
CAMPAIGN_END_MARKER = "<!-- END GENERATED HATCHERY EXACT CELLS -->"
GENERATED_MARKERS = (
    BEGIN_MARKER,
    END_MARKER,
    CAMPAIGN_BEGIN_MARKER,
    CAMPAIGN_END_MARKER,
)


def render_source_support(value: dict[str, Any]) -> str:
    keys = sorted(source_support_from_mapping(value, "rendered source support"))
    return "<br>".join(render_code(source_support_text(key)) for key in keys)


def render_code_list(values: list[str]) -> str:
    return ", ".join(render_code(value) for value in values) or "—"


def render_fallbacks(fallbacks: dict[str, str]) -> str:
    return "<br>".join(
        f"{render_code(guard)} → {render_code(fallback_id)}"
        for guard, fallback_id in sorted(fallbacks.items())
    )


def render_mapping(values: dict[str, str]) -> str:
    return (
        "<br>".join(
            f"{render_code(key)} = {render_code(value)}"
            for key, value in sorted(values.items())
        )
        or "—"
    )


def render_profile_semantics(profile: dict[str, Any]) -> str:
    """Render every non-document component of one accepted profile identity."""

    semantic_fields = {
        key: value
        for key, value in profile.items()
        if key not in {"document", "evidence_document"}
    }
    if not semantic_fields:
        return "—"
    rows: list[str] = []
    for field, value in sorted(semantic_fields.items()):
        rendered_value = (
            value
            if isinstance(value, str)
            else json.dumps(value, sort_keys=True, separators=(",", ":"))
        )
        rows.append(f"{render_code(field)} = {render_code(rendered_value)}")
    return "<br>".join(rows)


def _normalize_rendered_text(value: Any) -> str:
    raw = str(value)
    if any(marker in raw for marker in GENERATED_MARKERS):
        fail("rendered value contains a reserved generated marker")
    unsupported_controls = sorted(
        {
            f"U+{ord(character):04X}"
            for character in raw
            if unicodedata.category(character) == "Cc"
            and character not in {"\t", "\r", "\n"}
        }
    )
    if unsupported_controls:
        fail(
            "rendered value contains an unsupported control character: "
            + ", ".join(unsupported_controls)
        )
    return raw.replace("\r\n", " ").replace("\r", " ").replace("\n", " ")


def escape_table_text(value: Any) -> str:
    """Render inert prose in a Markdown table cell."""

    normalized = _normalize_rendered_text(value)
    return (
        html.escape(normalized, quote=False)
        .replace("\\", "&#92;")
        .replace("|", "\\|")
        .replace("`", "&#96;")
    )


def render_code(value: Any) -> str:
    """Render a table-safe CommonMark code span containing arbitrary text."""

    normalized = _normalize_rendered_text(value).replace("|", "\\|")
    longest_run = max(
        (len(match.group(0)) for match in re.finditer(r"`+", normalized)),
        default=0,
    )
    fence = "`" * (longest_run + 1)
    ascii_space_only = bool(normalized) and normalized.strip(" ") == ""
    padding = (
        " "
        if not ascii_space_only
        and (normalized.startswith(("`", " ")) or normalized.endswith(("`", " ")))
        else ""
    )
    return f"{fence}{padding}{normalized}{padding}{fence}"


def _render_variant_rows(variants: list[dict[str, Any]]) -> list[str]:
    rows: list[str] = []
    for variant in variants:
        recipe_backend = f"{variant['recipe']}:{variant['backend']}"
        platform_ids = "<br>".join(render_code(value) for value in variant["platforms"])
        model_types = render_code_list(variant["model_types"])
        rows.append(
            "| "
            f"{render_code(variant['id'])} | "
            f"{render_code(recipe_backend)} | "
            f"{platform_ids} | {model_types} | "
            f"{render_code(variant['constraints'])} | "
            f"{render_code(variant['operations'])} | "
            f"{render_code(variant['recovery'])} | "
            f"{render_code(variant['evidence_ceiling'])} |"
        )
    return rows


def _render_exclusion_rows(exclusions: list[dict[str, Any]]) -> list[str]:
    rows: list[str] = []
    for exclusion in exclusions:
        source_rows = "<br>".join(
            render_source_support(value)
            for value in exclusion.get("source_support", [])
        )
        empty_recipes = render_code_list(exclusion.get("empty_support_recipes", []))
        non_descriptor = render_code_list(exclusion.get("non_descriptor_recipes", []))
        rows.append(
            f"| {render_code(exclusion['id'])} | {source_rows or '—'} | "
            f"{empty_recipes} | {non_descriptor} |"
        )
    return rows


def _render_profile_semantic_rows(
    profile_semantics: dict[str, dict[str, Any]],
) -> list[str]:
    rows: list[str] = []
    for registry_name, profiles in sorted(profile_semantics.items()):
        for profile_id, profile in sorted(profiles.items()):
            rows.append(
                f"| {render_code(registry_name)} | {render_code(profile_id)} | "
                f"{render_profile_semantics(profile)} |"
            )
    return rows


def render_inventory(projection: dict[str, Any]) -> str:
    lines = [
        BEGIN_MARKER,
        "### Frozen source closure",
        "",
        "| Field | Identity |",
        "| --- | --- |",
        (
            "| Support baseline | "
            f"{render_code(projection['source_support_baseline'])} |"
        ),
        *(
            f"| C++ source tree {render_code(path)} | {render_code(tree_id)} |"
            for path, tree_id in sorted(projection["source_tree_objects"].items())
        ),
        "",
        "### Backend variants",
        "",
        "| Variant ID | Recipe/backend | Platform predicates | Model types | Constraint profile | Operation set | Recovery profile | Evidence ceiling |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    lines.extend(_render_variant_rows(projection["variants"]))

    lines.extend(
        [
            "",
            "### Source-support exclusions",
            "",
            "| Exclusion ID | Frozen source-support dispositions | Empty-support recipes | Non-descriptor recipes |",
            "| --- | --- | --- | --- |",
        ]
    )
    lines.extend(_render_exclusion_rows(projection["exclusions"]))

    lines.extend(
        [
            "",
            "### Material profile semantic identities",
            "",
            "| Registry | Profile ID | Accepted non-document semantics |",
            "| --- | --- | --- |",
        ]
    )
    lines.extend(_render_profile_semantic_rows(projection["profile_semantics"]))

    lines.extend(
        [
            "",
            "### Material profile document bindings",
            "",
            "| Binding | Locator | SHA-256 |",
            "| --- | --- | --- |",
        ]
    )
    for binding_id, binding in sorted(projection["profile_bindings"].items()):
        lines.append(
            f"| {render_code(binding_id)} | {render_code(binding['locator'])} | "
            f"{render_code(binding['sha256'])} |"
        )

    lines.extend(
        [
            "",
            "### Coverage policy",
            "",
            "| Field | Accepted value |",
            "| --- | --- |",
        ]
    )
    for field, value in sorted(projection["coverage_policy"].items()):
        lines.append(f"| {render_code(field)} | {render_code(value)} |")

    lines.extend(
        [
            "",
            "### Validation suite registry",
            "",
            "| Suite ID | Operations | Proof |",
            "| --- | --- | --- |",
        ]
    )
    for suite_id, suite in projection["suite_registry"].items():
        lines.append(
            f"| {render_code(suite_id)} | {render_code_list(suite['operations'])} | "
            f"{escape_table_text(suite['proof'])} |"
        )

    lines.extend(
        [
            "",
            "### Validation suite sets",
            "",
            "| Suite set | Suites |",
            "| --- | --- |",
        ]
    )
    for suite_set_id, suite_ids in projection["suite_sets"].items():
        lines.append(f"| {render_code(suite_set_id)} | {render_code_list(suite_ids)} |")

    lines.extend(
        [
            "",
            "### Campaign base binding",
            "",
            "| Binding | Document | SHA-256 |",
            "| --- | --- | --- |",
            (
                "| Campaign base profile | "
                f"{render_code(projection['campaign_base_binding']['document'])} | "
                f"{render_code(projection['campaign_base_binding']['sha256'])} |"
            ),
            "",
            "### Gate source bindings",
            "",
            "| Source ID | Document | Section | Section SHA-256 |",
            "| --- | --- | --- | --- |",
        ]
    )
    for source_id, source in projection["gate_sources"].items():
        lines.append(
            f"| {render_code(source_id)} | {render_code(source['document'])} | "
            f"{escape_table_text(source['section'])} | "
            f"{render_code(source['section_sha256'])} |"
        )

    lines.extend(
        [
            "",
            "### Atomic campaign gate registry",
            "",
            "| Gate ID | Source | Applications | Suites |",
            "| --- | --- | --- | --- |",
        ]
    )
    for gate_id, gate in projection["gate_registry"].items():
        lines.append(
            f"| {render_code(gate_id)} | {render_code(gate['source'])} | "
            f"{render_code_list(gate['applications'])} | "
            f"{render_code_list(gate['suites'])} |"
        )

    lines.extend(
        [
            "",
            "### Flattened campaign gate sets",
            "",
            "| Gate set | Atomic gates |",
            "| --- | --- |",
        ]
    )
    for gate_set_id, gate_ids in projection["gate_sets"].items():
        lines.append(f"| {render_code(gate_set_id)} | {render_code_list(gate_ids)} |")

    lines.extend(["", "### Exact promoted-cell selectors", ""])
    lines.extend(render_exact_cell_table(projection["exact_cells"]))
    lines.extend(["", "### Compatibility promotion contracts", ""])
    lines.extend(
        render_compatibility_contract_table(projection["compatibility_contracts"])
    )
    lines.extend(
        [
            "",
            "### Later promotion roster",
            "",
            (
                "These entries freeze future physical implementation and "
                "qualification work only. Each remains `unsupported`, `absent`, "
                "and capped at `modeled`; completing an issue or its physical gate "
                "set cannot promote it. Raising a unit's ceiling requires an "
                "explicit new inventory revision."
            ),
            "",
        ]
    )
    lines.extend(render_later_promotion_table(projection["later_promotion_roster"]))
    lines.extend(
        [
            "",
            "### Promotion roster",
            "",
            "| Promotion unit kind | IDs |",
            "| --- | --- |",
            (
                "| Exact cells | "
                f"{render_code_list(projection['promotion_roster']['exact_cells'])} |"
            ),
            (
                "| Compatibility contracts | "
                f"{render_code_list(projection['promotion_roster']['compatibility_contracts'])} |"
            ),
        ]
    )
    lines.append(END_MARKER)
    return "\n".join(lines)


def render_exact_cell_table(exact_cells: list[dict[str, Any]]) -> list[str]:
    lines = [
        "| Cell ID | Base variant / platform | Operation | Scope | Model types | Constraints | Evidence ceiling / current state | Promotion target | Campaign gate set | Fallbacks |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for cell in exact_cells:
        match = cell["match"]
        model_types = render_code_list(match["model_types"])
        constraints = render_code_list(cell["constraints"])
        fallbacks = render_fallbacks(cell["fallbacks"])
        scope = escape_table_text(cell["scope"])
        promotion_target = escape_table_text(cell["promotion_target"])
        lines.append(
            f"| {render_code(cell['cell_id'])} | {render_code(cell['base_variant'])} / "
            f"{render_code(match['platform'])} / {render_code(match['backend_channel'])} | "
            f"{render_code(cell['operation_template'])} / "
            f"{render_code(cell['operation_leaf'])} | {scope} | {model_types} | {constraints} | "
            f"{render_code(cell['evidence_ceiling'])} / {render_code(cell['capability_level'])} / "
            f"{render_code(cell['delivery_state'])} | {promotion_target} | "
            f"{render_code(cell['campaign_gate_set'])} | {fallbacks} |"
        )
    return lines


def render_compatibility_contract_table(
    contracts: list[dict[str, Any]],
) -> list[str]:
    lines = [
        "| Contract ID | Platform cases | Operation | Scope | Coverage | Relation constraints | Evidence / current state | Runtime / evidence mode | Suite / gate set | Fallbacks |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for contract in contracts:
        platform_cases = "<br>".join(
            f"{render_code(case['platform'])}: "
            f"{render_code(case['coexist_by_type_variant'])} ↔ "
            f"{render_code(case['exclusive_variant'])}"
            for case in contract["platform_cases"]
        )
        coverage = (
            f"directions: {render_code_list(contract['directions'])}<br>"
            f"incumbents: {render_code_list(contract['incumbent_states'])}<br>"
            f"model types: {render_code(contract['model_type_coverage'])}"
        )
        lines.append(
            f"| {render_code(contract['contract_id'])} | {platform_cases} | "
            f"{render_code(contract['operation_template'])} / "
            f"{render_code(contract['operation_leaf'])} | "
            f"{escape_table_text(contract['scope'])} | {coverage} | "
            f"{render_code_list(contract['relation_constraints'])} | "
            f"{render_code(contract['evidence_ceiling'])} / "
            f"{render_code(contract['capability_level'])} / "
            f"{render_code(contract['delivery_state'])} | "
            f"{render_code(contract['runtime_authority'])} / "
            f"{render_code(contract['evidence_mode'])} | "
            f"{render_code(contract['suite_set'])} / "
            f"{render_code(contract['campaign_gate_set'])} | "
            f"{render_fallbacks(contract['fallbacks'])} |"
        )
    return lines


def render_later_promotion_table(units: list[dict[str, Any]]) -> list[str]:
    lines = [
        "| Unit ID / issue | Exact selector | Material profiles | Constraints / recovery | Initial state / ceiling | Delivery / evidence gates | Fallbacks / relation contracts | Expected roots |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for unit in units:
        selector = unit["selector"]
        issue = "—" if unit["issue_id"] is None else render_code(unit["issue_id"])
        selector_text = (
            f"{render_code(selector['base_variant'])} / "
            f"{render_code(selector['platform'])} / "
            f"{render_code(selector['backend_channel'])}<br>"
            f"{render_code(selector['model_type'])} / "
            f"{render_code(selector['operation_template'])}: "
            f"{render_code_list(selector['operation_leaves'])}"
        )
        state = unit["initial_state"]
        relation_contracts = render_code_list(unit["compatibility_contracts"])
        lines.append(
            f"| {render_code(unit['unit_id'])}<br>{issue} | {selector_text} | "
            f"{render_mapping(unit['material_profiles'])} | "
            f"{render_code_list(unit['constraints'])}<br>{render_code(unit['recovery'])} | "
            f"{render_code(state['capability_level'])} / "
            f"{render_code(state['delivery_state'])} / "
            f"{render_code(unit['evidence_ceiling'])} | "
            f"{render_code(unit['delivery_gate'])}<br>"
            f"{render_code(unit['evidence_gate_set'])} | "
            f"{render_fallbacks(unit['fallbacks'])}<br>relations: "
            f"{relation_contracts} | {render_mapping(unit['expected_roots'])} |"
        )
    return lines


def render_campaign(projection: dict[str, Any]) -> str:
    return "\n".join(
        [
            CAMPAIGN_BEGIN_MARKER,
            f"Support baseline: {render_code(projection['source_support_baseline'])}",
            "",
            "#### Runtime exact cells",
            "",
        ]
        + render_exact_cell_table(projection["exact_cells"])
        + ["", "#### Evidence-only compatibility contract", ""]
        + render_compatibility_contract_table(projection["compatibility_contracts"])
        + [
            "",
            "#### Later physical implementation and qualification roster",
            "",
            (
                "These units remain `unsupported`, `absent`, and capped at "
                "`modeled`. Issue completion and physical evidence do not raise "
                "that ceiling; promotion requires an explicit new inventory revision."
            ),
            "",
        ]
        + render_later_promotion_table(projection["later_promotion_roster"])
        + [CAMPAIGN_END_MARKER]
    )


def validate_rendered_projections(
    rendered_inventory: str, rendered_campaign: str
) -> None:
    """Reject reserved-marker collisions before any generated document is staged."""

    _validate_rendered_block(
        rendered_inventory,
        BEGIN_MARKER,
        END_MARKER,
        "support inventory",
    )
    _validate_rendered_block(
        rendered_campaign,
        CAMPAIGN_BEGIN_MARKER,
        CAMPAIGN_END_MARKER,
        "Hatchery exact cells",
    )


def _validate_rendered_block(
    rendered: str, begin_marker: str, end_marker: str, label: str
) -> None:
    if (
        rendered.count(begin_marker) != 1
        or rendered.count(end_marker) != 1
        or not rendered.startswith(begin_marker)
        or not rendered.endswith(end_marker)
    ):
        fail(f"rendered {label} block contains a reserved generated marker")
    for marker in GENERATED_MARKERS:
        if marker not in {begin_marker, end_marker} and marker in rendered:
            fail(f"rendered {label} block contains a reserved generated marker")


def validate_marked_block(
    path: Path, rendered: str, begin_marker: str, end_marker: str, label: str
) -> None:
    document = path.read_text(encoding="utf-8")
    validate_marked_document(document, rendered, begin_marker, end_marker, label)


def validate_marked_document(
    document: str,
    rendered: str,
    begin_marker: str,
    end_marker: str,
    label: str,
) -> None:
    """Verify one complete prospective or persisted generated document."""

    start, end = _marked_block_bounds(document, begin_marker, end_marker, label)
    if document[start:end] != rendered:
        fail(f"generated {label} block is stale; rerender it")


def replace_marked_block(
    document: str, rendered: str, begin_marker: str, end_marker: str, label: str
) -> str:
    start, end = _marked_block_bounds(document, begin_marker, end_marker, label)
    return document[:start] + rendered + document[end:]


def _marked_block_bounds(
    document: str, begin_marker: str, end_marker: str, label: str
) -> tuple[int, int]:
    if document.count(begin_marker) != 1 or document.count(end_marker) != 1:
        fail(f"{label} must contain exactly one generated block")
    start = document.index(begin_marker)
    end_start = document.index(end_marker)
    if end_start < start:
        fail(f"{label} end marker must follow its begin marker")
    return start, end_start + len(end_marker)
