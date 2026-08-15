"""Public contract for the exact later-promotion issue roster."""

from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
INVENTORY = (
    REPO_ROOT / "docs" / "research" / "portable-residency-capability-inventory.json"
)
RECEIPT = REPO_ROOT / "plan" / "evidence" / "phase-0" / "later-promotion-issues.json"
REPOSITORY = "nisavid/lemonade"
ROSTER_IDENTITY_SHA256 = (
    "fa45267d883cb402cec87b4633f261bdf72389857ea9b6af7d1324201dc6ffcd"
)

VULKAN_UNITS = [
    "H-VULKAN-ADM-GTT-HOST-v1",
    "H-VULKAN-PRE-GTT-HOST-v1",
    "H-VULKAN-STA-GTT-HOST-v1",
    "H-VULKAN-REC-GTT-HOST-OWN-v1",
]
WINDOWS_PARTICIPANTS = [
    "FLM-NPU-LLM",
    "FLM-NPU-EMBEDDING",
    "FLM-NPU-TRANSCRIPTION",
    "WHISPERCPP-NPU-TRANSCRIPTION",
    "RYZENAI-LLM-NPU-LLM",
]
WINDOWS_OPERATIONS = ["ADM", "LFR", "STA", "REC", "UNL", "PIN"]
EXPECTED_UNIT_IDS = VULKAN_UNITS + [
    f"W-XDNA2-{participant}-{operation}-v1"
    for participant in WINDOWS_PARTICIPANTS
    for operation in WINDOWS_OPERATIONS
]
EXPECTED_ROW_KEYS = {
    "compatibility_contracts",
    "constraints",
    "delivery_gate",
    "evidence_ceiling",
    "evidence_gate_set",
    "expected_roots",
    "fallbacks",
    "initial_state",
    "issue_id",
    "material_profiles",
    "recovery",
    "selector",
    "unit_id",
}


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate object key {key!r}")
        result[key] = value
    return result


def _load_json(path: Path) -> dict[str, object]:
    value = json.loads(
        path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys
    )
    if not isinstance(value, dict):
        raise TypeError(f"{path} must contain a JSON object")
    return value


def _unit_identity(unit: dict[str, object]) -> dict[str, object]:
    return {key: value for key, value in unit.items() if key != "issue_id"}


def _canonical_json(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def issue_title(unit: dict[str, object]) -> str:
    return f"Implement and qualify {unit['unit_id']}"


def issue_body(unit: dict[str, object]) -> str:
    unit_id = unit["unit_id"]
    accepted_contract = json.dumps(
        _unit_identity(unit), ensure_ascii=False, indent=2, sort_keys=True
    )
    return f"""## Objective

Implement and physically qualify `{unit_id}` exactly as frozen in the schema-v6 portable residency inventory. This issue does not authorize raising its capability ceiling.

## Details

```json
{accepted_contract}
```

## Checklist

- [ ] Implement only the frozen selector, operation leaves, and expected root.
- [ ] Preserve every bound constraint, recovery rule, and fail-closed fallback.
- [ ] Add physical qualification tests under the frozen test root.
- [ ] Produce evidence under the frozen output root and pass the evidence gate set.
- [ ] Satisfy the delivery gate without importing evidence from another identity.
- [ ] Leave the unit `unsupported` / `absent` and capped at `modeled`; promotion requires a separate inventory revision.

## Dependencies

- [ ] Complete Phase 0 in #38 before implementation begins.
- [ ] Satisfy TASK-107 authority prerequisites before physical qualification.

## Notes

The authoritative contract is `later_promotion_roster` in `docs/research/portable-residency-capability-inventory.json`. Do not infer omitted variants or broaden this unit.
"""


class ResidencyLaterPromotionRosterTest(unittest.TestCase):
    def test_exact_units_are_bound_to_unique_created_issues(self) -> None:
        inventory = _load_json(INVENTORY)
        self.assertEqual(inventory["schema_version"], 6)

        roster = inventory["later_promotion_roster"]
        self.assertIsInstance(roster, list)
        self.assertEqual(
            [unit["unit_id"] for unit in roster],
            EXPECTED_UNIT_IDS,
        )
        self.assertEqual(len(roster), 34)
        identities = [_unit_identity(unit) for unit in roster]
        self.assertEqual(
            hashlib.sha256(_canonical_json(identities)).hexdigest(),
            ROSTER_IDENTITY_SHA256,
        )

        issue_ids = []
        for unit in roster:
            self.assertEqual(set(unit), EXPECTED_ROW_KEYS)
            self.assertEqual(
                unit["initial_state"],
                {"capability_level": "unsupported", "delivery_state": "absent"},
            )
            self.assertEqual(unit["evidence_ceiling"], "modeled")
            self.assertEqual(
                set(unit["expected_roots"]),
                {"implementation", "tests", "outputs"},
            )
            self.assertTrue(all(unit["expected_roots"].values()))
            self.assertTrue(unit["expected_roots"]["outputs"].endswith(unit["unit_id"]))

            issue_id = unit["issue_id"]
            self.assertIs(type(issue_id), int)
            self.assertGreater(issue_id, 0)
            issue_ids.append(issue_id)

        self.assertEqual(len(issue_ids), len(set(issue_ids)))
        self.assertNotIn(
            "PRE",
            {
                unit["selector"]["operation_template"]
                for unit in roster
                if unit["unit_id"].startswith("W-XDNA2-")
            },
        )

        receipt = _load_json(RECEIPT)
        self.assertEqual(
            set(receipt),
            {"issues", "repository", "schema"},
        )
        self.assertEqual(
            receipt["schema"],
            "portable_residency_later_promotion_issue_receipts/v1",
        )
        self.assertEqual(receipt["repository"], REPOSITORY)
        self.assertIsInstance(receipt["issues"], list)
        self.assertEqual(len(receipt["issues"]), len(roster))

        for unit, issue in zip(roster, receipt["issues"], strict=True):
            self.assertEqual(
                set(issue),
                {
                    "body_sha256",
                    "issue_id",
                    "node_id",
                    "title",
                    "unit_id",
                    "url",
                },
            )
            issue_id = unit["issue_id"]
            self.assertEqual(issue["unit_id"], unit["unit_id"])
            self.assertEqual(issue["issue_id"], issue_id)
            self.assertEqual(
                issue["url"],
                f"https://github.com/{REPOSITORY}/issues/{issue_id}",
            )
            self.assertTrue(issue["node_id"])
            self.assertEqual(issue["title"], issue_title(unit))
            self.assertEqual(
                issue["body_sha256"],
                hashlib.sha256(issue_body(unit).encode("utf-8")).hexdigest(),
            )


if __name__ == "__main__":
    unittest.main()
