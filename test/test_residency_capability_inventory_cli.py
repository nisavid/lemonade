"""Cli validation and rendering tests for the residency inventory validator."""

from __future__ import annotations

import unittest
from pathlib import Path

from residency_capability_inventory_cli_case import (
    CAMPAIGN_BEGIN,
    CAMPAIGN_END,
    MATRIX_BEGIN,
    MATRIX_END,
    ResidencyCapabilityInventoryCliCase,
)

from tools.residency_inventory.documents import (
    escape_table_text,
    render_code,
    render_inventory,
)
from tools.residency_inventory.schema import validate_inventory


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise CLI validation and rendering behavior through the CLI."""

    def test_normal_validation_accepts_frozen_rocm_arch_and_channel_gates(self) -> None:
        result = self._run_cli()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "portable residency capability inventory: valid\n"
        )

    def test_render_prints_both_projections_without_writing_documents(self) -> None:
        matrix_before = self.matrix_path.read_bytes()
        campaign_before = self.campaign_path.read_bytes()

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.count(MATRIX_BEGIN), 1)
        self.assertEqual(result.stdout.count(MATRIX_END), 1)
        self.assertEqual(result.stdout.count(CAMPAIGN_BEGIN), 1)
        self.assertEqual(result.stdout.count(CAMPAIGN_END), 1)
        self.assertIn("### Validation suite registry", result.stdout)
        self.assertIn("### Validation suite sets", result.stdout)
        self.assertIn("### Frozen source closure", result.stdout)
        self.assertEqual(
            result.stdout.count(
                f"Support baseline: `{self.inventory['source_support_baseline']}`"
            ),
            1,
        )
        self.assertIn(
            f"| Support baseline | `{self.inventory['source_support_baseline']}` |",
            result.stdout,
        )
        self.assertIn("### Material profile semantic identities", result.stdout)
        self.assertIn("### Material profile document bindings", result.stdout)
        self.assertIn(
            "| `predictor_rules` | `hatchery-llamacpp-rocm-profile-free-v1` | "
            "`confidence_target` = `validated_predictor` |",
            result.stdout,
        )
        self.assertIn(
            "`required_runtime_bindings` = "
            '`["device_identity","driver_runtime_closure"]`',
            result.stdout,
        )
        expected_binding_sources = (
            (
                "hardware_profiles.hatchery-gfx1151-shared-gtt-v1.evidence_document",
                "hardware_profiles",
                "hatchery-gfx1151-shared-gtt-v1",
                "evidence_document",
            ),
            (
                "configuration_profiles.profile-free-residency-estimation-v1-text-only.document",
                "configuration_profiles",
                "profile-free-residency-estimation-v1-text-only",
                "document",
            ),
            (
                "workload_profiles.hatchery-text-generation-campaign-v1.document",
                "workload_profiles",
                "hatchery-text-generation-campaign-v1",
                "document",
            ),
            (
                "predictor_rules.hatchery-llamacpp-rocm-profile-free-v1.document",
                "predictor_rules",
                "hatchery-llamacpp-rocm-profile-free-v1",
                "document",
            ),
            (
                "observation_contracts.hatchery-gtt-host-observation-v1.document",
                "observation_contracts",
                "hatchery-gtt-host-observation-v1",
                "document",
            ),
        )
        for binding_id, registry, profile_id, field in expected_binding_sources:
            binding = self.inventory[registry][profile_id][field]
            self.assertEqual(
                result.stdout.count(
                    f"| `{binding_id}` | `{binding['locator']}` | `{binding['sha256']}` |"
                ),
                1,
            )
        self.assertIn("### Coverage policy", result.stdout)
        for field, value in self.inventory["coverage_policy"].items():
            self.assertEqual(
                result.stdout.count(f"| `{field}` | `{value}` |"),
                1,
            )
        for suite_set_id, suite_ids in self.inventory["suite_sets"].items():
            rendered_suites = ", ".join(f"`{suite_id}`" for suite_id in suite_ids)
            self.assertIn(f"| `{suite_set_id}` | {rendered_suites} |", result.stdout)
        self.assertIn("### Atomic campaign gate registry", result.stdout)
        self.assertIn("### Flattened campaign gate sets", result.stdout)
        self.assertIn("### Compatibility promotion contracts", result.stdout)
        self.assertIn("### Promotion roster", result.stdout)
        self.assertEqual(result.stdout.count("`H-NPU-FLM-CONFLICT-XDNA2-v1`"), 3)
        self.assertEqual(self.matrix_path.read_bytes(), matrix_before)
        self.assertEqual(self.campaign_path.read_bytes(), campaign_before)

    def test_renderer_escapes_control_characters_in_document_bindings(self) -> None:
        projection = validate_inventory(self.repo, self.inventory)
        unsafe = "part\r\n|tail"
        escaped = "part \\|tail"
        projection["profile_bindings"] = {
            f"{unsafe}-binding": {
                "locator": f"{unsafe}-locator",
                "sha256": f"{unsafe}-sha",
            }
        }
        projection["campaign_base_binding"] = {
            "document": f"{unsafe}-base-document",
            "sha256": f"{unsafe}-base-sha",
        }
        projection["gate_sources"] = {
            f"{unsafe}-source": {
                "document": f"{unsafe}-source-document",
                "section": f"{unsafe}-section",
                "section_sha256": f"{unsafe}-source-sha",
            }
        }

        rendered = render_inventory(projection)

        self.assertEqual(escape_table_text("a\r\nb\rc\nd|e"), "a b c d\\|e")
        self.assertNotIn("\r", rendered)
        self.assertIn(
            f"| `{escaped}-binding` | `{escaped}-locator` | `{escaped}-sha` |",
            rendered,
        )
        self.assertIn(
            f"`{escaped}-base-document` | `{escaped}-base-sha` |",
            rendered,
        )
        self.assertIn(
            f"| `{escaped}-source` | `{escaped}-source-document` | "
            f"{escaped}-section | `{escaped}-source-sha` |",
            rendered,
        )

    def test_renderer_encodes_backticks_and_pipes_without_changing_table_shape(
        self,
    ) -> None:
        self.inventory["exact_cells"][0][
            "scope"
        ] = "accepted `embedded` | left \\| projection text"
        self._write_inventory()

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "accepted &#96;embedded&#96; \\| left &#92;\\| projection text",
            result.stdout,
        )
        self.assertEqual(render_code("part`inside"), "``part`inside``")
        self.assertEqual(render_code("`edge`"), "`` `edge` ``")
        self.assertEqual(render_code("part` | tail"), "``part` \\| tail``")
        for spaces in (" ", "  ", "   "):
            with self.subTest(spaces=len(spaces)):
                self.assertEqual(render_code(spaces), f"`{spaces}`")
        for mixed_whitespace in (" \t ", " \u00a0 "):
            with self.subTest(mixed_whitespace=repr(mixed_whitespace)):
                self.assertEqual(
                    render_code(mixed_whitespace), f"` {mixed_whitespace} `"
                )

    def test_renderer_rejects_nul_before_projection_output(self) -> None:
        self.inventory["exact_cells"][0]["scope"] = "accepted\0scope"
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "unsupported control character: U+0000")

    def test_renderer_handles_embedded_and_edge_backticks_in_bound_locators(
        self,
    ) -> None:
        projection = validate_inventory(self.repo, self.inventory)
        projection["profile_bindings"] = {
            "embedded`binding": {
                "locator": "`edge-bound-locator`",
                "sha256": "embedded`sha",
            }
        }

        rendered = render_inventory(projection)

        self.assertIn(
            "| ``embedded`binding`` | `` `edge-bound-locator` `` | "
            "``embedded`sha`` |",
            rendered,
        )

    def test_git_object_alternate_uses_an_absolute_existing_path(self) -> None:
        alternates = self.repo / ".git" / "objects" / "info" / "alternates"
        source_objects = Path(alternates.read_text(encoding="utf-8").strip())

        self.assertTrue(source_objects.is_absolute())
        self.assertTrue(source_objects.is_dir())

    def test_invalid_json_reports_a_bounded_cli_diagnostic(self) -> None:
        self.inventory_path.write_text("{\n", encoding="utf-8")

        result = self._run_cli()

        self.assert_invalid(result, "Expecting property name enclosed in double quotes")
        self.assertNotIn("Traceback", result.stderr)

    def test_long_domain_error_is_globally_bounded(self) -> None:
        inventory = dict(self.inventory)
        inventory["x" * 10_000] = "unexpected"
        self.inventory = inventory
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "... [truncated]")
        self.assertLessEqual(len(result.stderr), 2_100)
        self.assertNotIn("x" * 3_000, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_long_generic_error_is_globally_bounded(self) -> None:
        environment = self._sitecustomize_env("""
import os

_real_open = os.open

def _long_failure(path, *args, **kwargs):
    if str(path).endswith('portable-residency-capability-inventory.json'):
        raise OSError('y' * 10_000)
    return _real_open(path, *args, **kwargs)

os.open = _long_failure
""")

        result = self._run_cli("--render", env=environment)

        self.assert_invalid(result, "... [truncated]")
        self.assertLessEqual(len(result.stderr), 2_100)
        self.assertNotIn("y" * 3_000, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_invalid_arguments_report_the_domain_diagnostic(self) -> None:
        result = self._run_cli("--unknown-cycle3-option")

        self.assert_invalid(result, "unrecognized arguments: --unknown-cycle3-option")

    def test_normal_and_render_refuse_a_pending_document_transaction(self) -> None:
        journal_path = self.inventory_path.with_name(
            f".{self.inventory_path.name}.update-v1.json"
        )
        journal_path.write_text("{}\n", encoding="utf-8")

        for arguments in ((), ("--render",)):
            with self.subTest(arguments=arguments):
                result = self._run_cli(*arguments)
                self.assert_invalid(
                    result, "pending generated-document update transaction"
                )


if __name__ == "__main__":
    unittest.main()
