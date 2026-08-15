"""Closed residency-policy tests for the residency inventory validator."""

from __future__ import annotations

import hashlib
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise closed residency-policy behavior through the CLI."""

    def test_exact_cell_fallback_values_require_strings_before_deduplication(
        self,
    ) -> None:
        self.inventory["exact_cells"][0]["fallbacks"][
            "insufficient_capacity_authority"
        ] = ["not-a-fallback-id"]
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "exact cell H-ROCM-ADM-GTT-HOST-v1.fallbacks."
            "insufficient_capacity_authority must be a nonempty string",
        )

    def test_fallback_operations_must_equal_all_actual_references(self) -> None:
        def widen_npu_fallback(inventory: dict[str, object]) -> None:
            inventory["fallback_registry"]["residency_npu_conflict_preserve_refuse_v1"][
                "operations"
            ] = ["ADM", "NPC"]

        self._replace_inventory(widen_npu_fallback)

        result = self._run_cli("--render")

        self.assert_invalid(
            result, "must exactly equal its referenced operation templates"
        )

    def test_suite_proof_semantics_are_closed(self) -> None:
        def rewrite_npu_proof(inventory: dict[str, object]) -> None:
            inventory["suite_registry"]["PT-NPU"][
                "proof"
            ] = "byte capacity and destructive recovery"

        self._replace_inventory(rewrite_npu_proof)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted proof semantics")

    def test_registry_operations_use_the_exact_array_field(self) -> None:
        def collapse_suite_operations(inventory: dict[str, object]) -> None:
            inventory["suite_registry"]["PT-ADM"]["operations"] = "ADM"

        self._replace_inventory(collapse_suite_operations)

        result = self._run_cli("--render")

        self.assert_invalid(result, "suite_registry.PT-ADM.operations must be an array")

    def test_coverage_policy_rejects_arbitrary_values(self) -> None:
        def rewrite_host_floor_rule(inventory: dict[str, object]) -> None:
            inventory["coverage_policy"]["host_floor_rule"] = "anything goes"

        self._replace_inventory(rewrite_host_floor_rule)

        result = self._run_cli("--render")

        self.assert_invalid(result, "coverage_policy must equal the accepted policy")

    def test_predictor_profile_rejects_unaccepted_confidence_target(self) -> None:
        def replace_confidence_target(inventory: dict[str, object]) -> None:
            inventory["predictor_rules"]["hatchery-llamacpp-rocm-profile-free-v1"][
                "confidence_target"
            ] = "garbage-confidence"

        self._replace_inventory(replace_confidence_target)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "predictor rule hatchery-llamacpp-rocm-profile-free-v1 must equal its "
            "accepted semantic identity",
        )

    def test_profile_document_binding_rejects_missing_heading(self) -> None:
        def point_configuration_at_missing_heading(
            inventory: dict[str, object],
        ) -> None:
            inventory["configuration_profiles"][
                "profile-free-residency-estimation-v1-text-only"
            ]["document"] = {
                "locator": (
                    "docs/research/profile-free-residency-estimation.md"
                    "#missing-heading"
                ),
                "sha256": "0" * 64,
            }

        self._replace_inventory(point_configuration_at_missing_heading)

        result = self._run_cli("--render")

        self.assert_invalid(result, "heading #missing-heading does not exist")

    def test_profile_document_binding_rejects_path_traversal(self) -> None:
        def escape_workload_document(inventory: dict[str, object]) -> None:
            inventory["workload_profiles"]["hatchery-text-generation-campaign-v1"][
                "document"
            ] = {
                "locator": "../outside.md#workload",
                "sha256": "0" * 64,
            }

        self._replace_inventory(escape_workload_document)

        result = self._run_cli("--render")

        self.assert_invalid(result, "safe repository-relative path")

    def test_profile_document_binding_rejects_stale_digest(self) -> None:
        def stale_observation_document(inventory: dict[str, object]) -> None:
            inventory["observation_contracts"]["hatchery-gtt-host-observation-v1"][
                "document"
            ]["sha256"] = ("0" * 64)

        self._replace_inventory(stale_observation_document)

        result = self._run_cli("--render")

        self.assert_invalid(result, "does not match its bound document content")

    def test_profile_ids_reject_swapped_configuration_and_workload_bindings(
        self,
    ) -> None:
        def swap_document_bindings(inventory: dict[str, object]) -> None:
            configuration = inventory["configuration_profiles"][
                "profile-free-residency-estimation-v1-text-only"
            ]
            workload = inventory["workload_profiles"][
                "hatchery-text-generation-campaign-v1"
            ]
            configuration["document"], workload["document"] = (
                workload["document"],
                configuration["document"],
            )

        self._replace_inventory(swap_document_bindings)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "configuration profile profile-free-residency-estimation-v1-text-only "
            "must equal its accepted semantic identity",
        )

    def test_profile_document_binding_digest_covers_its_heading(self) -> None:
        document = (
            self.repo / "docs" / "research" / "profile-free-residency-estimation.md"
        )
        document.write_text(
            document.read_text(encoding="utf-8").replace(
                "### V1 configuration predicate\n",
                "### V1 configuration predicate!\n",
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "does not match its bound document content")

    def test_profile_id_rejects_lockstep_document_and_digest_rebinding(self) -> None:
        document = (
            self.repo / "docs" / "research" / "profile-free-residency-estimation.md"
        )
        changed = document.read_text(encoding="utf-8").replace(
            "### V1 configuration predicate\n",
            "### V1 configuration predicate!\n",
            1,
        )
        document.write_text(changed, encoding="utf-8")
        start = changed.index("### V1 configuration predicate!\n")
        end = changed.find("\n### ", start + 1)
        self.assertNotEqual(end, -1)
        section = changed[start : end + 1]
        self.inventory["configuration_profiles"][
            "profile-free-residency-estimation-v1-text-only"
        ]["document"]["sha256"] = hashlib.sha256(section.encode()).hexdigest()
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "configuration profile profile-free-residency-estimation-v1-text-only "
            "must equal its accepted semantic identity",
        )

    def test_document_bindings_canonicalize_crlf_checkouts(self) -> None:
        research = self.repo / "docs" / "research"
        for name in (
            "hatchery-campaign-parameters.md",
            "hatchery-residency-validation-profile.md",
            "profile-free-residency-estimation.md",
        ):
            path = research / name
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_exact_fallback_guards_reject_lockstep_id_swaps(self) -> None:
        def swap_pressure_fallbacks(inventory: dict[str, object]) -> None:
            pressure_cell = next(
                cell
                for cell in inventory["exact_cells"]
                if cell["operation_template"] == "PRE"
            )
            fallbacks = pressure_cell["fallbacks"]
            (
                fallbacks["valid_reporting_without_action_authority"],
                fallbacks["invalid_reporting_evidence"],
            ) = (
                fallbacks["invalid_reporting_evidence"],
                fallbacks["valid_reporting_without_action_authority"],
            )

        self._replace_inventory(swap_pressure_fallbacks)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must use its accepted fallback")

    def test_promotion_roster_must_equal_actual_units(self) -> None:
        def drop_roster_cell(inventory: dict[str, object]) -> None:
            inventory["promotion_roster"]["exact_cells"].pop()

        self._replace_inventory(drop_roster_cell)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must exactly list the exact cell IDs")

    def test_later_promotion_roster_projects_exactly_34_units(self) -> None:
        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("### Later promotion roster", result.stdout)
        self.assertEqual(result.stdout.count("| `H-VULKAN-ADM-GTT-HOST-v1`<br>"), 2)
        self.assertEqual(result.stdout.count("| `H-VULKAN-PRE-GTT-HOST-v1`<br>"), 2)
        self.assertEqual(result.stdout.count("| `H-VULKAN-STA-GTT-HOST-v1`<br>"), 2)
        self.assertEqual(result.stdout.count("| `H-VULKAN-REC-GTT-HOST-OWN-v1`<br>"), 2)
        self.assertEqual(result.stdout.count("| `W-XDNA2-FLM-NPU-LLM-ADM-v1`<br>"), 2)
        self.assertEqual(
            result.stdout.count("| `W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-PIN-v1`<br>"),
            2,
        )
        self.assertEqual(
            result.stdout.count("| `W-XDNA2-RYZENAI-LLM-NPU-LLM-REC-v1`<br>"),
            2,
        )

    def test_later_promotion_roster_rejects_missing_units(self) -> None:
        self.inventory["later_promotion_roster"].pop()
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "must contain exactly the accepted 34 units")

    def test_later_promotion_roster_accepts_a_recorded_issue_id(self) -> None:
        self.inventory["later_promotion_roster"][0]["issue_id"] = 12345
        self._write_inventory()

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout.count("| `H-VULKAN-ADM-GTT-HOST-v1`<br>`12345` |"),
            2,
        )

    def test_windows_physical_units_cannot_use_synthetic_relation_gates(self) -> None:
        windows_unit = next(
            unit
            for unit in self.inventory["later_promotion_roster"]
            if unit["unit_id"] == "W-XDNA2-FLM-NPU-LLM-ADM-v1"
        )
        windows_unit["evidence_gate_set"] = "xdna2_npu_flm_conflict_v1"
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted identity")

    def test_later_units_cannot_raise_their_capability_ceiling(self) -> None:
        self.inventory["later_promotion_roster"][0]["evidence_ceiling"] = "validated"
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted identity")

    def test_exact_cell_id_rejects_lockstep_roster_rename(self) -> None:
        def rename_cell_and_roster(inventory: dict[str, object]) -> None:
            cell = inventory["exact_cells"][0]
            old_id = cell["cell_id"]
            new_id = f"{old_id}-renamed"
            cell["cell_id"] = new_id
            roster = inventory["promotion_roster"]["exact_cells"]
            roster[roster.index(old_id)] = new_id

        self._replace_inventory(rename_cell_and_roster)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted identity selector")

    def test_exact_cell_ids_reject_admission_startup_role_swap(self) -> None:
        def swap_cell_and_roster_identities(inventory: dict[str, object]) -> None:
            cells = {
                cell["operation_template"]: cell for cell in inventory["exact_cells"]
            }
            cells["ADM"]["cell_id"], cells["STA"]["cell_id"] = (
                cells["STA"]["cell_id"],
                cells["ADM"]["cell_id"],
            )
            roster = inventory["promotion_roster"]["exact_cells"]
            admission_index = roster.index("H-ROCM-ADM-GTT-HOST-v1")
            startup_index = roster.index("H-ROCM-STA-GTT-HOST-v1")
            roster[admission_index], roster[startup_index] = (
                roster[startup_index],
                roster[admission_index],
            )

        self._replace_inventory(swap_cell_and_roster_identities)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted identity selector")

    def test_npu_constraint_profiles_reject_lockstep_contract_weakening(self) -> None:
        def remove_ownership_everywhere(inventory: dict[str, object]) -> None:
            inventory["constraint_profiles"]["npu_flm"].remove("ownership")
            inventory["constraint_profiles"]["npu_exclusive"].remove("ownership")

        self._replace_inventory(remove_ownership_everywhere)

        result = self._run_cli("--render")

        self.assert_invalid(result, "constraint_profiles must equal")

    def test_npu_fallback_rejects_destructive_semantic_rewrite(self) -> None:
        def rewrite_npu_fallback(inventory: dict[str, object]) -> None:
            fallback = inventory["fallback_registry"][
                "residency_npu_conflict_preserve_refuse_v1"
            ]
            fallback["guard"] = "capacity is sufficient"
            fallback["effect"] = "evict every incumbent and admit incoming"

        self._replace_inventory(rewrite_npu_fallback)

        result = self._run_cli("--render")

        self.assert_invalid(result, "guard/effect values must equal")

    def test_admission_fallback_rejects_destructive_semantic_rewrite(self) -> None:
        def rewrite_admission_fallback(inventory: dict[str, object]) -> None:
            inventory["fallback_registry"][
                "residency_admission_refuse_unknown_demand_v1"
            ]["effect"] = "evict all residents and retry"

        self._replace_inventory(rewrite_admission_fallback)

        result = self._run_cli("--render")

        self.assert_invalid(result, "guard/effect values must equal")

    def test_npu_variant_must_use_npu_operation_suite_and_fallback_sets(self) -> None:
        def assign_gpu_sets_to_flm(inventory: dict[str, object]) -> None:
            flm = next(
                variant
                for variant in inventory["variants"]
                if variant["id"] == "flm-npu"
            )
            flm["operations"] = "gpu_resource"
            flm["suites"] = "gpu_standard"
            flm["fallbacks"] = "gpu_standard"

        self._replace_inventory(assign_gpu_sets_to_flm)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must match its constraint profile")

    def test_variant_constraint_profile_must_match_platform_topology(self) -> None:
        def swap_gpu_constraint_profiles(inventory: dict[str, object]) -> None:
            variants = {variant["id"]: variant for variant in inventory["variants"]}
            variants["sd-cpp-rocm"]["constraints"] = "apple_unified"
            variants["llamacpp-metal"]["constraints"] = "provider_resolved_gpu"

        self._replace_inventory(swap_gpu_constraint_profiles)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must match its platform topology")

    def test_variant_recovery_profile_must_match_recipe_ownership(self) -> None:
        def replace_vllm_recovery(inventory: dict[str, object]) -> None:
            vllm = next(
                variant
                for variant in inventory["variants"]
                if variant["id"] == "vllm-rocm"
            )
            vllm["recovery"] = "native_subprocess_tree"

        self._replace_inventory(replace_vllm_recovery)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must match its recipe ownership model")

    def test_recovery_profiles_require_typed_contract_fields(self) -> None:
        def replace_native_with_incomplete_contract(
            inventory: dict[str, object],
        ) -> None:
            inventory["recovery_profiles"]["native_subprocess_tree"] = {
                "launch": "prepared",
                "containment": "complete_descendant_process_tree",
                "ownership": "lemonade_spawned_process_tree",
            }

        self._replace_inventory(replace_native_with_incomplete_contract)

        result = self._run_cli("--render")

        self.assert_invalid(result, "recovery profile native_subprocess_tree")

    def test_recovery_profiles_reject_closed_semantic_drift(self) -> None:
        def weaken_native_containment(inventory: dict[str, object]) -> None:
            inventory["recovery_profiles"]["native_subprocess_tree"] = {
                "launch": "prepared",
                "containment": "direct_child_only",
                "ownership": "lemonade_spawned_process_tree",
                "verified_release": ["process", "resource", "constraint"],
            }

        self._replace_inventory(weaken_native_containment)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted closed recovery semantics")

    def test_flm_recovery_requires_selected_membership_and_device_claim(
        self,
    ) -> None:
        def use_selected_membership_release(inventory: dict[str, object]) -> None:
            inventory["recovery_profiles"]["flm_system_managed"]["verified_release"] = [
                "serving_process_or_service_membership",
                "device_claim",
            ]

        self._replace_inventory(use_selected_membership_release)

        accepted = self._run_cli("--render")

        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        def use_service_only_release(inventory: dict[str, object]) -> None:
            inventory["recovery_profiles"]["flm_system_managed"]["verified_release"] = [
                "service",
                "device_claim",
            ]

        self._replace_inventory(use_service_only_release)

        rejected = self._run_cli("--render")

        self.assert_invalid(rejected, "accepted closed recovery semantics")

    def test_broad_variant_evidence_ceiling_must_remain_modeled(self) -> None:
        def promote_flm_variant(inventory: dict[str, object]) -> None:
            flm = next(
                variant
                for variant in inventory["variants"]
                if variant["id"] == "flm-npu"
            )
            flm["evidence_ceiling"] = "validated"

        self._replace_inventory(promote_flm_variant)

        result = self._run_cli("--render")

        self.assert_invalid(result, "evidence ceiling must remain modeled")

    def test_gpu_constraint_profiles_reject_lockstep_weakening(self) -> None:
        def weaken_provider_resolved_profile(inventory: dict[str, object]) -> None:
            inventory["constraint_profiles"]["provider_resolved_gpu"].remove(
                "ownership"
            )

        self._replace_inventory(weaken_provider_resolved_profile)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted closed profiles")

    def test_fallback_sets_reject_cross_scope_membership(self) -> None:
        def assign_hatchery_fallback_globally(inventory: dict[str, object]) -> None:
            inventory["fallback_sets"]["gpu_standard"]["ADM"] = [
                "hatchery_rocm_admission_refuse_unknown_capacity_v1"
            ]

        self._replace_inventory(assign_hatchery_fallback_globally)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted fail-safe memberships")

    def test_runtime_binding_vocabulary_rejects_lockstep_weakening(self) -> None:
        def remove_material_binding(inventory: dict[str, object]) -> None:
            binding = "backend_artifact_digest"
            inventory["runtime_binding_kinds"].remove(binding)
            for cell in inventory["exact_cells"]:
                cell["runtime_bindings"].remove(binding)

        self._replace_inventory(remove_material_binding)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted material identity set")

    def test_hardware_profile_requires_its_exact_runtime_binding_kinds(self) -> None:
        def drop_driver_runtime_closure(inventory: dict[str, object]) -> None:
            inventory["hardware_profiles"]["hatchery-gfx1151-shared-gtt-v1"][
                "required_runtime_bindings"
            ].remove("driver_runtime_closure")

        self._replace_inventory(drop_driver_runtime_closure)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "hardware profile hatchery-gfx1151-shared-gtt-v1 must equal its "
            "accepted semantic identity",
        )

    def test_model_type_enum_rejects_unknown_members(self) -> None:
        def add_unknown_model_type(inventory: dict[str, object]) -> None:
            inventory["enums"]["model_type"].append("bogus_type")

        self._replace_inventory(add_unknown_model_type)

        result = self._run_cli("--render")

        self.assert_invalid(result, "model_type enum is not the accepted closed set")

    def test_constraint_kind_enum_rejects_unknown_members(self) -> None:
        def add_unknown_constraint(inventory: dict[str, object]) -> None:
            inventory["enums"]["constraint_kind"].append("bogus_constraint")

        self._replace_inventory(add_unknown_constraint)

        result = self._run_cli("--render")

        self.assert_invalid(
            result, "constraint_kind enum is not the accepted closed set"
        )

    def test_retained_contracts_reject_unknown_members(self) -> None:
        def add_unknown_retained_contract(inventory: dict[str, object]) -> None:
            inventory["exclusions"][0]["retained_contracts"].append("bogus_contract")

        self._replace_inventory(add_unknown_retained_contract)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted closed set including verified_release")

    def test_retained_contracts_require_verified_release(self) -> None:
        def remove_verified_release(inventory: dict[str, object]) -> None:
            inventory["exclusions"][0]["retained_contracts"].remove("verified_release")

        self._replace_inventory(remove_verified_release)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted closed set including verified_release")

    def test_local_exclusion_must_declare_retained_contracts(self) -> None:
        def delete_retained_contracts(inventory: dict[str, object]) -> None:
            del inventory["exclusions"][0]["retained_contracts"]

        self._replace_inventory(delete_retained_contracts)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must declare the accepted retained contracts")

    def test_exact_cell_identity_rejects_lockstep_model_type_change(self) -> None:
        def replace_llm_with_embedding(inventory: dict[str, object]) -> None:
            configuration = inventory["configuration_profiles"][
                "profile-free-residency-estimation-v1-text-only"
            ]
            configuration["model_types"] = ["embedding"]
            for cell in inventory["exact_cells"]:
                cell["match"]["model_types"] = ["embedding"]

        self._replace_inventory(replace_llm_with_embedding)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "configuration profile profile-free-residency-estimation-v1-text-only "
            "must equal its accepted semantic identity",
        )


if __name__ == "__main__":
    unittest.main()
