"""Promotion-gate tests for the residency inventory validator."""

from __future__ import annotations

import hashlib
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise promotion-gate behavior through the CLI."""

    def test_compatibility_recovery_uses_a_relation_only_gate(self) -> None:
        registry = self.inventory["gate_registry"]
        compatibility_members = self.inventory["gate_sets"][
            "xdna2_npu_flm_conflict_v1"
        ]["members"]

        self.assertEqual(registry["H-NPU-REC-01"]["suites"], ["PT-REC"])
        self.assertIn("H-NPU-REC-01", compatibility_members)
        self.assertNotIn("H-REC-01", compatibility_members)

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("`H-NPU-REC-01`", result.stdout)

    def test_gate_source_drift_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        profile.write_text(
            profile.read_text(encoding="utf-8").replace("H-FP-01", "H-FP-99", 1),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "section_sha256 does not match")

    def test_gate_range_shorthand_fails_closed(self) -> None:
        def use_range(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["evidence_common_v1"]["members"][1] = "H-LIV-01a..g"

        self._replace_inventory(use_range)

        result = self._run_cli("--render")

        self.assert_invalid(result, "may not contain '..' range shorthand")

    def test_gate_source_range_shorthand_fails_closed(self) -> None:
        self.campaign_path.write_text(
            self.campaign_path.read_text(encoding="utf-8").replace(
                "H-LIV-01a", "H-LIV-01a..g", 1
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "may not contain '..' range shorthand")

    def test_gate_set_cycle_fails_closed(self) -> None:
        def create_cycle(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["evidence_common_v1"]["extends"] = [
                "hatchery_common_v1"
            ]

        self._replace_inventory(create_cycle)

        result = self._run_cli("--render")

        self.assert_invalid(result, "gate set inheritance cycle")

    def test_runtime_cell_must_include_hatchery_common_gates(self) -> None:
        def omit_common(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["hatchery_rocm_adm_v1"]["extends"] = []

        self._replace_inventory(omit_common)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted runtime expansion")

    def test_common_gate_sets_reject_lockstep_topology_swaps(self) -> None:
        def swap_topology_gates(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["hatchery_common_v1"]["members"] = ["H-NPU-TOP-01"]
            compatibility_members = inventory["gate_sets"]["xdna2_npu_flm_conflict_v1"][
                "members"
            ]
            compatibility_members.remove("H-NPU-TOP-01")
            compatibility_members.append("H-TOP-01")

        self._replace_inventory(swap_topology_gates)

        result = self._run_cli("--render")

        self.assert_invalid(
            result, "hatchery_common_v1 must equal evidence_common_v1 plus H-TOP-01"
        )

    def test_runtime_gate_sets_reject_lockstep_atomic_moves(self) -> None:
        def swap_admission_and_pressure_atoms(inventory: dict[str, object]) -> None:
            admission = inventory["gate_sets"]["hatchery_rocm_adm_v1"]["members"]
            pressure = inventory["gate_sets"]["hatchery_rocm_pre_v1"]["members"]
            admission.remove("H-ADM-02")
            admission.append("H-PRE-02")
            pressure.remove("H-PRE-02")
            pressure.append("H-ADM-02")

        self._replace_inventory(swap_admission_and_pressure_atoms)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted runtime expansion")

    def test_evidence_common_must_retain_every_liveness_atom(self) -> None:
        def move_liveness_atom_to_one_child(inventory: dict[str, object]) -> None:
            inventory["gate_sets"]["evidence_common_v1"]["members"].remove("H-LIV-01a")
            inventory["gate_sets"]["hatchery_rocm_adm_v1"]["members"].append(
                "H-LIV-01a"
            )

        self._replace_inventory(move_liveness_atom_to_one_child)

        result = self._run_cli("--render")

        self.assert_invalid(
            result, "evidence_common_v1 must equal the accepted evidence-common gates"
        )

    def test_promotion_gates_must_cover_every_applicable_suite(self) -> None:
        def remove_npu_suite_coverage(inventory: dict[str, object]) -> None:
            for gate_id in ("H-NPU-01", "H-PROT-01", "H-PROT-02"):
                inventory["gate_registry"][gate_id]["suites"] = ["PT-ID"]

        self._replace_inventory(remove_npu_suite_coverage)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted atomic gate assignments")

    def test_wildcard_suites_apply_to_every_promotion_operation(self) -> None:
        def remove_liveness_suite_coverage(inventory: dict[str, object]) -> None:
            for suffix in "abcdefg":
                inventory["gate_registry"][f"H-LIV-01{suffix}"]["suites"] = ["PT-ID"]

        self._replace_inventory(remove_liveness_suite_coverage)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted atomic gate assignments")

    def test_suite_applicability_rejects_npu_operation_drift(self) -> None:
        def drop_npc_from_npu_suite(inventory: dict[str, object]) -> None:
            inventory["suite_registry"]["PT-NPU"]["operations"] = ["ADM"]

        self._replace_inventory(drop_npc_from_npu_suite)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted suite applicability")

    def test_suite_sets_reject_lockstep_proof_moves(self) -> None:
        def swap_npu_and_pressure_suites(inventory: dict[str, object]) -> None:
            gpu_suites = inventory["suite_sets"]["gpu_standard"]
            npu_suites = inventory["suite_sets"]["npu_compatibility"]
            gpu_suites.remove("PT-PRE")
            gpu_suites.append("PT-NPU")
            npu_suites.remove("PT-NPU")
            npu_suites.append("PT-PRE")

        self._replace_inventory(swap_npu_and_pressure_suites)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted GPU and NPU proof memberships")

    def test_gate_registry_rejects_lockstep_suite_reassignment(self) -> None:
        def swap_explanation_and_liveness_suites(inventory: dict[str, object]) -> None:
            registry = inventory["gate_registry"]
            registry["H-EXP-01"]["suites"], registry["H-LIV-01a"]["suites"] = (
                registry["H-LIV-01a"]["suites"],
                registry["H-EXP-01"]["suites"],
            )

        self._replace_inventory(swap_explanation_and_liveness_suites)

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted atomic gate assignments")

    def test_gate_source_locator_rejects_generated_projection_self_source(self) -> None:
        def point_overlay_at_generated_projection(inventory: dict[str, object]) -> None:
            source = inventory["gate_sources"]["hatchery_overlay"]
            source["document"] = "docs/research/portable-residency-capability-matrix.md"
            source["section"] = "Atomic campaign gate registry"

        self._replace_inventory(point_overlay_at_generated_projection)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must equal its accepted document and section")

    def test_gate_section_parser_ignores_fenced_headings(self) -> None:
        campaign = self.campaign_path.read_text(encoding="utf-8")
        self.campaign_path.write_text(
            "```markdown\n"
            "### Atomic overlay gate definitions\n"
            "H-EXT-01 H-NPU-TOP-01 H-LIV-01a H-LIV-01b\n"
            "```\n\n"
            f"{campaign}",
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_gate_section_parser_matches_the_raw_heading_title(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "## Accepted Hatchery validation matrix\n",
                "## Accepted Hatchery validation matrix!\n",
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "must contain exactly one section named "
            "'Accepted Hatchery validation matrix'",
        )

    @staticmethod
    def _section_sha256(document: str, heading: str) -> str:
        marker = f"\n## {heading}\n"
        if marker not in document:
            marker = f"\n### {heading}\n"
        start = document.index(marker) + len(marker) - 1
        level = marker.split(heading, maxsplit=1)[0].count("#")
        end = len(document)
        for candidate_level in range(1, level + 1):
            candidate = document.find(f"\n{'#' * candidate_level} ", start + 1)
            if candidate >= 0:
                end = min(end, candidate + 1)
        return hashlib.sha256(document[start:end].encode()).hexdigest()

    def test_gate_source_semantic_rewrite_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        changed = profile.read_text(encoding="utf-8").replace(
            "Load without reclamation; reservation precedes spawn",
            "Evict every resident before loading",
            1,
        )
        profile.write_text(changed, encoding="utf-8")

        result = self._run_cli("--render")

        self.assert_invalid(result, "section_sha256 does not match")

    def test_gate_source_duplicate_row_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        source = profile.read_text(encoding="utf-8")
        row = next(
            line for line in source.splitlines() if line.startswith("| H-ADM-01 ")
        )
        changed = source.replace(row, f"{row}\n{row}", 1)
        profile.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_base"]["section_sha256"] = (
            self._section_sha256(changed, "Accepted Hatchery validation matrix")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "repeats atomic gate ID H-ADM-01")

    def test_gate_source_duplicate_semantics_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        source = profile.read_text(encoding="utf-8")
        row = next(
            line for line in source.splitlines() if line.startswith("| H-ADM-01 ")
        )
        duplicate = row.replace("H-ADM-01", "H-ADM-99", 1)
        changed = source.replace(row, f"{row}\n{duplicate}", 1)
        profile.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_base"]["section_sha256"] = (
            self._section_sha256(changed, "Accepted Hatchery validation matrix")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "duplicate gate definition semantics")

    def test_gate_sources_reject_cross_source_duplicate_semantics(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        profile_row = next(
            line
            for line in profile.read_text(encoding="utf-8").splitlines()
            if line.startswith("| H-TOP-01 ")
        )
        _, _, fixture, _, trigger, required_result, _ = profile_row.split("|")
        campaign = self.campaign_path.read_text(encoding="utf-8")
        overlay_row = next(
            line for line in campaign.splitlines() if line.startswith("| H-NPU-TOP-01 ")
        )
        duplicate = (
            f"| H-NPU-TOP-01 |{fixture}| compatibility_synthetic |"
            f"{trigger}|{required_result}|"
        )
        changed = campaign.replace(overlay_row, duplicate, 1)
        self.campaign_path.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_overlay"]["section_sha256"] = (
            self._section_sha256(changed, "Atomic overlay gate definitions")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "duplicate gate definition semantics")

    def test_gate_source_wrong_application_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        source = profile.read_text(encoding="utf-8")
        changed = source.replace(
            "| H-PROT-01 | L | exact_runtime |",
            "| H-PROT-01 | L | compatibility_synthetic |",
            1,
        )
        profile.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_base"]["section_sha256"] = (
            self._section_sha256(changed, "Accepted Hatchery validation matrix")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted applications")

    def test_compatibility_recovery_gate_rejects_runtime_authority(self) -> None:
        source = self.campaign_path.read_text(encoding="utf-8")
        changed = source.replace(
            "Return the registered fail-closed compatibility response and preserve "
            "every participant; do not clean up a runtime, replay claims, recover a "
            "participant, or assert live recovery authority.",
            "Replay participant claims, clean up owned runtimes, and recover each "
            "participant before returning success.",
            1,
        )
        self.assertNotEqual(source, changed)
        self.campaign_path.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_overlay"]["section_sha256"] = (
            self._section_sha256(changed, "Atomic overlay gate definitions")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted relation-only recovery definition")

    def test_npu_conflict_gate_rejects_unpinned_idle_displacement_authority(
        self,
    ) -> None:
        profile_path = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        source = profile_path.read_text(encoding="utf-8")
        changed = source.replace(
            "Classify the `npu_cross_family` conflict, preserve every incumbent, "
            "and refuse the incoming load before eviction or backend spawn; "
            "unpinned-idle state grants no displacement authority.",
            "Classify the `npu_cross_family` conflict and displace an unpinned-idle "
            "incumbent before loading the incoming participant.",
            1,
        )
        self.assertNotEqual(source, changed)
        profile_path.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_base"]["section_sha256"] = (
            self._section_sha256(changed, "Accepted Hatchery validation matrix")
        )
        base_digest = hashlib.sha256(changed.encode()).hexdigest()
        self.inventory["campaign_base_binding"]["sha256"] = base_digest
        campaign = self.campaign_path.read_text(encoding="utf-8")
        campaign = campaign.replace(
            f"at SHA-256 `{hashlib.sha256(source.encode()).hexdigest()}`",
            f"at SHA-256 `{base_digest}`",
            1,
        )
        self.campaign_path.write_text(campaign, encoding="utf-8")
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted compatibility safety definition")

    def test_npu_stale_relation_gate_rejects_spawn_or_eviction_authority(self) -> None:
        source = self.campaign_path.read_text(encoding="utf-8")
        changed = source.replace(
            "Reject the stale relation token, recompute from current relation "
            "evidence, and fail closed by preserving every incumbent and refusing "
            "the incoming load with zero eviction and zero backend spawn.",
            "Commit the stale relation decision and spawn the incoming backend "
            "after evicting the incumbent.",
            1,
        )
        self.assertNotEqual(source, changed)
        self.campaign_path.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_overlay"]["section_sha256"] = (
            self._section_sha256(changed, "Atomic overlay gate definitions")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "accepted compatibility safety definition")

    def test_gate_source_unknown_application_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        source = profile.read_text(encoding="utf-8")
        changed = source.replace(
            "| H-PROT-01 | L | exact_runtime |",
            "| H-PROT-01 | L | runtimeish |",
            1,
        )
        profile.write_text(changed, encoding="utf-8")
        self.inventory["gate_sources"]["hatchery_base"]["section_sha256"] = (
            self._section_sha256(changed, "Accepted Hatchery validation matrix")
        )
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "unknown applications")

    def test_gate_source_stale_section_digest_fails_closed(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "Cold baseline and each actual model class",
                "Changed cold baseline",
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "section_sha256 does not match")

    def test_campaign_base_binding_rejects_stale_citation(self) -> None:
        self.campaign_path.write_text(
            self.campaign_path.read_text(encoding="utf-8").replace(
                "at SHA-256 `",
                "at SHA-256 `deadbeef",
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "campaign base profile citation")

    def test_campaign_base_binding_rejects_stale_full_document_digest(self) -> None:
        profile = (
            self.repo / "docs" / "research" / "hatchery-residency-validation-profile.md"
        )
        profile.write_text(
            profile.read_text(encoding="utf-8").replace(
                "# Hatchery residency validation profile",
                "# Changed Hatchery residency validation profile",
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "does not match the complete base profile")

    def test_render_includes_gate_source_bindings_and_applications(self) -> None:
        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("### Gate source bindings", result.stdout)
        self.assertIn("| Gate ID | Source | Applications | Suites |", result.stdout)
        self.assertIn("`compatibility_synthetic`", result.stdout)
        for source_id, binding in self.inventory["gate_sources"].items():
            self.assertIn(
                f"| `{source_id}` | `{binding['document']}` | "
                f"{binding['section']} | `{binding['section_sha256']}` |",
                result.stdout,
            )
        base_binding = self.inventory["campaign_base_binding"]
        self.assertIn(
            f"| Campaign base profile | `{base_binding['document']}` | "
            f"`{base_binding['sha256']}` |",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
