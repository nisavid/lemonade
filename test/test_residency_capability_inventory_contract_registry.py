"""Closed generated-contract registry tests through the inventory CLI."""

from __future__ import annotations

import copy

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase


class ResidencyCapabilityInventoryContractRegistryTest(
    ResidencyCapabilityInventoryCliCase
):
    """Exercise the schema-v7 contract registry through the public CLI."""

    def test_010_schema_v7_closes_the_generated_contract_registries(self) -> None:
        self.assertEqual(self.inventory["schema_version"], 7)
        registry = self.inventory["contract_registry"]
        self.assertEqual(registry["schema"], "residency.explanation/1.0")
        self.assertEqual(len(registry["operation_registry"]["families"]), 2)
        self.assertEqual(len(registry["operation_registry"]["aliases"]), 12)
        self.assertEqual(len(registry["reason_envelope_registry"]), 8)
        self.assertEqual(len(registry["request_context_registry"]), 37)
        self.assertEqual(len(registry["request_stage_registry"]), 7)
        self.assertEqual(len(registry["reason_registry"]), 87)
        self.assertEqual(len(registry["presentation_registry"]), 27)
        self.assertEqual(len(registry["detail_schema_registry"]), 15)
        self.assertEqual(len(registry["schema_registry"]), 16)
        local_overlay_schemas = registry["schema_registry"]
        self.assertEqual(
            local_overlay_schemas["profiling_input_envelope"]["version"],
            {"major": 2, "minor": 0},
        )
        self.assertEqual(
            local_overlay_schemas["profiling_input_envelope"]["schema_type"],
            "residency.profiling_input_envelope/2.0",
        )
        profiling_fields = local_overlay_schemas["profiling_input_envelope"]["fields"]
        for field in ("sequence", "max_clock_skew_milliseconds"):
            self.assertEqual(
                profiling_fields[field],
                {"required": True, "type": "local_overlay_positive_uint64"},
            )
        for schema_key in (
            "profiling_phase_attestation",
            "deployment_local_overlay_object",
            "overlay_activation_root",
        ):
            self.assertEqual(
                local_overlay_schemas[schema_key]["version"],
                {"major": 1, "minor": 0},
            )
        catalog_fields = registry["schema_registry"]["residency_profiles"]["fields"]
        self.assertEqual(
            catalog_fields["source_support_baseline"],
            {"type": "git_commit_sha1", "required": True},
        )
        self.assertEqual(
            catalog_fields["selection_registry_sha256"],
            {"type": "sha256", "required": True},
        )

        result = self._run_cli()

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_020_reason_envelope_rejects_unknown_membership(self) -> None:
        def add_unknown(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["reason_envelope_registry"]["request_error"][
                "reason_codes"
            ].append("residency_unknown")

        self._replace_inventory(add_unknown)

        self.assert_invalid(self._run_cli(), "membership is not closed")

    def test_030_reason_envelope_rejects_duplicate_membership(self) -> None:
        def duplicate_reason(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            codes = registry["reason_envelope_registry"]["operation_revision"][
                "reason_codes"
            ]
            codes.append(codes[0])

        self._replace_inventory(duplicate_reason)

        self.assert_invalid(self._run_cli(), "contains duplicate values")

    def test_040_reason_row_must_match_reverse_envelope_membership(self) -> None:
        def drop_context(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            del registry["reason_registry"]["residency_capability_unsupported"][
                "envelopes"
            ]["coordinator_step_result"]

        self._replace_inventory(drop_context)

        self.assert_invalid(self._run_cli(), "envelope join disagrees")

    def test_050_operation_reason_rejects_illegal_terminal_state(self) -> None:
        def use_null_terminal_outcome(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["reason_registry"]["residency_operation_succeeded"]["envelopes"][
                "operation_revision"
            ]["contexts"][0]["legal_states"] = ["terminal:null:primary"]

        self._replace_inventory(use_null_terminal_outcome)

        self.assert_invalid(self._run_cli(), "invalid terminal outcome")

    def test_060_contextual_http_status_must_be_registered(self) -> None:
        def use_invalid_status(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["reason_registry"]["residency_precondition_required"]["envelopes"][
                "request_error"
            ]["http_status"] = 700

        self._replace_inventory(use_invalid_status)

        self.assert_invalid(self._run_cli(), "registered HTTP status")

    def test_070_presentation_rejects_bidi_controls(self) -> None:
        def add_bidi_control(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["presentation_registry"]["p_success"][
                "title"
            ] = "Operation \u202esucceeded"

        self._replace_inventory(add_bidi_control)

        self.assert_invalid(self._run_cli(), "contains unsafe controls")

    def test_080_detail_schema_fields_must_close_required_and_optional_lists(
        self,
    ) -> None:
        def drop_detail_field(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            del registry["detail_schema_registry"]["d_authority"]["fields"][
                "failure_class"
            ]

        self._replace_inventory(drop_detail_field)

        self.assert_invalid(self._run_cli(), "field closure disagrees")

    def test_090_schema_enum_ref_must_resolve(self) -> None:
        def use_unknown_ref(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["schema_registry"]["operation_revision"]["fields"]["phase"][
                "ref"
            ] = "operation_registry.unknown_phases"

        self._replace_inventory(use_unknown_ref)

        self.assert_invalid(self._run_cli(), "unknown registry path")

    def test_100_schema_conditional_fields_must_resolve(self) -> None:
        def require_unknown_field(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["schema_registry"]["operation_revision"]["conditionals"][0][
                "require_nonnull"
            ].append("unknown_field")

        self._replace_inventory(require_unknown_field)

        self.assert_invalid(self._run_cli(), "unknown schema fields")

    def test_110_schema_conditional_require_values_must_not_be_empty(self) -> None:
        def empty_required_values(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            conditional = registry["schema_registry"]["authority_transaction_result"][
                "conditionals"
            ][0]
            conditional.pop("require_nonempty")
            conditional["require_values"] = {}

        self._replace_inventory(empty_required_values)

        self.assert_invalid(self._run_cli(), "require_values is empty")

    def test_120_schema_conditional_rejects_mixed_predicate_modes(self) -> None:
        def add_unless_predicate(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            conditional = registry["schema_registry"]["authority_transaction_result"][
                "conditionals"
            ][0]
            conditional["unless"] = conditional["if"]

        self._replace_inventory(add_unless_predicate)

        self.assert_invalid(self._run_cli(), "exactly one predicate mode")

    def test_130_schema_assertion_rejects_conditional_consequences(self) -> None:
        def replace_predicate_with_assertion(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            conditional = registry["schema_registry"]["authority_transaction_result"][
                "conditionals"
            ][0]
            conditional["assert"] = conditional.pop("if")

        self._replace_inventory(replace_predicate_with_assertion)

        self.assert_invalid(self._run_cli(), "assert cannot include consequences")

    def test_135_schema_assertion_rejects_unknown_comparison_root(self) -> None:
        def use_unknown_comparison_root(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            conditional = registry["schema_registry"][
                "deployment_local_overlay_object"
            ]["conditionals"][0]
            conditional["assert"]["less_than_path"] = "unknown_field"

        self._replace_inventory(use_unknown_comparison_root)

        self.assert_invalid(self._run_cli(), "references unknown schema field")

    def test_136_schema_assertion_rejects_malformed_comparison_path(self) -> None:
        def use_malformed_comparison_path(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            conditional = registry["schema_registry"][
                "deployment_local_overlay_object"
            ]["conditionals"][0]
            conditional["assert"]["less_than_path"] = "not a schema path"

        self._replace_inventory(use_malformed_comparison_path)

        self.assert_invalid(self._run_cli(), "is not a valid schema path")

    def test_137_schema_if_rejects_path_comparisons(self) -> None:
        baseline = copy.deepcopy(self.inventory)
        for key in ("equals_path", "less_than_path", "less_than_or_equal_path"):
            with self.subTest(key=key):
                self.inventory = copy.deepcopy(baseline)
                conditional = self.inventory["contract_registry"]["schema_registry"][
                    "operation_revision"
                ]["conditionals"][0]
                conditional["if"][key] = "phase"
                self._write_inventory()

                self.assert_invalid(
                    self._run_cli(), "path comparisons require an assert predicate"
                )

    def test_138_schema_unless_rejects_path_comparisons(self) -> None:
        baseline = copy.deepcopy(self.inventory)
        for key in ("equals_path", "less_than_path", "less_than_or_equal_path"):
            with self.subTest(key=key):
                self.inventory = copy.deepcopy(baseline)
                conditional = self.inventory["contract_registry"]["schema_registry"][
                    "operation_revision"
                ]["conditionals"][0]
                conditional["unless"] = conditional.pop("if")
                conditional["unless"][key] = "phase"
                self._write_inventory()

                self.assert_invalid(
                    self._run_cli(), "path comparisons require an assert predicate"
                )

    def test_140_resource_vocabulary_is_closed(self) -> None:
        def add_writer_state(inventory: dict[str, object]) -> None:
            registry = inventory["contract_registry"]
            registry["http_auth_registry"]["resource_vocabularies"][
                "writer_normalized_states"
            ].append("terminal_unknown")

        self._replace_inventory(add_writer_state)

        self.assert_invalid(self._run_cli(), "resource vocabularies")

    def test_150_promotion_units_remain_inactive(self) -> None:
        def raise_capability(inventory: dict[str, object]) -> None:
            inventory["exact_cells"][0]["capability_level"] = "modeled"

        self._replace_inventory(raise_capability)

        self.assert_invalid(
            self._run_cli(), "cannot accept capability while delivery is absent"
        )


if __name__ == "__main__":
    import unittest

    unittest.main()
