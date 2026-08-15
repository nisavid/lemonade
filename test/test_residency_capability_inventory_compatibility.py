"""Compatibility-contract tests for the residency inventory validator."""

from __future__ import annotations

import unittest

from residency_capability_inventory_cli_case import (
    REPO_ROOT,
    ResidencyCapabilityInventoryCliCase,
)


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise compatibility-contract behavior through the CLI."""

    def test_matrix_documents_exact_source_and_recoverable_update_contract(
        self,
    ) -> None:
        matrix = (
            REPO_ROOT / "docs" / "research" / "portable-residency-capability-matrix.md"
        ).read_text(encoding="utf-8")

        self.assertNotIn("git fetch upstream --prune --tags", matrix)
        self.assertIn(
            "The `--update` path is a locked, crash-consistent and recoverable "
            "two-document update",
            matrix,
        )
        self.assertIn(
            "Validator and render clients share the inventory lock and refuse "
            "pending journals",
            matrix,
        )
        self.assertIn("may briefly observe mixed projections", matrix)
        self.assertIn("does not provide atomic cross-file visibility", matrix)
        self.assertIn("The next `--update` recovers", matrix)
        self.assertIn("Material Markdown bindings use canonical UTF-8 text", matrix)
        self.assertIn("includes the selected heading line", matrix)

    def test_compatibility_contract_must_include_evidence_common_gates(self) -> None:
        def omit_evidence_common(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"]["extends"] = []

        self._replace_inventory(omit_evidence_common)

        result = self._run_cli("--render")

        self.assert_invalid(result, "does not inherit required common gate set")

    def test_compatibility_contract_must_include_npu_topology_gate(self) -> None:
        def omit_npu_topology(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"]["members"].remove(
                "H-NPU-TOP-01"
            )

        self._replace_inventory(omit_npu_topology)

        result = self._run_cli("--render")

        self.assert_invalid(result, "omits required common gate atoms")

    def test_compatibility_platform_cases_are_source_derived(self) -> None:
        def drop_case(inventory: dict[str, object]) -> None:
            inventory["compatibility_contracts"][0]["platform_cases"].pop()

        self._replace_inventory(drop_case)

        result = self._run_cli("--render")

        self.assert_invalid(result, "cases do not match")

    def test_compatibility_fallback_guard_is_closed(self) -> None:
        def rename_fallback_guard(inventory: dict[str, object]) -> None:
            contract = inventory["compatibility_contracts"][0]
            fallback_id = contract["fallbacks"].pop(
                "insufficient_displacement_authority"
            )
            contract["fallbacks"]["unproven_compatibility_authority"] = fallback_id

        self._replace_inventory(rename_fallback_guard)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted displacement-authority fallback")

    def test_npu_operation_contract_rejects_pressure_reclamation(self) -> None:
        def add_npu_pressure(inventory: dict[str, object]) -> None:
            inventory["operation_sets"]["npu_compatibility"].append("PRE")

        self._replace_inventory(add_npu_pressure)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted GPU and NPU operation contracts")

    def test_npu_compatibility_rejects_memory_capacity_constraints(self) -> None:
        def add_memory_constraint(inventory: dict[str, object]) -> None:
            inventory["constraint_profiles"]["npu_flm"].append("gpu_shared_residency")

        self._replace_inventory(add_memory_constraint)

        result = self._run_cli("--render")

        self.assert_invalid(result, "constraint_profiles must equal")

    def test_compatibility_suite_includes_relation_only_artifact_closure(self) -> None:
        suites = self.inventory["suite_sets"]["npu_compatibility"]
        artifact_gate = self.inventory["gate_registry"]["H-NPU-ART-01"]

        self.assertIn("PT-REC", suites)
        self.assertIn("PT-ART", suites)
        self.assertEqual(artifact_gate["source"], "hatchery_overlay")
        self.assertEqual(artifact_gate["suites"], ["PT-ART"])

    def test_recovery_suite_distinguishes_runtime_and_relation_proofs(self) -> None:
        recovery_suite = self.inventory["suite_registry"]["PT-REC"]

        self.assertEqual(recovery_suite["operations"], ["REC", "NPC"])
        self.assertEqual(
            recovery_suite["proof"],
            "runtime ownership replay and verified release, or "
            "compatibility-synthetic fail-closed relation-state response",
        )

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "| `PT-REC` | `REC`, `NPC` | runtime ownership replay and verified "
            "release, or compatibility-synthetic fail-closed relation-state "
            "response |",
            result.stdout,
        )

    def test_compatibility_id_rejects_lockstep_roster_rename(self) -> None:
        def rename_contract_and_roster(inventory: dict[str, object]) -> None:
            contract = inventory["compatibility_contracts"][0]
            old_id = contract["contract_id"]
            new_id = f"{old_id}-renamed"
            contract["contract_id"] = new_id
            roster = inventory["promotion_roster"]["compatibility_contracts"]
            roster[roster.index(old_id)] = new_id

        self._replace_inventory(rename_contract_and_roster)

        result = self._run_cli("--render")

        self.assert_invalid(result, "compatibility contract ID must equal")

    def test_compatibility_gate_expansion_retains_npu_conflict_proof(self) -> None:
        def remove_npu_conflict_gate(inventory: dict[str, object]) -> None:
            compatibility_members = inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"][
                "members"
            ]
            compatibility_members.remove("H-NPU-01")

        self._replace_inventory(remove_npu_conflict_gate)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted compatibility gate expansion")

    def test_compatibility_uses_relation_specific_concurrency_gates(self) -> None:
        registry = self.inventory["gate_registry"]
        members = self.inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"]["members"]

        self.assertIn("H-NPU-CON-01", members)
        self.assertIn("H-NPU-CON-02", members)
        self.assertNotIn("H-CON-01", members)
        self.assertNotIn("H-CON-02", members)
        self.assertEqual(registry["H-NPU-CON-01"]["suites"], ["PT-CON"])
        self.assertEqual(registry["H-NPU-CON-02"]["suites"], ["PT-CON"])
        profile = (
            self.repo / "docs/research/hatchery-residency-validation-profile.md"
        ).read_text(encoding="utf-8")
        campaign = self.campaign_path.read_text(encoding="utf-8")
        runtime_concurrency_row = next(
            line for line in profile.splitlines() if line.startswith("| H-CON-01 ")
        )
        self.assertIn("| exact_runtime, exact_synthetic |", runtime_concurrency_row)
        self.assertNotIn("compatibility_synthetic", runtime_concurrency_row)
        for gate_id in ("H-NPU-CON-01", "H-NPU-CON-02"):
            compatibility_row = next(
                line
                for line in campaign.splitlines()
                if line.startswith(f"| {gate_id} ")
            )
            self.assertIn("| compatibility_synthetic |", compatibility_row)

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("`H-NPU-CON-01`", result.stdout)
        self.assertIn("`H-NPU-CON-02`", result.stdout)

    def test_compatibility_gate_expansion_rejects_runtime_recovery_atom(self) -> None:
        def import_runtime_recovery(inventory: dict[str, object]) -> None:
            members = inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"]["members"]
            members[members.index("H-NPU-REC-01")] = "H-REC-01"

        self._replace_inventory(import_runtime_recovery)

        result = self._run_cli("--render")

        self.assert_invalid(result, "ineligible for compatibility_synthetic")

    def test_compatibility_roles_reject_lockstep_inversion(self) -> None:
        def invert_roles(inventory: dict[str, object]) -> None:
            variants = {variant["id"]: variant for variant in inventory["variants"]}
            variants["flm-npu"]["constraints"] = "npu_exclusive"
            variants["whispercpp-npu"]["constraints"] = "npu_flm"
            variants["ryzenai-llm-npu"]["constraints"] = "npu_flm"
            for case in inventory["compatibility_contracts"][0]["platform_cases"]:
                case["coexist_by_type_variant"], case["exclusive_variant"] = (
                    case["exclusive_variant"],
                    case["coexist_by_type_variant"],
                )

        self._replace_inventory(invert_roles)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted NPU role")

    def test_compatibility_contract_proves_only_cross_family_relation(self) -> None:
        contract = self.inventory["compatibility_contracts"][0]
        self.assertEqual(contract["relation_constraints"], ["npu_cross_family"])
        self.assertNotIn("constraints", contract)

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Relation constraints", result.stdout)

    def test_compatibility_relation_scope_rejects_participant_local_claim(self) -> None:
        def widen_relation_scope(inventory: dict[str, object]) -> None:
            inventory["compatibility_contracts"][0]["relation_constraints"].append(
                "ownership"
            )

        self._replace_inventory(widen_relation_scope)

        result = self._run_cli("--render")

        self.assert_invalid(result, "relation constraints must equal")


if __name__ == "__main__":
    unittest.main()
