"""Frozen source-contract tests for the residency inventory validator."""

from __future__ import annotations

import copy
import json
import unittest

from residency_capability_inventory_cli_case import ResidencyCapabilityInventoryCliCase


class ResidencyCapabilityInventoryCliTest(ResidencyCapabilityInventoryCliCase):
    """Exercise frozen source-contract behavior through the CLI."""

    def test_frozen_source_read_failure_is_a_bounded_path_diagnostic(self) -> None:
        environment = self._sitecustomize_env("""
import subprocess

_real_run = subprocess.run


def run(*args, **kwargs):
    command = list(args[0])
    if command[:2] == ["git", "show"]:
        raise subprocess.CalledProcessError(
            128,
            command,
            stderr="fatal: injected frozen-source read failure " + ("x" * 4_000),
        )
    return _real_run(*args, **kwargs)


subprocess.run = run
""")

        result = self._run_cli("--render", env=environment)

        self.assert_invalid(result, "cannot read frozen source CMakeLists.txt")
        self.assertIn("fatal: injected frozen-source read failure", result.stderr)
        self.assertLessEqual(len(result.stderr), 700)

    def test_frozen_blob_lookup_failure_is_a_bounded_path_diagnostic(self) -> None:
        environment = self._sitecustomize_env("""
import subprocess

_real_run = subprocess.run


def run(*args, **kwargs):
    command = list(args[0])
    if command[:2] == ["git", "rev-parse"] and ":" in command[-1]:
        raise subprocess.CalledProcessError(
            128,
            command,
            stderr="fatal: injected frozen-blob lookup failure " + ("y" * 4_000),
        )
    return _real_run(*args, **kwargs)


subprocess.run = run
""")

        result = self._run_cli("--render", env=environment)

        self.assert_invalid(result, "cannot resolve frozen source blob CMakeLists.txt")
        self.assertIn("fatal: injected frozen-blob lookup failure", result.stderr)
        self.assertLessEqual(len(result.stderr), 700)

    def test_rocm_arch_gate_rejects_gfx950_nightly_selector(self) -> None:
        def select_nightly(inventory: dict[str, object]) -> None:
            platforms = inventory["platform_predicates"]
            platforms["linux-amd-rocm-llamacpp-gfx950-stable"]["channels"] = ["nightly"]

        self._replace_inventory(select_nightly)

        result = self._run_cli()

        self.assert_invalid(result, "gfx950/nightly")

    def test_schema_version_must_be_seven(self) -> None:
        def use_old_schema(inventory: dict[str, object]) -> None:
            inventory["schema_version"] = 3

        self._replace_inventory(use_old_schema)

        result = self._run_cli("--render")

        self.assert_invalid(result, "schema_version must be 7")

    def test_source_baseline_object_must_be_a_commit(self) -> None:
        def select_blob_as_baseline(inventory: dict[str, object]) -> None:
            inventory["source_support_baseline"] = inventory["source_file_blobs"][
                "CMakeLists.txt"
            ]

        self._replace_inventory(select_blob_as_baseline)

        result = self._run_cli("--render")

        self.assert_invalid(result, "source support baseline must be a Git commit")
        self.assertIn("got blob", result.stderr)

    def test_frozen_source_blob_path_must_resolve_to_a_blob(self) -> None:
        baseline = self.inventory["source_support_baseline"]
        environment = self._sitecustomize_env(f"""
import subprocess

_real_run = subprocess.run


def run(*args, **kwargs):
    command = list(args[0])
    if command[:3] == ["git", "cat-file", "-t"] and command[-1] == "{baseline}:CMakeLists.txt":
        return subprocess.CompletedProcess(command, 0, stdout="tree\\n", stderr="")
    return _real_run(*args, **kwargs)


subprocess.run = run
""")

        result = self._run_cli("--render", env=environment)

        self.assert_invalid(
            result, "frozen source object CMakeLists.txt must be a Git blob"
        )
        self.assertIn("got tree", result.stderr)

    def test_frozen_source_tree_path_must_resolve_to_a_tree(self) -> None:
        baseline = self.inventory["source_support_baseline"]
        environment = self._sitecustomize_env(f"""
import subprocess

_real_run = subprocess.run


def run(*args, **kwargs):
    command = list(args[0])
    if command[:3] == ["git", "cat-file", "-t"] and command[-1] == "{baseline}:src/cpp":
        return subprocess.CompletedProcess(command, 0, stdout="blob\\n", stderr="")
    return _real_run(*args, **kwargs)


subprocess.run = run
""")

        result = self._run_cli("--render", env=environment)

        self.assert_invalid(result, "frozen source object src/cpp must be a Git tree")
        self.assertIn("got blob", result.stderr)

    def test_malformed_descriptor_initializer_fails_closed(self) -> None:
        descriptor = "src/cpp/include/lemon/backends/llamacpp/llamacpp.h"

        def corrupt_initializer(source: str) -> str:
            marker = source.index("/*support*/")
            opening_brace = source.index("{", marker)
            return (
                source[:opening_brace]
                + "not_an_initializer"
                + source[opening_brace + 1 :]
            )

        self._commit_source_change(descriptor, corrupt_initializer)

        result = self._run_cli()

        self.assert_invalid(result, "unsupported C++ initializer token")

    def test_missing_source_disposition_fails_closed(self) -> None:
        def remove_disposition(inventory: dict[str, object]) -> None:
            inventory["exclusions"][0]["source_support"].pop()

        self._replace_inventory(remove_disposition)

        result = self._run_cli()

        self.assert_invalid(result, "must have exactly one disposition, got []")

    def test_duplicate_source_disposition_fails_closed(self) -> None:
        def duplicate_disposition(inventory: dict[str, object]) -> None:
            source_support = inventory["exclusions"][0]["source_support"]
            source_support.append(copy.deepcopy(source_support[0]))

        self._replace_inventory(duplicate_disposition)

        result = self._run_cli()

        self.assert_invalid(result, "must have exactly one disposition")

    def test_exclusion_must_dispose_at_least_one_source_item(self) -> None:
        def add_inert_exclusion(inventory: dict[str, object]) -> None:
            inventory["exclusions"].append(
                {
                    "id": "inert-exclusion",
                    "behavior": "does not dispose any frozen source item",
                }
            )

        self._replace_inventory(add_inert_exclusion)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must dispose at least one source item")

    def test_selector_without_frozen_source_row_fails_closed(self) -> None:
        def add_unknown_architecture(inventory: dict[str, object]) -> None:
            platform = inventory["platform_predicates"][
                "linux-amd-rocm-llamacpp-gfx950-stable"
            ]
            platform["architectures"].append("gfx999")

        self._replace_inventory(add_unknown_architecture)

        result = self._run_cli()

        self.assert_invalid(result, "configured selector has no frozen source row")
        self.assertIn("gfx999/stable", result.stderr)

    def test_unavailable_baseline_reports_directed_fetch_action(self) -> None:
        unavailable = "0" * 40

        def replace_baseline(inventory: dict[str, object]) -> None:
            inventory["source_support_baseline"] = unavailable

        self._replace_inventory(replace_baseline)

        result = self._run_cli()

        self.assert_invalid(result, f"source support baseline {unavailable}")
        self.assertIn(
            "git fetch --no-tags --depth=1 "
            "https://github.com/lemonade-sdk/lemonade.git "
            f"{unavailable}",
            result.stderr,
        )
        self.assertNotIn("git fetch upstream", result.stderr)

    def test_source_baseline_change_is_visible_in_both_projections(self) -> None:
        accepted = self.inventory["source_support_baseline"]
        accepted_render = self._run_cli("--render")
        self.assertEqual(accepted_render.returncode, 0, accepted_render.stderr)

        descendant = self._commit_source_change(
            "docs/index.html",
            lambda source: (
                source + "\n<!-- residency baseline projection fixture -->\n"
            ),
        )
        self.assertNotEqual(descendant, accepted)

        result = self._run_cli("--render")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(result.stdout, accepted_render.stdout)
        self.assertEqual(result.stdout.count(descendant), 2)

    def test_registry_selection_helper_is_a_required_source_input(self) -> None:
        def omit_registry_helper(inventory: dict[str, object]) -> None:
            inventory["source_file_blobs"].pop("src/cpp/include/lemon/registry_files.h")

        self._replace_inventory(omit_registry_helper)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "missing=['src/cpp/include/lemon/registry_files.h']",
        )

    def test_selector_discovery_disables_git_color(self) -> None:
        environment = self._sitecustomize_env("""
import subprocess

_real_run = subprocess.run


def run(*args, **kwargs):
    command = list(args[0])
    if command[:2] == ["git", "grep"] and "--no-color" not in command:
        return subprocess.CompletedProcess(
            command, 2, stdout="", stderr="configured Git color escaped a path"
        )
    return _real_run(*args, **kwargs)


subprocess.run = run
""")

        result = self._run_cli("--render", env=environment)

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_backend_artifact_selector_override_is_frozen(self) -> None:
        selector_path = "src/cpp/server/backends/acestep/acestep_server.cpp"
        accepted_blobs = dict(self.inventory["source_file_blobs"])
        self._commit_source_change(
            selector_path,
            lambda source: source.replace('"vae-BF16.gguf"', '"vae-F16.gguf"', 1),
        )
        self.inventory["source_file_blobs"] = accepted_blobs
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, f"source blob mismatch for {selector_path}")

    def test_new_backend_artifact_selector_override_must_be_frozen(self) -> None:
        selector_path = "src/cpp/server/backends/vllm/vllm_server.cpp"
        self._commit_source_change(
            selector_path,
            lambda source: source + "\n// select_checkpoint_files override fixture\n",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, f"missing=['{selector_path}']")

    def test_delegated_selector_helper_is_bound_by_the_cpp_tree(self) -> None:
        accepted_trees = dict(self.inventory["source_tree_objects"])
        helper_path = "src/cpp/include/lemon/backends/backend_utils.h"
        self._commit_source_change(
            helper_path,
            lambda source: source + "\n// delegated artifact selector fixture\n",
        )
        self.inventory["source_tree_objects"] = accepted_trees
        self._write_inventory()

        result = self._run_cli("--render")

        self.assert_invalid(result, "source tree mismatch for src/cpp")

    def test_removed_backend_artifact_selector_override_is_rejected(self) -> None:
        selector_path = "src/cpp/server/backends/acestep/acestep_server.cpp"
        self._commit_source_change(
            selector_path,
            lambda source: source.replace(
                "select_checkpoint_files", "retired_checkpoint_files", 1
            ),
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, f"extra=['{selector_path}']")

    def test_relocated_backend_artifact_selector_override_is_rejected(self) -> None:
        old_path = "src/cpp/server/backends/acestep/acestep_server.cpp"
        new_path = "src/cpp/server/backends/vllm/vllm_server.cpp"
        self._commit_source_change(
            old_path,
            lambda source: source.replace(
                "select_checkpoint_files", "retired_checkpoint_files", 1
            ),
        )
        self._commit_source_change(
            new_path,
            lambda source: source + "\n// select_checkpoint_files relocation fixture\n",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, f"missing=['{new_path}']")
        self.assertIn(f"extra=['{old_path}']", result.stderr)

    def test_provider_must_match_the_source_backend(self) -> None:
        def mismatch_provider(inventory: dict[str, object]) -> None:
            inventory["platform_predicates"]["linux-amd-rocm-llamacpp"][
                "provider"
            ] = "vulkan"

        self._replace_inventory(mismatch_provider)

        result = self._run_cli("--render")

        self.assert_invalid(result, "provider must be derived from backend rocm")

    def test_provider_mapping_rejects_lockstep_bogus_values(self) -> None:
        def replace_provider_everywhere(inventory: dict[str, object]) -> None:
            inventory["provider_keys"]["rocm"] = "bogus-rocm"
            for platform in inventory["platform_predicates"].values():
                if platform["provider"] == "rocm":
                    platform["provider"] = "bogus-rocm"

        self._replace_inventory(replace_provider_everywhere)

        result = self._run_cli("--render")

        self.assert_invalid(
            result, "provider_keys must equal the accepted backend-to-provider mapping"
        )

    def test_topology_must_match_the_source_accelerator(self) -> None:
        def mismatch_topology(inventory: dict[str, object]) -> None:
            inventory["platform_predicates"]["windows-xdna2"][
                "topology"
            ] = "provider_resolved"

        self._replace_inventory(mismatch_topology)

        result = self._run_cli("--render")

        self.assert_invalid(result, "topology must be derived from accelerator amd_npu")

    def test_topology_mapping_rejects_lockstep_bogus_values(self) -> None:
        def replace_topology_everywhere(inventory: dict[str, object]) -> None:
            inventory["topology_rules"]["amd_npu"] = "bogus-npu"
            for platform in inventory["platform_predicates"].values():
                if platform["accelerator"] == "amd_npu":
                    platform["topology"] = "bogus-npu"

        self._replace_inventory(replace_topology_everywhere)

        result = self._run_cli("--render")

        self.assert_invalid(
            result,
            "topology_rules must equal the accepted accelerator-to-topology mapping",
        )

    def test_hardware_refinement_may_not_widen_its_source_atom(self) -> None:
        def widen_source_atom(inventory: dict[str, object]) -> None:
            inventory["hardware_profiles"]["hatchery-gfx1151-shared-gtt-v1"][
                "source_support"
            ]["architectures"].append("gfx1150")

        self._replace_inventory(widen_source_atom)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must select exactly one source atom")

    def test_flm_contract_rejects_a_reranking_slot(self) -> None:
        def add_reranking(inventory: dict[str, object]) -> None:
            inventory["recipe_model_type_contracts"]["flm"]["model_types"].append(
                "reranking"
            )

        self._replace_inventory(add_reranking)

        result = self._run_cli("--render")

        self.assert_invalid(result, "recipe model-type contract flm")

    def test_frozen_flm_sources_reject_reranking_slot_drift(self) -> None:
        server_source = "src/cpp/server/backends/fastflowlm/fastflowlm_server.cpp"
        self._commit_source_change(
            server_source,
            lambda source: source + "\n// ModelType::RERANKING is not a slot.\n",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "independent reranking model-type slot")

    def test_label_rule_parser_rejects_conjunctions(self) -> None:
        model_types = "src/cpp/include/lemon/model_types.h"

        def add_conjunction(source: str) -> str:
            return source.replace(
                'label == "embeddings" || label == "embedding"',
                'label == "embeddings" && label == "embedding"',
                1,
            )

        self._commit_source_change(model_types, add_conjunction)

        result = self._run_cli("--render")

        self.assert_invalid(result, "unsupported label-rule condition")

    def test_label_rule_parser_rejects_unparsed_if_bodies(self) -> None:
        model_types = "src/cpp/include/lemon/model_types.h"

        def add_unparsed_statement(source: str) -> str:
            return source.replace(
                'if (label == "reranking") {\n'
                "            return ModelType::RERANKING;\n"
                "        }",
                'if (label == "reranking") {\n'
                "            const bool unexpected = true;\n"
                "            return ModelType::RERANKING;\n"
                "        }",
                1,
            )

        self._commit_source_change(model_types, add_unparsed_statement)

        result = self._run_cli("--render")

        self.assert_invalid(result, "unparsed if body")

    def test_server_models_flm_types_must_fit_frozen_slot_policy(self) -> None:
        server_models = "src/cpp/resources/server_models.json"

        def add_flm_reranker(source: str) -> str:
            models = json.loads(source)
            models["validator-flm-reranker"] = {
                "recipe": "flm",
                "labels": ["reranking"],
            }
            return json.dumps(models, indent=4) + "\n"

        self._commit_source_change(server_models, add_flm_reranker)

        result = self._run_cli("--render")

        self.assert_invalid(result, "server_models FLM model types exceed")

    def test_inventory_json_rejects_duplicate_object_keys(self) -> None:
        source = self.inventory_path.read_text(encoding="utf-8")
        self.inventory_path.write_text(
            source.replace(
                '  "schema_version": 7,',
                '  "schema_version": 7,\n  "schema_version": 7,',
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "duplicate object key 'schema_version'")

    def test_inventory_json_rejects_nested_duplicate_object_keys(self) -> None:
        source = self.inventory_path.read_text(encoding="utf-8")
        self.inventory_path.write_text(
            source.replace(
                '    "document": "docs/research/'
                'hatchery-residency-validation-profile.md",',
                '    "document": "docs/research/'
                'hatchery-residency-validation-profile.md",\n'
                '    "document": "docs/research/'
                'hatchery-residency-validation-profile.md",',
                1,
            ),
            encoding="utf-8",
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "duplicate object key 'document'")

    def test_frozen_server_models_rejects_duplicate_object_keys(self) -> None:
        server_models = "src/cpp/resources/server_models.json"

        def duplicate_first_model(source: str) -> str:
            first_key_start = source.index('"')
            first_key_end = source.index('"', first_key_start + 1)
            model_name = source[first_key_start + 1 : first_key_end]
            model_value = json.loads(source)[model_name]
            duplicate = json.dumps(model_value, indent=4)
            return source.replace(
                "{",
                f'{{\n    "{model_name}": {duplicate},',
                1,
            )

        self._commit_source_change(server_models, duplicate_first_model)

        result = self._run_cli("--render")

        self.assert_invalid(result, "duplicate object key")

    def test_collection_recipe_constant_drift_fails_closed(self) -> None:
        model_types = "src/cpp/include/lemon/model_types.h"
        self._commit_source_change(
            model_types,
            lambda source: source.replace(
                'COLLECTION_OMNI_MODEL_RECIPE = "collection.omni"',
                'COLLECTION_OMNI_MODEL_RECIPE = "collection.changed"',
                1,
            ),
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "frozen collection recipe collection.changed")

    def test_collection_helper_typo_fails_closed(self) -> None:
        model_types = "src/cpp/include/lemon/model_types.h"
        self._commit_source_change(
            model_types,
            lambda source: source.replace(
                "return recipe == COLLECTION_OMNI_MODEL_RECIPE;",
                "return recipe != COLLECTION_OMNI_MODEL_RECIPE;",
                1,
            ),
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "collection helper")

    def test_collection_aggregate_predicate_drift_fails_closed(self) -> None:
        model_types = "src/cpp/include/lemon/model_types.h"
        self._commit_source_change(
            model_types,
            lambda source: source.replace(
                "is_omni_collection_recipe(recipe) || "
                "is_router_collection_recipe(recipe)",
                "is_omni_collection_recipe(recipe)",
                1,
            ),
        )

        result = self._run_cli("--render")

        self.assert_invalid(result, "collection aggregate predicate")

    def test_collection_disposition_deletion_fails_closed(self) -> None:
        def drop_collection(inventory: dict[str, object]) -> None:
            collections = next(
                exclusion
                for exclusion in inventory["exclusions"]
                if exclusion["id"] == "collections"
            )
            collections["non_descriptor_recipes"].pop()

        self._replace_inventory(drop_collection)

        result = self._run_cli("--render")

        self.assert_invalid(result, "must have exactly one disposition")

    def test_collection_disposition_extra_fails_closed(self) -> None:
        def add_collection_typo(inventory: dict[str, object]) -> None:
            collections = next(
                exclusion
                for exclusion in inventory["exclusions"]
                if exclusion["id"] == "collections"
            )
            collections["non_descriptor_recipes"].append("collection.typo")

        self._replace_inventory(add_collection_typo)

        result = self._run_cli("--render")

        self.assert_invalid(result, "is not a frozen collection recipe")


if __name__ == "__main__":
    unittest.main()
