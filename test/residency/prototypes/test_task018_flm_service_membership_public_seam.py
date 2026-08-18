import hashlib
import importlib.util
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FAILURE = "TASK-018 FLM service-membership and ownership prototype is unavailable"
TASK_BASE = "67e4c3ae45ebf3caecb47c5058f005b28cfd15fe"
TASK_ID = "TASK-018"
PROTOTYPE_ID = "flm_service_membership_and_ownership"
SOURCE_PATH = "test/cpp/test_residency_prototype_task018.cpp"
SOURCE_SHA256_PLACEHOLDER = "__TASK018_SOURCE_SHA256__"
EXPECTED_SOURCE_SHA256 = (
    "d8d115a4e88dd0af628fda5b97539aa076380fd83cb099750ee97d93fa9010c0"
)
INVENTORY_PATH = "docs/research/portable-residency-capability-inventory.json"
GENERATED_PROFILES_PATH = "src/cpp/resources/residency_profiles.json"
PLAN_PATH = "plan/architecture-portable-residency-1.md"
ATTEST_RECORDED_OBSERVATION = "--attest-recorded-observation"


def parse_replay_mode(arguments: list[str]) -> bool:
    if not arguments:
        return False
    if arguments == [ATTEST_RECORDED_OBSERVATION]:
        return True
    print(
        f"{Path(__file__).name}: unsupported arguments; expected no arguments or "
        f"{ATTEST_RECORDED_OBSERVATION}",
        file=sys.stderr,
    )
    raise SystemExit(2)


TASK_BASE_LIVE_SOURCE_SHA256 = {
    "src/cpp/include/lemon/utils/process_manager.h": (
        "735346e564f435a5d427f2aaaf3586bb0344bb7f8635197cc6797646f429298e"
    ),
    "src/cpp/include/lemon/wrapped_server.h": (
        "86ea156341e5bdab7cfeb8163f1f8eff767ae9e67ce95cd9094d18e18b5bce63"
    ),
    "src/cpp/server/backends/fastflowlm/fastflowlm_server.cpp": (
        "99473434ba972228c637235d3060aa69dc4dfac7a600de13cabb60ef0679d35c"
    ),
    "src/cpp/include/lemon/backends/fastflowlm/fastflowlm_server.h": (
        "270fe407f7a23632df6c310e011c5a33efaad97215bc16e87d67f9952769b40a"
    ),
    "src/cpp/server/utils/platform/process_linux.cpp": (
        "f6bd1326fbed0806cfc1e49d85aa9235430cd70aafa039b77b4221754d0b10c8"
    ),
    "src/cpp/include/lemon/utils/process_platform.h": (
        "9e28988b7501c7ebb992dfd21a0be59c25a1ebc0aba7f592b3f53545b72a3a9b"
    ),
    "src/cpp/server/utils/platform/process_windows.cpp": (
        "a178b5c89f12d9668a21845a67c81966d0dbff0b9b2a6e6d225fb8b079d0ffd4"
    ),
    "src/cpp/server/utils/process_manager.cpp": (
        "031e6e73cbe4463c98be696cd466e76aa89bf998a8d416a3504d1681da486c23"
    ),
    "src/cpp/server/wrapped_server.cpp": (
        "4dfd0020173a13b7d23c082145d8dc483e367602839fd73e57af7f35eedd85d9"
    ),
}

UPSTREAM_SOURCE = {
    "repository": "fastflowlm_fastflowlm",
    "release": "v0.9.46",
    "tag_object": "40e98422f4fc475dbc51a0fc74279bb2dddce154",
    "commit": "c3825404ea7c20b3a38c775d761d564254e08925",
    "main_blob": "d0500fd701ea10a149e09d30163651ba0236007f",
    "main_sha256": "ef567ecd181862509a804f421de71c239650de2c2b61b55ec2974a071c52117b",
    "server_blob": "b7bf6683ad20e585a156cf8ada79f7a07b073d7f",
    "server_sha256": "455312589db43fdd172b3052785fa4e1e448df9c0ca9fd51d4a2ee12084fbcc8",
}
INVENTORY_SHA256 = "bfc28b215486e29391ab64c0471e17edd7965b42b5f591131ad2e325c51f0739"
GENERATED_PROFILES_SHA256 = (
    "a6dc806b5f69b6c44ec8e373c8880e0ce3f00da4a01a3388d00336d267176e42"
)
RUNTIME_BINDING_DIGEST = (
    "bd25c10593b16860298412dcdf3bf6c433a6ce7db305b37836917c833bf5a16d"
)
GATE_SET_DIGEST = "43a714e46f42a3d43ec10ad94b87fe63abbef01469fab14d0591b2115e8bfaf4"
RECOVERY_PROFILE_DIGEST = (
    "56b6d3a974499cfe990196ff214c51769036129929698d6211b6f7d98e419546"
)
FLM_SELECTOR_DIGEST = "2cbbded8bac3c314b41826fd427a2c441df93d6909f293faf51519dc9bc6b42d"
FALLBACK_BINDING_LIST_DIGEST = (
    "58824fe31c33149d555767aa6a7212a15baccd06dae233fe26f9285dd7bb4f9d"
)

RUNTIME_BINDING_NAMES = [
    "device_identity",
    "backend_artifact_digest",
    "source_build_dependency_closure",
    "driver_runtime_closure",
    "model_manifest_digest",
    "normalized_configuration_digest",
    "evidence_index_digest",
    "evidence_liveness_lease",
]
REC_OPERATION_LEAVES = [
    "service_termination",
    "dead_backend_pruning",
    "same_epoch_recovery_cleanup",
    "prior_epoch_owner_cleanup",
    "artifact_scope_recovery_cleanup",
]
FALLBACK_ID = "residency_recovery_block_unproven_release_v1"
FALLBACKS = {"unproven_release": FALLBACK_ID}
FALLBACK_DEFINITION = {
    "operations": ["REC"],
    "guard": "ownership or verified release unavailable",
    "effect": "block lifecycle readiness and retain maximum plausible claims",
}
RECOVERY_PROFILE = {
    "launch": "prepared",
    "containment": (
        "serving_process_or_service_membership_excluding_external_package_and_model_store"
    ),
    "ownership": "lemonade_serving_process_or_service_only",
    "verified_release": [
        "serving_process_or_service_membership",
        "device_claim",
    ],
}
GATE_SET = {
    "extends": ["windows_xdna2_physical_common_v1"],
    "members": ["W-XDNA2-REC-01"],
}
FLM_SELECTOR = {"npu": "v0.9.46"}

UNIT_SPECS = [
    {
        "unit_id": "W-XDNA2-FLM-NPU-EMBEDDING-REC-v1",
        "model_type": "embedding",
        "issue_id": 52,
        "gate": "windows_xdna2_flm_npu_embedding_rec_v1",
        "record_sha256": (
            "967b08077597a44e75eb588d923b098fd4803685179468472fa3ab1f730352d6"
        ),
        "wrapper_sha256": (
            "11e3ed0ad1f36fe2fe14e8f60b73ff03080b677cf723c6c13bf5af52a5692af4"
        ),
    },
    {
        "unit_id": "W-XDNA2-FLM-NPU-LLM-REC-v1",
        "model_type": "llm",
        "issue_id": 46,
        "gate": "windows_xdna2_flm_npu_llm_rec_v1",
        "record_sha256": (
            "271ef0c8de286bbbd653cc97dc3c2ee4d6cd5f377e04d2c0cd2e28042df61ac3"
        ),
        "wrapper_sha256": (
            "70055cbb798706e093ac7d7c608407753219d49ff55bae1c08bf5306bb48cd6f"
        ),
    },
    {
        "unit_id": "W-XDNA2-FLM-NPU-TRANSCRIPTION-REC-v1",
        "model_type": "transcription",
        "issue_id": 58,
        "gate": "windows_xdna2_flm_npu_transcription_rec_v1",
        "record_sha256": (
            "3dd0b4114b2397c18f7243a4a1148f5f00a592e8ced8f5b92f77f55011ddf5c1"
        ),
        "wrapper_sha256": (
            "ba540b4b34f8c022d54a1a87378b7c78b7352a295ef44531a6ff6039ed6fdd57"
        ),
    },
]
UNIT_IDS = [spec["unit_id"] for spec in UNIT_SPECS]

EXPECTED_LINUX_ROWS = [
    "upstream.repository=fastflowlm_fastflowlm",
    "upstream.release=v0.9.46",
    "upstream.tag_object=40e98422f4fc475dbc51a0fc74279bb2dddce154",
    "upstream.commit=c3825404ea7c20b3a38c775d761d564254e08925",
    "upstream.main_blob=d0500fd701ea10a149e09d30163651ba0236007f",
    "upstream.main_sha256=ef567ecd181862509a804f421de71c239650de2c2b61b55ec2974a071c52117b",
    "upstream.server_blob=b7bf6683ad20e585a156cf8ada79f7a07b073d7f",
    "upstream.server_sha256=455312589db43fdd172b3052785fa4e1e448df9c0ca9fd51d4a2ee12084fbcc8",
    "source.topology=monolithic_direct_process",
    "source.server_execution=in_process_threads",
    "source.persistent_serving_child=absent_in_inspected_paths",
    "source.external_service_contract=absent",
    "source.topology_audit=passed",
    "current.launch=direct_process",
    "current.handle_identity=pid_or_process_handle",
    "current.ownership_scope=exact_child",
    "current.posix_termination=pid_signal_and_reap",
    "current.windows_termination=process_handle",
    "current.process_group_membership=absent",
    "current.windows_job_membership=absent",
    "current.scm_service_membership=absent",
    "current.service_instance_birth_token=absent",
    "current.device_claim=absent",
    "current.verified_membership_release=absent",
    "current.verified_device_release=absent",
    "current.recovery_authority=fallback",
    "current.release_decision=fallback",
    "current.claim_disposition=maximum",
    "profile.id=flm_system_managed",
    "profile.launch=prepared",
    "profile.containment=serving_process_or_service_membership_excluding_external_package_and_model_store",
    "profile.external_package=excluded",
    "profile.model_store=excluded",
    "profile.ownership=lemonade_serving_process_or_service_only",
    "profile.release_membership=required",
    "profile.release_device_claim=required",
    "profile.release_composition=both_required",
    "profile.inventory_contract=passed",
    "inventory.unit_runtime_bindings=absent",
    "inventory.unit_material_profiles=empty",
    "inventory.runtime_binding_count=8",
    "inventory.runtime_binding_digest=bd25c10593b16860298412dcdf3bf6c433a6ce7db305b37836917c833bf5a16d",
    "unit.embedding.id=matched",
    "unit.embedding.model_type=embedding",
    "unit.embedding.direct_process=verified_release",
    "unit.embedding.managed_service=verified_release",
    "unit.llm.id=matched",
    "unit.llm.model_type=llm",
    "unit.llm.direct_process=verified_release",
    "unit.llm.managed_service=verified_release",
    "unit.transcription.id=matched",
    "unit.transcription.model_type=transcription",
    "unit.transcription.direct_process=verified_release",
    "unit.transcription.managed_service=verified_release",
    "operation.behavioral_leaf=service_termination",
    "operation.other_rec_leaves=inventory_applicability_only",
    "runtime_binding.device_identity=required",
    "runtime_binding.backend_artifact_digest=required",
    "runtime_binding.source_build_dependency_closure=required",
    "runtime_binding.driver_runtime_closure=required",
    "runtime_binding.model_manifest_digest=required",
    "runtime_binding.normalized_configuration_digest=required",
    "runtime_binding.evidence_index_digest=required",
    "runtime_binding.evidence_liveness_lease=required",
    "runtime_identity.resident_id=required",
    "runtime_identity.resident_generation=required",
    "runtime_identity.backend_instance_birth_token=required",
    "runtime_identity.topology_generation=required",
    "runtime_identity.observation_contract_digest=required",
    "runtime_identity.termination_action_token=required",
    "runtime_identity.required_token_count=14",
    "runtime_identity.required_tokens_nonzero=passed",
    "runtime_identity.exact_match_across_owner_membership_action_device=passed",
    "runtime_identity.evidence_locus_count=7",
    "runtime_identity.exact_match_across_all_loci=passed",
    "runtime_identity.common_token_storage=one_value_per_token_per_locus",
    "runtime_identity.variant_selector=derived_from_closed_selector",
    "runtime_identity.hidden_causal_identity=absent",
    "runtime_identity.selector_authority=single_typed_logical_field",
    "runtime_identity.prepared_authority=single_structural_input",
    "runtime_identity.prepared_authority_identity=one_full_mode_identity",
    "membership.input_contract=bounded_root_and_member_records",
    "membership.member_record_identity=process_pid_birth_executable",
    "direct.profile=direct_process",
    "direct.identity_shape=controller_and_process_pid_birth_executable",
    "direct.service_state=not_applicable",
    "direct.managed_only_fields=structurally_absent",
    "direct.prepared_launch=passed",
    "direct.controller_identity=present_nonzero_exact",
    "direct.process_identity=present_nonzero_exact",
    "direct.process_birth_token=present_nonzero_exact",
    "direct.executable_digest=present_nonzero_exact",
    "direct.prepared_membership_identity_binding=matched",
    "direct.membership_generation_before=7",
    "direct.membership_snapshot_before=present_fresh_healthy_complete",
    "direct.membership_shape_facts=derived_from_bounded_records",
    "direct.membership_root=matched",
    "direct.common_causal_tokens=14_exact",
    "direct.membership_count_before=1",
    "direct.membership_direct_process=matched",
    "direct.external_package_member_count=0",
    "direct.model_store_member_count=0",
    "direct.ownership_scope=serving_process_only",
    "direct.termination_requested=passed",
    "direct.termination_target_mode=direct_process",
    "direct.termination_target_semantic_tuple=unit_model_leaf_mode_common14_and_full_identity",
    "direct.termination_target_controller_identity=present_nonzero_exact",
    "direct.termination_target_process_identity=present_nonzero_exact",
    "direct.termination_target_birth_token=present_nonzero_exact",
    "direct.termination_target_executable_digest=present_nonzero_exact",
    "direct.termination_action_token=present_nonzero_exact",
    "direct.termination_attempted=passed",
    "direct.termination_effect_calls=1",
    "direct.termination_acknowledged=completion_only",
    "direct.termination_ack_target=semantic_tuple_matched",
    "direct.termination_ack_action_token=matched",
    "direct.termination_state_unambiguous=derived_from_typed_ack_and_post_evidence",
    "direct.membership_generation_after=8",
    "direct.membership_generation_increment=checked_uint64_successor",
    "direct.membership_snapshot_after=present_fresh_healthy_complete",
    "direct.membership_after_scope=matched",
    "direct.membership_count_after=0",
    "direct.termination_verified=passed",
    "managed.profile=managed_service",
    "managed.identity_shape=controller_manager_service_instance_generation_and_serving_process",
    "managed.prepared_control=passed",
    "managed.controller_identity=present_nonzero_exact",
    "managed.service_manager_identity=present_nonzero_exact",
    "managed.service_identity_digest=present_nonzero_exact",
    "managed.service_config_digest=present_nonzero_exact",
    "managed.service_instance_birth_token=present_nonzero_exact",
    "managed.service_start_generation=13",
    "managed.service_start_generation_binding=present_nonzero_exact",
    "managed.service_state_before=running",
    "managed.service_state_before_generation=matched",
    "managed.membership_generation_before=11",
    "managed.membership_snapshot_before=present_fresh_healthy_complete",
    "managed.membership_shape_facts=derived_from_bounded_records",
    "managed.membership_service_identity=matched",
    "managed.membership_serving_process_identity=present_nonzero_exact",
    "managed.membership_serving_process_birth_token=present_nonzero_exact",
    "managed.membership_serving_process_executable_digest=present_nonzero_exact",
    "managed.prepared_membership_identity_binding=matched",
    "managed.common_causal_tokens=14_exact",
    "managed.membership_count_before=1",
    "managed.external_package_member_count=0",
    "managed.model_store_member_count=0",
    "managed.ownership_scope=serving_service_only",
    "managed.termination_requested=passed",
    "managed.termination_target_mode=managed_service",
    "managed.termination_target_semantic_tuple=unit_model_leaf_mode_common14_and_full_identity",
    "managed.termination_target_controller_identity=present_nonzero_exact",
    "managed.termination_target_service_manager_identity=present_nonzero_exact",
    "managed.termination_target_service_identity=present_nonzero_exact",
    "managed.termination_target_service_instance_birth_token=present_nonzero_exact",
    "managed.termination_target_config_digest=present_nonzero_exact",
    "managed.termination_target_start_generation=present_nonzero_exact",
    "managed.termination_target_serving_process_identity=present_nonzero_exact",
    "managed.termination_target_serving_process_birth_token=present_nonzero_exact",
    "managed.termination_target_serving_process_executable_digest=present_nonzero_exact",
    "managed.termination_action_token=present_nonzero_exact",
    "managed.termination_attempted=passed",
    "managed.termination_effect_calls=1",
    "managed.termination_acknowledged=completion_only",
    "managed.termination_ack_target=semantic_tuple_matched",
    "managed.termination_ack_action_token=matched",
    "managed.termination_state_unambiguous=derived_from_typed_ack_and_post_evidence",
    "managed.service_state_after=stopped",
    "managed.membership_generation_after=12",
    "managed.membership_generation_increment=checked_uint64_successor",
    "managed.membership_snapshot_after=present_fresh_healthy_complete",
    "managed.membership_after_scope=matched",
    "managed.membership_count_after=0",
    "managed.termination_verified=passed",
    "device.before_present=passed",
    "device.before_fresh=passed",
    "device.before_healthy=passed",
    "device.before_complete=passed",
    "device.observation_generation_before=17",
    "device.common_causal_tokens=14_exact",
    "device.device_identity=present_nonzero_exact",
    "device.device_identity_alias=runtime_binding_device_identity",
    "device.owner_identity=present_nonzero_exact",
    "device.owner_identity_shape=full_mode_specific_identity",
    "device.direct_owner_identity_fields=4_exact",
    "device.managed_owner_identity_fields=9_exact",
    "device.claim_identity=matched",
    "device.prepared_claim_owner_identity=present_nonzero_exact",
    "device.prepared_claim_identity=present_nonzero_exact",
    "device.prepared_claim_generation=23",
    "device.before_owner_identity=matched",
    "device.before_claim_identity=matched",
    "device.before_claim_anchor_binding=matched",
    "device.action_token=common14_binding",
    "device.claim_lookup_scope=keyed_target_claim",
    "device.unrelated_claims=out_of_scope",
    "device.target_claim_cardinality=zero_or_one",
    "device.duplicate_target_keys=rejected",
    "device.claim_presence=derived_from_target_key_count",
    "device.target_claim_before=present",
    "device.after_present=passed",
    "device.after_fresh=passed",
    "device.after_healthy=passed",
    "device.after_complete=passed",
    "device.observation_generation_after=18",
    "device.observation_generation_increment=checked_uint64_successor",
    "device.claim_generation_before=23",
    "device.claim_generation_after=23",
    "device.claim_generation_binding=present_nonzero_exact",
    "device.after_owner_identity=matched",
    "device.after_claim_identity=matched",
    "device.target_claim_after=absent",
    "device.release_verified=passed",
    "device.direct_owner_binding=matched",
    "device.managed_owner_binding=matched",
    "release.direct_membership_and_device=verified_release",
    "release.managed_membership_and_device=verified_release",
    "release.unit_binding=matched",
    "release.runtime_identity=all_14_matched",
    "release.owner_identity=matched",
    "release.action_token=common14_binding",
    "release.membership_proof_scope=membership_and_owner_only",
    "release.device_proof_scope=exact_device_claim_tuple",
    "release.device_proof_prepared_selection_binding=matched",
    "release.device_claim_tuple=device_identity_claim_identity_generation_matched",
    "release.device_identity_composition_axis=common14_runtime_binding_device_identity",
    "release.proof_types=distinct_membership_and_device",
    "release.shared_proof_binding=unit_model_mode_operation_common14_owner",
    "release.raw_decision_proof_input=absent",
    "release.composition_expected_binding=prepared_authority",
    "release.composition_validated_action_context=attempted_and_acknowledged",
    "release.composition_membership_proof_slot=present",
    "release.composition_device_proof_slot=present",
    "release.composition_proof_validity=derived_from_payload",
    "release.composition_credit_stage=after_both_proofs",
    "negative.profile_unknown=unknown",
    "negative.prepared_authority_missing=unknown",
    "negative.each_prepared_selection_missing=unknown",
    "negative.each_prepared_selection_unknown=unknown",
    "negative.each_unit_model_crosswire=unknown",
    "negative.operation_leaf_missing=unknown",
    "negative.operation_leaf_mismatch=unknown",
    "negative.membership_before_missing=unknown",
    "negative.membership_before_stale=unknown",
    "negative.membership_before_incomplete=unknown",
    "negative.each_membership_shape_pre=unknown",
    "negative.each_membership_shape_post=quarantine",
    "negative.each_ownership_pre_invalid=unknown",
    "negative.each_ownership_post_invalid=quarantine",
    "negative.direct_evidence_for_managed_profile=unknown",
    "negative.managed_evidence_for_direct_profile=unknown",
    "negative.each_action_state_pre_dispatch_clean=unknown",
    "negative.each_action_state_no_action_intact=verified_intact",
    "negative.each_action_state_contradiction=quarantine",
    "negative.each_no_action_changed_leg=quarantine",
    "negative.each_precondition_fault_attempted=quarantine",
    "negative.each_precondition_fault_ack_only=quarantine",
    "negative.membership_after_missing=quarantine",
    "negative.membership_after_stale=quarantine",
    "negative.membership_after_incomplete=quarantine",
    "negative.membership_after_nonempty=quarantine",
    "negative.direct_process_identity_reused=quarantine",
    "negative.release_credit_before_both_proofs_no_action=unknown",
    "negative.release_credit_before_both_proofs_attempted=quarantine",
    "negative.each_leg_selector_pre_missing=unknown",
    "negative.each_leg_selector_pre_unknown=unknown",
    "negative.each_selection_leg_crosswire_pre=unknown",
    "negative.each_leg_selector_post_missing=quarantine",
    "negative.each_leg_selector_post_unknown=quarantine",
    "negative.each_selection_leg_crosswire_post=quarantine",
    "negative.each_common_token_pre_missing=unknown",
    "negative.each_common_token_pre_zero=unknown",
    "negative.each_common_token_pre_anchor_mismatch=unknown",
    "negative.each_common_token_post_missing=quarantine",
    "negative.each_common_token_post_zero=quarantine",
    "negative.each_common_token_pre_crossleg_mismatch=unknown",
    "negative.each_common_token_post_crossleg_mismatch=quarantine",
    "negative.each_evidence_liveness_expired_pre=unknown",
    "negative.each_evidence_liveness_expired_no_action=quarantine",
    "negative.each_evidence_liveness_expired_post=quarantine",
    "negative.membership_before_unhealthy=unknown",
    "negative.membership_after_unhealthy=quarantine",
    "negative.membership_generation_before_missing=unknown",
    "negative.membership_generation_before_zero=unknown",
    "negative.membership_generation_before_unchecked=unknown",
    "negative.membership_generation_after_missing=quarantine",
    "negative.membership_generation_after_zero=quarantine",
    "negative.membership_generation_replay=quarantine",
    "negative.membership_generation_regressed=quarantine",
    "negative.membership_generation_skipped=quarantine",
    "negative.membership_generation_overflow_to_zero=quarantine",
    "negative.membership_generation_after_unchecked=quarantine",
    "negative.device_generation_before_missing=unknown",
    "negative.device_generation_before_zero=unknown",
    "negative.device_generation_before_unchecked=unknown",
    "negative.device_generation_after_missing=quarantine",
    "negative.device_generation_after_zero=quarantine",
    "negative.device_generation_replay=quarantine",
    "negative.device_generation_regressed=quarantine",
    "negative.device_generation_skipped=quarantine",
    "negative.device_generation_overflow_to_zero=quarantine",
    "negative.device_generation_after_unchecked=quarantine",
    "negative.each_action_target_missing=unknown",
    "negative.each_action_target_zero=unknown",
    "negative.each_action_target_mismatch=unknown",
    "negative.each_action_target_crosswire=unknown",
    "negative.each_mode_specific_pre_identity_missing=unknown",
    "negative.each_mode_specific_pre_identity_zero=unknown",
    "negative.each_mode_specific_pre_identity_mismatch=unknown",
    "negative.each_device_specific_pre_identity_missing=unknown",
    "negative.each_device_specific_pre_identity_zero=unknown",
    "negative.each_device_specific_pre_identity_mismatch=unknown",
    "negative.each_device_specific_post_identity_missing=quarantine",
    "negative.each_device_specific_post_identity_zero=quarantine",
    "negative.each_device_specific_post_identity_mismatch=quarantine",
    "negative.managed_service_start_generation_missing=unknown",
    "negative.managed_service_start_generation_zero=unknown",
    "negative.managed_service_start_generation_mismatch=unknown",
    "negative.managed_service_post_restart_generation=quarantine",
    "negative.managed_service_state_before_missing=unknown",
    "negative.managed_service_state_before_unknown=unknown",
    "negative.managed_service_state_before_not_running=unknown",
    "negative.managed_service_state_after_missing=quarantine",
    "negative.managed_service_state_after_unknown=quarantine",
    "negative.managed_service_state_after_running=quarantine",
    "negative.managed_service_state_after_wrong=quarantine",
    "negative.device_before_missing=unknown",
    "negative.device_before_stale=unknown",
    "negative.device_before_unhealthy=unknown",
    "negative.device_before_incomplete=unknown",
    "negative.device_before_claim_absent=unknown",
    "negative.device_after_missing=quarantine",
    "negative.device_after_stale=quarantine",
    "negative.device_after_unhealthy=quarantine",
    "negative.device_after_incomplete=quarantine",
    "negative.device_after_claim_present=quarantine",
    "negative.each_device_target_key_pre_invalid=unknown",
    "negative.each_device_target_key_post_invalid=quarantine",
    "negative.release_membership_proof_missing=quarantine",
    "negative.release_device_proof_missing=quarantine",
    "negative.each_release_proof_unit_model_splice=quarantine",
    "negative.each_release_proof_mode_splice=quarantine",
    "negative.each_release_proof_operation_splice=quarantine",
    "negative.each_release_proof_owner_mismatch=quarantine",
    "negative.each_release_proof_common_token_mismatch=quarantine",
    "negative.each_release_device_claim_tuple_missing=quarantine",
    "negative.each_release_device_claim_tuple_zero=quarantine",
    "negative.each_release_device_claim_tuple_present_nonzero_nonexact=quarantine",
    "negative.termination_ack_target_missing=quarantine",
    "negative.termination_ack_target_zero=quarantine",
    "negative.termination_ack_target_mismatch=quarantine",
    "negative.termination_ack_target_crosswire=quarantine",
    "negative.termination_ack_action_token_missing=quarantine",
    "negative.termination_ack_action_token_zero=quarantine",
    "negative.termination_ack_action_token_mismatch=quarantine",
    "negative.device_claim_generation_before_missing=unknown",
    "negative.device_claim_generation_before_zero=unknown",
    "negative.device_claim_generation_before_mismatch=unknown",
    "negative.device_claim_generation_after_missing=quarantine",
    "negative.device_claim_generation_after_zero=quarantine",
    "negative.device_claim_generation_after_mismatch=quarantine",
    "negative.each_direct_mode_specific_post_identity_missing=quarantine",
    "negative.each_direct_mode_specific_post_identity_zero=quarantine",
    "negative.each_direct_mode_specific_post_identity_mismatch=quarantine",
    "negative.each_managed_mode_specific_post_identity_missing=quarantine",
    "negative.each_managed_mode_specific_post_identity_zero=quarantine",
    "negative.each_managed_mode_specific_post_identity_mismatch=quarantine",
    "disposition.precondition_failure=unknown",
    "disposition.precondition_effect_calls=0",
    "disposition.precondition_claims=preserved",
    "disposition.effect_calls_equal_attempted=passed",
    "disposition.rejected_without_effect=verified_intact",
    "disposition.rejected_without_effect_claims=preserved",
    "disposition.intact_no_action_claims=preserved",
    "disposition.post_action_ambiguity=quarantine",
    "disposition.post_action_claims=maximum",
    "disposition.invalid_precondition_attempt_dominance=quarantine",
    "disposition.membership_only_release=quarantine",
    "disposition.device_only_release=quarantine",
    "disposition.partial_proof_claims=maximum",
    "disposition.partial_proof_release_credit=0",
    "disposition.pre_dispatch_unavailable_authority=fallback",
    "disposition.precondition_neutral_state=unchanged_no_action",
    "disposition.fallback_release_credit=0",
    "disposition.quarantine_release_credit=0",
    "disposition.unverified_release_credit=0",
    "synthetic.observation_source=injected",
    "synthetic.direct_process_envelope=verified_release",
    "synthetic.managed_service_envelope=verified_release",
    "synthetic.fail_closed_matrix=passed",
    "synthetic.negative_fixture_shapes=passed",
    "synthetic.action_state_matrix=passed",
    "synthetic.precondition_action_dominance=passed",
    "synthetic.device_target_claim_lookup=passed",
    "synthetic.negative_fault_class_count=132",
    "synthetic.negative_variant_count=24303",
    "synthetic.negative_case_manifest_sha256=f655d1d22e21d61de56531bd95565dc016ccf235f5e80fd073f0bc567baa7342",
    "synthetic.negative_labeled_manifest_sha256=5c5be17cd31b46feb4f32c83428f95cf87e52f39e6adeb9955fa1efe4e700a85",
    "synthetic.negative_unordered_pair_count=295305753",
    "synthetic.negative_input_descriptor_unique=passed",
    "synthetic.negative_pairwise_unique=passed",
    "synthetic.negative_input_execution=passed",
    "synthetic.input_domain_partition=raw_verification_and_proof_composition",
    "synthetic.zero_release_credit_matrix=passed",
    "native.windows_service_membership=deferred",
    "native.windows_npu_device_claim=deferred",
    "fallback_binding.embedding=residency_recovery_block_unproven_release_v1",
    "fallback_binding.llm=residency_recovery_block_unproven_release_v1",
    "fallback_binding.transcription=residency_recovery_block_unproven_release_v1",
    "platform.current=linux",
    "runtime_authority=none",
]
NEGATIVE_ROWS = [row for row in EXPECTED_LINUX_ROWS if row.startswith("negative.")]
COMMON_CAUSAL_TOKEN_FIELDS = [
    "runtime_binding.device_identity",
    "runtime_binding.backend_artifact_digest",
    "runtime_binding.source_build_dependency_closure",
    "runtime_binding.driver_runtime_closure",
    "runtime_binding.model_manifest_digest",
    "runtime_binding.normalized_configuration_digest",
    "runtime_binding.evidence_index_digest",
    "runtime_binding.evidence_liveness_lease",
    "runtime_identity.resident_id",
    "runtime_identity.resident_generation",
    "runtime_identity.backend_instance_birth_token",
    "runtime_identity.topology_generation",
    "runtime_identity.observation_contract_digest",
    "runtime_identity.termination_action_token",
]
DIRECT_PRE_IDENTITY_FIELDS = [
    "direct.controller_identity",
    "direct.process_identity",
    "direct.process_birth_token",
    "direct.executable_digest",
]
MANAGED_PRE_IDENTITY_FIELDS = [
    "managed.controller_identity",
    "managed.service_manager_identity",
    "managed.service_identity_digest",
    "managed.service_config_digest",
    "managed.service_instance_birth_token",
    "managed.membership_serving_process_identity",
    "managed.membership_serving_process_birth_token",
    "managed.membership_serving_process_executable_digest",
]
DIRECT_TARGET_FIELDS = [
    "direct.termination_target_controller_identity",
    "direct.termination_target_process_identity",
    "direct.termination_target_birth_token",
    "direct.termination_target_executable_digest",
]
MANAGED_TARGET_FIELDS = [
    "managed.termination_target_controller_identity",
    "managed.termination_target_service_manager_identity",
    "managed.termination_target_service_identity",
    "managed.termination_target_service_instance_birth_token",
    "managed.termination_target_config_digest",
    "managed.termination_target_start_generation",
    "managed.termination_target_serving_process_identity",
    "managed.termination_target_serving_process_birth_token",
    "managed.termination_target_serving_process_executable_digest",
]
DIRECT_POST_IDENTITY_FIELDS = [f"post.{field}" for field in DIRECT_PRE_IDENTITY_FIELDS]
MANAGED_POST_IDENTITY_FIELDS = [
    *(f"post.{field}" for field in MANAGED_PRE_IDENTITY_FIELDS),
    "post.managed.service_start_generation",
]
DEVICE_IDENTITY_FIELDS_BY_MODE = {
    "direct_process": [
        "device.owner.controller_identity",
        "device.owner.process_identity",
        "device.owner.process_birth_token",
        "device.owner.executable_digest",
        "device.claim_identity",
    ],
    "managed_service": [
        "device.owner.controller_identity",
        "device.owner.service_manager_identity",
        "device.owner.service_identity_digest",
        "device.owner.service_config_digest",
        "device.owner.service_instance_birth_token",
        "device.owner.membership_serving_process_identity",
        "device.owner.membership_serving_process_birth_token",
        "device.owner.membership_serving_process_executable_digest",
        "device.owner.service_start_generation",
        "device.claim_identity",
    ],
}
MEMBERSHIP_MODES = ["direct_process", "managed_service"]
MODE_PRE_IDENTITY_FIELDS = {
    "direct_process": DIRECT_PRE_IDENTITY_FIELDS,
    "managed_service": MANAGED_PRE_IDENTITY_FIELDS,
}
MODE_TARGET_FIELDS = {
    "direct_process": DIRECT_TARGET_FIELDS,
    "managed_service": MANAGED_TARGET_FIELDS,
}
UNIT_MODEL_TYPES = ["embedding", "llm", "transcription"]
MEMBERSHIP_SHAPES = [
    "zero_root",
    "zero_member_declared_zero",
    "wrong_root",
    "duplicate_root",
    "declared_count_mismatch",
    "unknown_member",
    "duplicate_member",
    "external_package_member",
    "model_store_member",
    "member_pid_missing",
    "member_pid_zero",
    "member_pid_mismatch",
    "member_birth_token_missing",
    "member_birth_token_zero",
    "member_birth_token_mismatch",
    "member_executable_digest_missing",
    "member_executable_digest_zero",
    "member_executable_digest_mismatch",
]
MEMBERSHIP_SHAPES_BY_PHASE = {
    "pre": MEMBERSHIP_SHAPES,
    "post": [
        "zero_root",
        "wrong_root",
        "duplicate_root",
        "declared_count_mismatch",
    ],
}
OWNERSHIP_SHAPES = ["missing", "scope_too_broad", "shared_ambiguous"]
DEVICE_TARGET_KEY_SHAPES_BY_PHASE = {
    "pre": [
        "duplicate_target_key",
        "ambiguous_target_key",
        "unrelated_claim_identity_missing",
        "unrelated_claim_identity_zero",
        "unrelated_claim_generation_missing",
        "unrelated_claim_generation_zero",
    ],
    "post": [
        "candidate_wrong_generation",
        "unrelated_claim_identity_missing",
        "unrelated_claim_identity_zero",
        "unrelated_claim_generation_missing",
        "unrelated_claim_generation_zero",
    ],
}
MEMBERSHIP_SHAPE_MUTATIONS = {
    "zero_root": (
        "root_record_count=0,member_record_count=1,"
        "member_identity=expected_pid_birth_executable,declared_member_count=1"
    ),
    "zero_member_declared_zero": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=0,declared_member_count=0"
    ),
    "wrong_root": (
        "root_record_count=1,root_identity=wrong_valid_identity,"
        "member_record_count=1,member_identity=expected_pid_birth_executable,"
        "declared_member_count=1"
    ),
    "duplicate_root": (
        "root_record_count=2,root_identity=duplicate_same_identity,"
        "member_record_count=1,member_identity=expected_pid_birth_executable,"
        "declared_member_count=1"
    ),
    "declared_count_mismatch": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=1,member_identity=expected_pid_birth_executable,"
        "declared_member_count=2"
    ),
    "unknown_member": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=1,member_identity=unknown_pid_birth_executable,"
        "member_class=unknown,declared_member_count=1"
    ),
    "duplicate_member": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=2,member_identity=duplicate_same_pid_birth_executable,"
        "member_class=serving_process,declared_member_count=2"
    ),
    "external_package_member": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=1,member_identity=external_package_identity,"
        "member_class=external_package,declared_member_count=1"
    ),
    "model_store_member": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=1,member_identity=model_store_identity,"
        "member_class=model_store,declared_member_count=1"
    ),
    "member_pid_missing": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=absent,member_birth_token=expected,"
        "member_executable_digest=expected,declared_member_count=1"
    ),
    "member_pid_zero": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=0,member_birth_token=expected,member_executable_digest=expected,"
        "declared_member_count=1"
    ),
    "member_pid_mismatch": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=valid_other,member_birth_token=expected,"
        "member_executable_digest=expected,declared_member_count=1"
    ),
    "member_birth_token_missing": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=absent,"
        "member_executable_digest=expected,declared_member_count=1"
    ),
    "member_birth_token_zero": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=0,member_executable_digest=expected,"
        "declared_member_count=1"
    ),
    "member_birth_token_mismatch": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=valid_other,"
        "member_executable_digest=expected,declared_member_count=1"
    ),
    "member_executable_digest_missing": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=expected,"
        "member_executable_digest=absent,declared_member_count=1"
    ),
    "member_executable_digest_zero": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=expected,"
        "member_executable_digest=0,declared_member_count=1"
    ),
    "member_executable_digest_mismatch": (
        "root_record_count=1,root_identity=expected_identity,member_record_count=1,"
        "member_pid=expected,member_birth_token=expected,"
        "member_executable_digest=valid_other,declared_member_count=1"
    ),
}
POST_MEMBERSHIP_SHAPE_MUTATIONS = {
    "zero_root": ("root_record_count=0,member_record_count=0,declared_member_count=0"),
    "wrong_root": (
        "root_record_count=1,root_identity=wrong_valid_identity,"
        "member_record_count=0,declared_member_count=0"
    ),
    "duplicate_root": (
        "root_record_count=2,root_identity=duplicate_same_identity,"
        "member_record_count=0,declared_member_count=0"
    ),
    "declared_count_mismatch": (
        "root_record_count=1,root_identity=expected_identity,"
        "member_record_count=0,declared_member_count=1"
    ),
}
COMMON_PRE_LOCI = [
    "prepared_authority",
    "membership_before",
    "action_target",
    "device_before",
]
COMMON_POST_LOCI = ["membership_after", "ack_target", "device_after"]
COMMON_NON_ANCHOR_LOCI = [
    "membership_before",
    "action_target",
    "device_before",
    "membership_after",
    "ack_target",
    "device_after",
]
TARGET_NONCOMMON_FIELDS = [
    "selection.unit_id",
    "selection.model_type",
    "operation.behavioral_leaf",
    "membership.mode",
]
NON_BEHAVIORAL_OPERATION_LEAVES = [*REC_OPERATION_LEAVES[1:], "unknown"]
ACTION_REQUEST_STATES = ["absent", "rejected", "accepted"]
ACTION_ACK_STATES = ["absent", "valid"]
ACTION_CHANGED_LEGS = ["membership", "device", "both"]
PRECONDITION_FAULT_KEYS = [
    "negative.profile_unknown",
    "negative.prepared_authority_missing",
    "negative.each_prepared_selection_missing",
    "negative.each_prepared_selection_unknown",
    "negative.each_unit_model_crosswire",
    "negative.operation_leaf_missing",
    "negative.operation_leaf_mismatch",
    "negative.membership_before_missing",
    "negative.membership_before_stale",
    "negative.membership_before_incomplete",
    "negative.each_membership_shape_pre",
    "negative.each_ownership_pre_invalid",
    "negative.direct_evidence_for_managed_profile",
    "negative.managed_evidence_for_direct_profile",
    "negative.each_leg_selector_pre_missing",
    "negative.each_leg_selector_pre_unknown",
    "negative.each_selection_leg_crosswire_pre",
    "negative.each_common_token_pre_missing",
    "negative.each_common_token_pre_zero",
    "negative.each_common_token_pre_anchor_mismatch",
    "negative.each_common_token_pre_crossleg_mismatch",
    "negative.each_evidence_liveness_expired_pre",
    "negative.membership_before_unhealthy",
    "negative.membership_generation_before_missing",
    "negative.membership_generation_before_zero",
    "negative.membership_generation_before_unchecked",
    "negative.device_generation_before_missing",
    "negative.device_generation_before_zero",
    "negative.device_generation_before_unchecked",
    "negative.each_action_target_missing",
    "negative.each_action_target_zero",
    "negative.each_action_target_mismatch",
    "negative.each_action_target_crosswire",
    "negative.each_mode_specific_pre_identity_missing",
    "negative.each_mode_specific_pre_identity_zero",
    "negative.each_mode_specific_pre_identity_mismatch",
    "negative.each_device_specific_pre_identity_missing",
    "negative.each_device_specific_pre_identity_zero",
    "negative.each_device_specific_pre_identity_mismatch",
    "negative.managed_service_start_generation_missing",
    "negative.managed_service_start_generation_zero",
    "negative.managed_service_start_generation_mismatch",
    "negative.managed_service_state_before_missing",
    "negative.managed_service_state_before_unknown",
    "negative.managed_service_state_before_not_running",
    "negative.device_before_missing",
    "negative.device_before_stale",
    "negative.device_before_unhealthy",
    "negative.device_before_incomplete",
    "negative.device_before_claim_absent",
    "negative.each_device_target_key_pre_invalid",
    "negative.device_claim_generation_before_missing",
    "negative.device_claim_generation_before_zero",
    "negative.device_claim_generation_before_mismatch",
]
NEGATIVE_MANIFEST_SHA256 = (
    "f655d1d22e21d61de56531bd95565dc016ccf235f5e80fd073f0bc567baa7342"
)
NEGATIVE_LABELED_MANIFEST_SHA256 = (
    "5c5be17cd31b46feb4f32c83428f95cf87e52f39e6adeb9955fa1efe4e700a85"
)
NEGATIVE_VARIANT_COUNT = 24303
RAW_NEGATIVE_VARIANT_COUNT = 23889
PROOF_COMPOSITION_NEGATIVE_VARIANT_COUNT = 414
NEGATIVE_UNORDERED_PAIR_COUNT = 295305753

ALLOWED_STANDARD_HEADERS = {
    "array",
    "cstddef",
    "cstdint",
    "iostream",
    "limits",
    "string_view",
}
ALLOWED_STANDARD_SYMBOLS = {
    "array",
    "cout",
    "numeric_limits",
    "size_t",
    "string_view",
    "uint64_t",
}
ALLOWED_PLATFORM_DIRECTIVES = [
    "#ifdef _WIN32",
    "#elif defined(__APPLE__)",
    "#elif defined(__linux__)",
    "#else",
    "#endif",
]
FORBIDDEN_PROTOTYPE_SOURCE_PATTERNS = (
    re.compile(
        r"\b(?:extern|system|popen|fork|vfork|clone|"
        r"_?exec(?:l(?:e|p|pe)?|v(?:e|p|pe)?)|"
        r"posix_spawn[a-z0-9_]*|kill|killpg|wait|waitpid|pidfd_[a-z0-9_]*|"
        r"createprocess[a-z0-9_]*|terminateprocess|openprocess|"
        r"openscmanager[a-z0-9_]*|openservice[a-z0-9_]*|"
        r"controlservice|queryservice[a-z0-9_]*|startservice[a-z0-9_]*|"
        r"deviceiocontrol|createfile[a-z0-9_]*|"
        r"open|openat|fopen|creat|mmap|munmap|mprotect|madvise|"
        r"filesystem|ifstream|ofstream|fstream|"
        r"pthread_create|thrd_create|thread|jthread|"
        r"sleep|usleep|nanosleep|clock|gettimeofday|getenv|"
        r"random_device|rand|srand|"
        r"dlopen|dlsym|loadlibrary[a-z0-9_]*|getprocaddress|"
        r"socket|socketpair|accept|bind|listen|connect|send|recv|"
        r"ioctl|syscall|__builtin_[a-z0-9_]*|_?alloca)\b"
    ),
    re.compile(r"\b_spawn[a-z0-9_]*\b"),
    re.compile(r"\b(?:asm|__asm__?)\b"),
    re.compile(
        r"\b(?:malloc|calloc|realloc|free|new|delete|make_unique|make_shared)\b"
    ),
    re.compile(r"\busing\s+namespace\s+std\b|\bnamespace\s+[a-z0-9_]+\s*=\s*std\b"),
)


def fail_unavailable() -> None:
    print(FAILURE, file=sys.stderr)
    raise SystemExit(1)


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def repository_root() -> Path:
    resolved = Path(__file__).resolve()
    installed = resolved.parents[3] if len(resolved.parents) > 3 else Path("/")
    contract_path = Path("test/residency/prototypes/result_contract.py")
    if (installed / contract_path).is_file():
        return installed
    return Path.cwd().resolve()


def parse_json_object(raw: bytes, label: str) -> dict:
    def object_from_pairs(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"{label} contains duplicate JSON key {key}")
            result[key] = value
        return result

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=object_from_pairs)
    require(type(value) is dict, f"{label} must be a JSON object")
    return value


def canonical_json_bytes(value) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def canonical_json_sha256(value) -> str:
    return hashlib.sha256(canonical_json_bytes(value)).hexdigest()


def unit_model_crosswire_variants() -> list[str]:
    return [
        (
            f"phase=pre|attempted=false|mode={mode}|"
            f"locus=prepared_authority|"
            f"unit_id={selected['unit_id']}|"
            f"model_type={other['model_type']}"
        )
        for mode in MEMBERSHIP_MODES
        for selected in UNIT_SPECS
        for other in UNIT_SPECS
        if selected["unit_id"] != other["unit_id"]
    ]


def prepared_selection_variants(state: str) -> list[str]:
    return [
        (
            f"phase=pre|attempted=false|locus=prepared_authority|"
            f"mode={mode}|field={field}|state={state}"
        )
        for mode in MEMBERSHIP_MODES
        for field in (
            "selection.unit_id",
            "selection.model_type",
            "membership.mode",
        )
    ]


def operation_leaf_missing_variants() -> list[str]:
    return [
        (
            f"phase=pre|attempted=false|locus=prepared_authority|"
            f"mode={mode}|field=operation.behavioral_leaf|state=missing"
        )
        for mode in MEMBERSHIP_MODES
    ]


def operation_leaf_variants() -> list[str]:
    return [
        (
            f"phase=pre|attempted=false|locus=prepared_authority|"
            f"mode={mode}|operation_leaf={leaf}"
        )
        for mode in MEMBERSHIP_MODES
        for leaf in NON_BEHAVIORAL_OPERATION_LEAVES
    ]


def leg_selector_scalar_variants(phase: str, mutation: str) -> list[str]:
    attempted = phase == "post"
    candidate = "absent" if mutation == "missing" else "unknown"
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"locus={leg}_{'before' if phase == 'pre' else 'after'}|"
            f"mode={mode}|field={field}|mutation={mutation}|candidate={candidate}"
        )
        for mode in MEMBERSHIP_MODES
        for leg in ("membership", "device")
        for field in (
            "selection.unit_id",
            "selection.model_type",
            "membership.mode",
        )
    ]


def selection_crosswire_variants(phase: str) -> list[str]:
    attempted = phase == "post"
    variants = []
    phase_locus = "before" if phase == "pre" else "after"
    for selected in UNIT_SPECS:
        for mode in MEMBERSHIP_MODES:
            for leg_shape in ("membership", "device", "both"):
                for other in UNIT_SPECS:
                    if selected == other:
                        continue
                    for field in ("selection.unit_id", "selection.model_type"):
                        variants.append(
                            f"phase={phase}|attempted={str(attempted).lower()}|"
                            f"mode={mode}|locus=leg_selector_{phase_locus}|"
                            f"selected={selected['unit_id']}:{selected['model_type']}|"
                            f"leg_shape={leg_shape}|field={field}|"
                            f"candidate={other['unit_id']}:{other['model_type']}|"
                            "mutation=valid_other"
                        )
                other_mode = next(item for item in MEMBERSHIP_MODES if item != mode)
                variants.append(
                    f"phase={phase}|attempted={str(attempted).lower()}|"
                    f"mode={mode}|locus=leg_selector_{phase_locus}|"
                    f"selected={selected['unit_id']}:{selected['model_type']}|"
                    f"leg_shape={leg_shape}|field=membership.mode|"
                    f"candidate={other_mode}|mutation=valid_other"
                )
    return variants


def membership_shape_variants(phase: str) -> list[str]:
    attempted = phase == "post"
    mutations = (
        MEMBERSHIP_SHAPE_MUTATIONS
        if phase == "pre"
        else POST_MEMBERSHIP_SHAPE_MUTATIONS
    )
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"mode={mode}|"
            f"locus=membership_{'before' if phase == 'pre' else 'after'}|"
            f"shape={shape}|"
            f"record_mutation={mutations[shape]}"
        )
        for mode in MEMBERSHIP_MODES
        for shape in MEMBERSHIP_SHAPES_BY_PHASE[phase]
    ]


def ownership_variants(phase: str) -> list[str]:
    attempted = phase == "post"
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"mode={mode}|"
            f"locus=membership_{'before' if phase == 'pre' else 'after'}|"
            f"ownership_mutation={shape}"
        )
        for mode in MEMBERSHIP_MODES
        for shape in OWNERSHIP_SHAPES
    ]


def common_token_locus_variants(loci: list[str], mutation: str) -> list[str]:
    phase = "pre" if loci == COMMON_PRE_LOCI else "post"
    attempted = phase == "post"
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"mode={mode}|locus={locus}|field={field}|mutation={mutation}|"
            f"candidate={'absent' if mutation == 'missing' else '0'}"
        )
        for mode in MEMBERSHIP_MODES
        for locus in loci
        for field in COMMON_CAUSAL_TOKEN_FIELDS
    ]


def common_token_anchor_mismatch_variants() -> list[str]:
    return [
        (
            f"phase=pre|attempted=false|mode={mode}|locus=prepared_authority|"
            f"field={field}|candidate=valid_other"
        )
        for mode in MEMBERSHIP_MODES
        for field in COMMON_CAUSAL_TOKEN_FIELDS
    ]


def common_token_crossleg_variants(loci: list[str]) -> list[str]:
    phase = "pre" if loci == COMMON_PRE_LOCI[1:] else "post"
    attempted = phase == "post"
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|mode={mode}|"
            f"locus=crossleg_binding|anchor=prepared_authority|"
            f"candidate={locus}|field={field}"
        )
        for mode in MEMBERSHIP_MODES
        for locus in loci
        for field in COMMON_CAUSAL_TOKEN_FIELDS
    ]


def semantic_target_field_variants(locus: str, mutation: str) -> list[str]:
    variants = []
    phase = "pre" if locus == "action_target" else "post"
    attempted = phase == "post"
    for mode in MEMBERSHIP_MODES:
        fields = [*TARGET_NONCOMMON_FIELDS, *MODE_TARGET_FIELDS[mode]]
        if mutation == "mismatch":
            fields = [field for field in fields if field != "operation.behavioral_leaf"]
        for field in fields:
            candidate = {
                "missing": "absent",
                "zero": "0",
                "mismatch": (
                    "unknown" if field in TARGET_NONCOMMON_FIELDS else "valid_other"
                ),
            }[mutation]
            variants.append(
                f"phase={phase}|attempted={str(attempted).lower()}|"
                f"locus={locus}|mode={mode}|field={field}|candidate={candidate}"
            )
    return variants


def selector_component(spec: dict, field: str) -> str:
    key = "unit_id" if field == "selection.unit_id" else "model_type"
    return spec[key]


def target_crosswire_variants(locus: str) -> list[str]:
    phase = "pre" if locus == "action_target" else "post"
    attempted = phase == "post"
    prefix = f"phase={phase}|attempted={str(attempted).lower()}|locus={locus}"
    unit_model_crosswires = [
        (
            f"{prefix}|kind=unit_or_model|mode={mode}|"
            f"selected={selected['unit_id']}:{selected['model_type']}|"
            f"field={field}|mutation=valid_other|"
            f"candidate={selector_component(other, field)}"
        )
        for mode in MEMBERSHIP_MODES
        for selected in UNIT_SPECS
        for other in UNIT_SPECS
        if selected["unit_id"] != other["unit_id"]
        for field in ("selection.unit_id", "selection.model_type")
    ]
    leaf_crosswires = [
        (f"{prefix}|kind=operation_leaf|mode={mode}|" f"candidate={leaf}")
        for mode in MEMBERSHIP_MODES
        for leaf in NON_BEHAVIORAL_OPERATION_LEAVES
    ]
    mode_crosswires = [
        (f"{prefix}|kind=mode|mode={mode}|" f"candidate={other_mode}")
        for mode in MEMBERSHIP_MODES
        for other_mode in MEMBERSHIP_MODES
        if mode != other_mode
    ]
    return [*unit_model_crosswires, *leaf_crosswires, *mode_crosswires]


def mode_pre_identity_variants(mutation: str) -> list[str]:
    candidate = {"missing": "absent", "zero": "0", "mismatch": "valid_other"}[mutation]
    return [
        (
            f"phase=pre|attempted=false|mode={mode}|locus={locus}|"
            f"field={field}|mutation={mutation}|candidate={candidate}"
        )
        for mode in MEMBERSHIP_MODES
        for locus in ("prepared_authority", "membership_before")
        for field in MODE_PRE_IDENTITY_FIELDS[mode]
    ]


def device_specific_identity_variants(phase: str, mutation: str) -> list[str]:
    candidate = {"missing": "absent", "zero": "0", "mismatch": "valid_other"}[mutation]
    variants = []
    for mode in MEMBERSHIP_MODES:
        loci = (
            [
                ("prepared_authority", ["device.claim_identity"]),
                ("device_before", DEVICE_IDENTITY_FIELDS_BY_MODE[mode]),
            ]
            if phase == "pre"
            else [("device_after", DEVICE_IDENTITY_FIELDS_BY_MODE[mode])]
        )
        for locus, fields in loci:
            variants.extend(
                (
                    f"phase={phase}|attempted={str(phase == 'post').lower()}|"
                    f"mode={mode}|locus={locus}|field={field}|mutation={mutation}|"
                    f"candidate={candidate}"
                )
                for field in fields
            )
    return variants


def mode_post_identity_variants(mode: str, mutation: str) -> list[str]:
    fields = (
        DIRECT_POST_IDENTITY_FIELDS
        if mode == "direct_process"
        else MANAGED_POST_IDENTITY_FIELDS
    )
    state = "membership_empty" if mode == "direct_process" else "service_stopped"
    candidate = {"missing": "absent", "zero": "0", "mismatch": "valid_other"}[mutation]
    return [
        (
            f"phase=post|attempted=true|mode={mode}|"
            f"locus=membership_after|field={field}|"
            f"mutation={mutation}|candidate={candidate}|state={state}"
        )
        for field in fields
    ]


def action_state_category(request: str, attempted: bool, ack: str) -> str:
    if request == "accepted" and attempted and ack == "valid":
        return "verified_release"
    if request == "absent" and not attempted and ack == "absent":
        return "pre_dispatch_clean"
    if request in {"rejected", "accepted"} and not attempted and ack == "absent":
        return "no_action_intact"
    return "contradiction"


def action_state_variants(category: str) -> list[str]:
    variants = []
    for mode in MEMBERSHIP_MODES:
        for request in ACTION_REQUEST_STATES:
            for attempted in (False, True):
                for ack in ACTION_ACK_STATES:
                    if action_state_category(request, attempted, ack) != category:
                        continue
                    released = attempted
                    post_state = "verified_released" if released else "unchanged_intact"
                    phase = "pre" if category == "pre_dispatch_clean" else "post"
                    variants.append(
                        f"phase={phase}|"
                        f"mode={mode}|request={request}|"
                        f"attempted={str(attempted).lower()}|"
                        f"locus=termination_action|ack={ack}|"
                        f"effect_calls={int(attempted)}|"
                        f"post_membership={post_state}|post_device={post_state}"
                    )
    return variants


def no_action_changed_leg_variants() -> list[str]:
    variants = []
    for mode in MEMBERSHIP_MODES:
        for request in ACTION_REQUEST_STATES:
            for leg in ACTION_CHANGED_LEGS:
                membership = (
                    "changed_unproven"
                    if leg in {"membership", "both"}
                    else "unchanged_intact"
                )
                device = (
                    "changed_unproven"
                    if leg in {"device", "both"}
                    else "unchanged_intact"
                )
                variants.append(
                    f"phase=post|mode={mode}|request={request}|attempted=false|"
                    f"ack=absent|effect_calls=0|locus=post_state|"
                    f"post_membership={membership}|post_device={device}|"
                    f"changed_leg={leg}"
                )
    return variants


def device_target_key_variants(phase: str) -> list[str]:
    mutations = {
        "duplicate_target_key": (
            "exact_count=2,candidate_count=2,"
            "records=two_identical_exact_target_records,unrelated_claim_count=1"
        ),
        "ambiguous_target_key": (
            "exact_count=1,candidate_count=2,"
            "records=one_exact_target_record_plus_one_wrong_generation_candidate,"
            "unrelated_claim_count=1"
        ),
        "candidate_wrong_generation": (
            "exact_count=0,candidate_count=1,"
            "records=one_wrong_generation_candidate,unrelated_claim_count=1"
        ),
        "unrelated_claim_identity_missing": (
            f"exact_count={1 if phase == 'pre' else 0},"
            f"candidate_count={1 if phase == 'pre' else 0},"
            "records=baseline_target_records_plus_one_unrelated_malformed_record,"
            "malformed_record_field=device.claim_identity,malformed_value=absent,"
            "unrelated_claim_count=1"
        ),
        "unrelated_claim_identity_zero": (
            f"exact_count={1 if phase == 'pre' else 0},"
            f"candidate_count={1 if phase == 'pre' else 0},"
            "records=baseline_target_records_plus_one_unrelated_malformed_record,"
            "malformed_record_field=device.claim_identity,malformed_value=0,"
            "unrelated_claim_count=1"
        ),
        "unrelated_claim_generation_missing": (
            f"exact_count={1 if phase == 'pre' else 0},"
            f"candidate_count={1 if phase == 'pre' else 0},"
            "records=baseline_target_records_plus_one_unrelated_malformed_record,"
            "malformed_record_field=device.claim_generation,malformed_value=absent,"
            "unrelated_claim_count=1"
        ),
        "unrelated_claim_generation_zero": (
            f"exact_count={1 if phase == 'pre' else 0},"
            f"candidate_count={1 if phase == 'pre' else 0},"
            "records=baseline_target_records_plus_one_unrelated_malformed_record,"
            "malformed_record_field=device.claim_generation,malformed_value=0,"
            "unrelated_claim_count=1"
        ),
    }
    return [
        (
            f"phase={phase}|attempted={str(phase == 'post').lower()}|"
            f"mode={mode}|"
            f"locus=device_{'before' if phase == 'pre' else 'after'}|shape={shape}|"
            f"claim_set_mutation={mutations[shape]}"
        )
        for mode in MEMBERSHIP_MODES
        for shape in DEVICE_TARGET_KEY_SHAPES_BY_PHASE[phase]
    ]


def service_start_generation_variants(mutation: str) -> list[str]:
    value = {"missing": "missing", "zero": "0", "mismatch": "14"}[mutation]
    return [
        (
            f"phase=pre|attempted=false|mode=managed_service|locus={locus}|"
            f"field=managed.service_start_generation|mutation={mutation}|"
            f"candidate={value}|prepared_exact=13"
        )
        for locus in ("prepared_authority", "membership_before")
    ]


def device_claim_generation_variants(phase: str, mutation: str) -> list[str]:
    loci = (
        ["prepared_authority", "device_before"]
        if phase == "before"
        else ["device_after"]
    )
    canonical_phase = "pre" if phase == "before" else "post"
    value = {"missing": "missing", "zero": "0", "mismatch": "24"}[mutation]
    return [
        (
            f"phase={canonical_phase}|attempted={str(phase == 'after').lower()}|"
            f"mode={mode}|locus={locus}|field=device.claim_generation|"
            f"mutation={mutation}|candidate={value}|prepared_exact=23"
        )
        for mode in MEMBERSHIP_MODES
        for locus in loci
    ]


def release_proof_presence_variants(proof: str) -> list[str]:
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            f"locus=release_composition|proof={proof}|mutation=missing|"
            "proof_presence_context=distinct_derived_types"
        )
        for mode in MEMBERSHIP_MODES
    ]


def release_proof_selector_splice_variant(
    mode: str, selected: dict, other: dict, field: str, mutated_proof: str
) -> str:
    membership = dict(selected)
    device = dict(selected)
    component = "unit_id" if field == "selection.unit_id" else "model_type"
    mutated = membership if mutated_proof == "membership" else device
    mutated[component] = other[component]
    return (
        "phase=post|request=accepted|attempted=true|ack=valid|"
        "effect_calls=1|post_membership=verified_released|"
        f"post_device=verified_released|mode={mode}|"
        "locus=release_composition|"
        f"selected={selected['unit_id']}:{selected['model_type']}|"
        f"field={field}|membership_proof_unit_id={membership['unit_id']}|"
        f"membership_proof_model_type={membership['model_type']}|"
        f"device_proof_unit_id={device['unit_id']}|"
        f"device_proof_model_type={device['model_type']}|"
        f"mutated_proof={mutated_proof}|"
        "mutation=valid_other_selector_component_splice"
    )


def release_proof_unit_model_splice_variants() -> list[str]:
    return [
        release_proof_selector_splice_variant(
            mode, selected, other, field, mutated_proof
        )
        for mode in MEMBERSHIP_MODES
        for selected in UNIT_SPECS
        for other in UNIT_SPECS
        if selected != other
        for field in ("selection.unit_id", "selection.model_type")
        for mutated_proof in ("membership", "device")
    ]


def release_proof_mode_splice_variants() -> list[str]:
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            f"locus=release_composition|"
            f"membership_proof_mode="
            f"{other_mode if mutated_proof == 'membership' else mode}|"
            f"device_proof_mode="
            f"{other_mode if mutated_proof == 'device' else mode}|"
            f"mutated_proof={mutated_proof}|mutation=valid_other_mode_splice"
        )
        for mode in MEMBERSHIP_MODES
        for other_mode in MEMBERSHIP_MODES
        if mode != other_mode
        for mutated_proof in ("membership", "device")
    ]


def proof_operation_value(mutated_proof: str, proof: str, other: str) -> str:
    return other if mutated_proof == proof else REC_OPERATION_LEAVES[0]


def release_proof_operation_splice_variants() -> list[str]:
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            "locus=release_composition|membership_proof_operation="
            f"{proof_operation_value(mutated_proof, 'membership', other_operation)}|"
            "device_proof_operation="
            f"{proof_operation_value(mutated_proof, 'device', other_operation)}|"
            f"mutated_proof={mutated_proof}|"
            "mutation=valid_other_operation_splice"
        )
        for mode in MEMBERSHIP_MODES
        for other_operation in NON_BEHAVIORAL_OPERATION_LEAVES
        for mutated_proof in ("membership", "device")
    ]


def release_proof_owner_variants() -> list[str]:
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            f"locus=release_composition|field={field}|"
            f"membership_proof_owner="
            f"{'valid_other' if mutated_proof == 'membership' else 'prepared_exact'}|"
            f"device_proof_owner="
            f"{'valid_other' if mutated_proof == 'device' else 'prepared_exact'}|"
            f"mutated_proof={mutated_proof}|mutation=owner_mismatch"
        )
        for mode in MEMBERSHIP_MODES
        for field in (
            MODE_PRE_IDENTITY_FIELDS[mode]
            + (
                ["managed.service_start_generation"]
                if mode == "managed_service"
                else []
            )
        )
        for mutated_proof in ("membership", "device")
    ]


def release_proof_common_token_variants() -> list[str]:
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            f"locus=release_composition|field={field}|"
            f"membership_proof_token="
            f"{'valid_other' if mutated_proof == 'membership' else 'prepared_exact'}|"
            f"device_proof_token="
            f"{'valid_other' if mutated_proof == 'device' else 'prepared_exact'}|"
            f"mutated_proof={mutated_proof}|mutation=common_token_mismatch"
        )
        for mode in MEMBERSHIP_MODES
        for field in COMMON_CAUSAL_TOKEN_FIELDS
        for mutated_proof in ("membership", "device")
    ]


def release_device_claim_tuple_variants(mutation: str) -> list[str]:
    candidate = {
        "missing": "absent",
        "zero": "0",
        "present_nonzero_nonexact": "present_nonzero_nonexact",
    }[mutation]
    return [
        (
            f"phase=post|request=accepted|attempted=true|ack=valid|"
            f"effect_calls=1|post_membership=verified_released|"
            f"post_device=verified_released|mode={mode}|"
            f"locus=release_device_proof|field={field}|mutation={mutation}|"
            f"candidate={candidate}"
        )
        for mode in MEMBERSHIP_MODES
        for field in (
            "device.claim_identity",
            "device.claim_generation",
        )
    ]


def early_release_credit_variants(action_state: str) -> list[str]:
    attempted = action_state == "attempted"
    ack_states = ["absent", "valid"] if attempted else ["absent"]
    return [
        (
            f"phase={'post' if attempted else 'pre'}|"
            f"request={'accepted' if attempted else 'absent'}|"
            f"attempted={str(attempted).lower()}|ack={ack}|"
            f"effect_calls={int(attempted)}|mode={mode}|"
            "locus=release_composition|credit_stage=before_both_proofs"
        )
        for mode in MEMBERSHIP_MODES
        for ack in ack_states
    ]


def evidence_liveness_expiry_variants(phase: str) -> list[str]:
    attempted = phase == "post"
    requests = (
        ["accepted"]
        if attempted
        else (["accepted", "rejected"] if phase == "no_action" else ["absent"])
    )
    return [
        (
            f"phase={'post' if phase == 'no_action' else phase}|"
            f"mode={mode}|request={request}|attempted={str(attempted).lower()}|"
            f"locus=evidence_lease_{phase}|"
            f"ack={'valid' if attempted else 'absent'}|"
            f"effect_calls={int(attempted)}|"
            "post_membership="
            f"{'verified_released' if attempted else 'unchanged_intact'}|"
            f"post_device={'verified_released' if attempted else 'unchanged_intact'}|"
            "mutation=evidence_liveness_expired"
        )
        for mode in MEMBERSHIP_MODES
        for request in requests
    ]


def exact_aggregate_negative_variants(key: str):
    return {
        "negative.each_prepared_selection_missing": prepared_selection_variants(
            "missing"
        ),
        "negative.each_prepared_selection_unknown": prepared_selection_variants(
            "unknown"
        ),
        "negative.each_unit_model_crosswire": unit_model_crosswire_variants(),
        "negative.operation_leaf_missing": operation_leaf_missing_variants(),
        "negative.operation_leaf_mismatch": operation_leaf_variants(),
        "negative.each_membership_shape_pre": membership_shape_variants("pre"),
        "negative.each_membership_shape_post": membership_shape_variants("post"),
        "negative.each_ownership_pre_invalid": ownership_variants("pre"),
        "negative.each_ownership_post_invalid": ownership_variants("post"),
        "negative.each_leg_selector_pre_missing": leg_selector_scalar_variants(
            "pre", "missing"
        ),
        "negative.each_leg_selector_pre_unknown": leg_selector_scalar_variants(
            "pre", "unknown"
        ),
        "negative.each_selection_leg_crosswire_pre": selection_crosswire_variants(
            "pre"
        ),
        "negative.each_leg_selector_post_missing": leg_selector_scalar_variants(
            "post", "missing"
        ),
        "negative.each_leg_selector_post_unknown": leg_selector_scalar_variants(
            "post", "unknown"
        ),
        "negative.each_selection_leg_crosswire_post": selection_crosswire_variants(
            "post"
        ),
        "negative.each_common_token_pre_anchor_mismatch": (
            common_token_anchor_mismatch_variants()
        ),
        "negative.each_common_token_pre_crossleg_mismatch": (
            common_token_crossleg_variants(COMMON_PRE_LOCI[1:])
        ),
        "negative.each_common_token_post_crossleg_mismatch": (
            common_token_crossleg_variants(COMMON_POST_LOCI)
        ),
        "negative.each_action_target_crosswire": target_crosswire_variants(
            "action_target"
        ),
        "negative.termination_ack_target_crosswire": target_crosswire_variants(
            "ack_target"
        ),
        "negative.each_action_state_pre_dispatch_clean": action_state_variants(
            "pre_dispatch_clean"
        ),
        "negative.each_action_state_no_action_intact": action_state_variants(
            "no_action_intact"
        ),
        "negative.each_action_state_contradiction": action_state_variants(
            "contradiction"
        ),
        "negative.each_no_action_changed_leg": no_action_changed_leg_variants(),
        "negative.each_device_target_key_pre_invalid": device_target_key_variants(
            "pre"
        ),
        "negative.each_device_target_key_post_invalid": device_target_key_variants(
            "post"
        ),
        "negative.each_evidence_liveness_expired_pre": (
            evidence_liveness_expiry_variants("pre")
        ),
        "negative.each_evidence_liveness_expired_post": (
            evidence_liveness_expiry_variants("post")
        ),
        "negative.each_evidence_liveness_expired_no_action": (
            evidence_liveness_expiry_variants("no_action")
        ),
        "negative.release_credit_before_both_proofs_no_action": (
            early_release_credit_variants("no_action")
        ),
        "negative.release_credit_before_both_proofs_attempted": (
            early_release_credit_variants("attempted")
        ),
        "negative.release_membership_proof_missing": (
            release_proof_presence_variants("membership")
        ),
        "negative.release_device_proof_missing": (
            release_proof_presence_variants("device")
        ),
        "negative.each_release_proof_unit_model_splice": (
            release_proof_unit_model_splice_variants()
        ),
        "negative.each_release_proof_mode_splice": (
            release_proof_mode_splice_variants()
        ),
        "negative.each_release_proof_operation_splice": (
            release_proof_operation_splice_variants()
        ),
        "negative.each_release_proof_owner_mismatch": release_proof_owner_variants(),
        "negative.each_release_proof_common_token_mismatch": (
            release_proof_common_token_variants()
        ),
    }.get(key)


def structured_aggregate_negative_variants(key: str):
    builders = {
        **{
            f"negative.each_common_token_pre_{mutation}": (
                lambda mutation=mutation: common_token_locus_variants(
                    COMMON_PRE_LOCI, mutation
                )
            )
            for mutation in ("missing", "zero")
        },
        **{
            f"negative.each_common_token_post_{mutation}": (
                lambda mutation=mutation: common_token_locus_variants(
                    COMMON_POST_LOCI, mutation
                )
            )
            for mutation in ("missing", "zero")
        },
        **{
            f"negative.each_action_target_{mutation}": (
                lambda mutation=mutation: semantic_target_field_variants(
                    "action_target", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_mode_specific_pre_identity_{mutation}": (
                lambda mutation=mutation: mode_pre_identity_variants(mutation)
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_device_specific_pre_identity_{mutation}": (
                lambda mutation=mutation: device_specific_identity_variants(
                    "pre", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_device_specific_post_identity_{mutation}": (
                lambda mutation=mutation: device_specific_identity_variants(
                    "post", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_direct_mode_specific_post_identity_{mutation}": (
                lambda mutation=mutation: mode_post_identity_variants(
                    "direct_process", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_managed_mode_specific_post_identity_{mutation}": (
                lambda mutation=mutation: mode_post_identity_variants(
                    "managed_service", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.termination_ack_target_{mutation}": (
                lambda mutation=mutation: semantic_target_field_variants(
                    "ack_target", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.managed_service_start_generation_{mutation}": (
                lambda mutation=mutation: service_start_generation_variants(mutation)
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.device_claim_generation_before_{mutation}": (
                lambda mutation=mutation: device_claim_generation_variants(
                    "before", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.device_claim_generation_after_{mutation}": (
                lambda mutation=mutation: device_claim_generation_variants(
                    "after", mutation
                )
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        **{
            f"negative.each_release_device_claim_tuple_{mutation}": (
                lambda mutation=mutation: release_device_claim_tuple_variants(mutation)
            )
            for mutation in ("missing", "zero", "present_nonzero_nonexact")
        },
    }
    builder = builders.get(key)
    return None if builder is None else builder()


def precondition_fault_variants() -> list[str]:
    variants = []
    for key in PRECONDITION_FAULT_KEYS:
        require(
            key in {row.split("=", 1)[0] for row in NEGATIVE_ROWS}, f"{key} missing"
        )
        for variant in negative_variants(key):
            require("mode=" in variant, f"{key} variant lacks an exact mode")
            variants.append(variant)
    require(len(variants) == len(set(variants)), "precondition faults are not unique")
    return variants


def without_action_state(descriptor: str) -> str:
    action_fields = {
        "ack",
        "attempted",
        "effect_calls",
        "phase",
        "post_device",
        "post_membership",
        "request",
    }
    return "|".join(
        part
        for part in descriptor.split("|")
        if part.split("=", 1)[0] not in action_fields
    )


def precondition_action_variants(action_state: str) -> list[str]:
    attempted = action_state == "attempted"
    require(action_state in {"attempted", "ack_only"}, "action state changed")
    ack_states = ["absent", "valid"] if attempted else ["valid"]
    post_state = "verified_released" if attempted else "unchanged_intact"
    return [
        (
            f"phase=post|request={request}|attempted={str(attempted).lower()}|"
            f"ack={ack}|effect_calls={int(attempted)}|"
            f"post_membership={post_state}|post_device={post_state}|"
            f"action_state={action_state}|precondition_fault=true|"
            f"{without_action_state(fault)}"
        )
        for fault in precondition_fault_variants()
        for request in ACTION_REQUEST_STATES
        for ack in ack_states
    ]


def aggregate_negative_variants(key: str):
    if key.startswith("negative.each_precondition_fault_"):
        return precondition_action_variants(
            key.removeprefix("negative.each_precondition_fault_")
        )
    exact = exact_aggregate_negative_variants(key)
    if exact is not None:
        return exact
    return structured_aggregate_negative_variants(key)


def mode_phase_variants(phase: str, locus: str, mutation: str) -> list[str]:
    attempted = phase == "post"
    generation_values = {
        "direct_process": (7, 8),
        "managed_service": (11, 12),
    }

    def mutation_state(mode: str) -> str:
        membership_before, membership_after = generation_values[mode]
        states = {
            "membership_before_missing": "membership_envelope=absent",
            "membership_before_stale": "membership_fresh=false",
            "membership_before_incomplete": "membership_complete=false",
            "membership_before_unhealthy": "membership_healthy=false",
            "membership_after_missing": "membership_envelope=absent",
            "membership_after_stale": "membership_fresh=false",
            "membership_after_incomplete": "membership_complete=false",
            "membership_after_unhealthy": "membership_healthy=false",
            "membership_after_nonempty": "member_record_count=1",
            "membership_generation_before_missing": "generation_before=missing",
            "membership_generation_before_zero": "generation_before=0",
            "membership_generation_before_unchecked": (
                f"generation_before={membership_before},checked=false"
            ),
            "membership_generation_after_missing": "generation_after=missing",
            "membership_generation_after_zero": "generation_after=0",
            "membership_generation_replay": (
                f"generation_before={membership_before},generation_after={membership_before}"
            ),
            "membership_generation_regressed": (
                f"generation_before={membership_before},"
                f"generation_after={membership_before - 1}"
            ),
            "membership_generation_skipped": (
                f"generation_before={membership_before},"
                f"generation_after={membership_after + 1}"
            ),
            "membership_generation_overflow_to_zero": (
                "generation_before=uint64_max,generation_after=0"
            ),
            "membership_generation_after_unchecked": (
                f"generation_before={membership_before},generation_after={membership_after},"
                "checked=false"
            ),
            "device_generation_before_missing": "generation_before=missing",
            "device_generation_before_zero": "generation_before=0",
            "device_generation_before_unchecked": "generation_before=17,checked=false",
            "device_generation_after_missing": "generation_after=missing",
            "device_generation_after_zero": "generation_after=0",
            "device_generation_replay": "generation_before=17,generation_after=17",
            "device_generation_regressed": "generation_before=17,generation_after=16",
            "device_generation_skipped": "generation_before=17,generation_after=19",
            "device_generation_overflow_to_zero": (
                "generation_before=uint64_max,generation_after=0"
            ),
            "device_generation_after_unchecked": (
                "generation_before=17,generation_after=18,checked=false"
            ),
            "device_before_claim_absent": (
                "exact_count=0,candidate_count=0,records=no_target_candidates,"
                "unrelated_claim_count=1"
            ),
            "device_after_claim_present": (
                "exact_count=1,candidate_count=1,records=one_exact_target_record,"
                "unrelated_claim_count=1"
            ),
            "device_before_missing": "device_envelope=absent",
            "device_before_stale": "device_fresh=false",
            "device_before_unhealthy": "device_healthy=false",
            "device_before_incomplete": "device_complete=false",
            "device_after_missing": "device_envelope=absent",
            "device_after_stale": "device_fresh=false",
            "device_after_unhealthy": "device_healthy=false",
            "device_after_incomplete": "device_complete=false",
            "termination_ack_action_token_missing": "ack_action_token=absent",
            "termination_ack_action_token_zero": "ack_action_token=0",
            "termination_ack_action_token_mismatch": ("ack_action_token=valid_other"),
        }
        return states.get(mutation, f"typed_state={mutation}")

    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"mode={mode}|locus={locus}|mutation={mutation}|"
            f"mutation_state={mutation_state(mode)}"
        )
        for mode in MEMBERSHIP_MODES
    ]


def managed_service_negative_variants(key: str) -> list[str]:
    phase = "post" if "_after_" in key else "pre"
    attempted = phase == "post"
    locus = "service_after" if attempted else "service_before"
    return [
        (
            f"phase={phase}|attempted={str(attempted).lower()}|"
            f"mode=managed_service|locus={locus}|"
            f"shape={key.removeprefix('negative.')}"
        )
    ]


def membership_negative_variants(key: str) -> list[str]:
    phase = "pre" if "before" in key else "post"
    locus = "membership_before" if phase == "pre" else "membership_after"
    return mode_phase_variants(phase, locus, key.removeprefix("negative."))


def device_negative_variants(key: str) -> list[str]:
    phase = "pre" if "before" in key else "post"
    locus = "device_before" if phase == "pre" else "device_after"
    return mode_phase_variants(phase, locus, key.removeprefix("negative."))


def negative_variants(key: str) -> list[str]:
    aggregate = aggregate_negative_variants(key)
    if aggregate is not None:
        return aggregate
    exact = {
        **{
            f"negative.termination_ack_action_token_{mutation}": mode_phase_variants(
                "post", "ack_action_token", f"termination_ack_action_token_{mutation}"
            )
            for mutation in ("missing", "zero", "mismatch")
        },
        "negative.profile_unknown": mode_phase_variants(
            "pre", "prepared_authority", "profile_unknown"
        ),
        "negative.prepared_authority_missing": mode_phase_variants(
            "pre", "prepared_authority", "prepared_authority_missing"
        ),
        "negative.direct_evidence_for_managed_profile": [
            (
                "phase=pre|attempted=false|selected_mode=managed_service|"
                "locus=prepared_authority|evidence_kind=direct_process"
            )
        ],
        "negative.managed_evidence_for_direct_profile": [
            (
                "phase=pre|attempted=false|selected_mode=direct_process|"
                "locus=prepared_authority|evidence_kind=managed_service"
            )
        ],
        "negative.managed_service_post_restart_generation": [
            (
                "phase=post|attempted=true|mode=managed_service|"
                "locus=membership_after|"
                "shape=state_running_and_start_generation_changed"
            )
        ],
        "negative.managed_service_state_after_running": [
            (
                "phase=post|attempted=true|mode=managed_service|"
                "locus=membership_after|"
                "shape=state_running_and_start_generation_unchanged"
            )
        ],
        "negative.direct_process_identity_reused": [
            (
                "phase=post|attempted=true|mode=direct_process|"
                "locus=membership_after|shape=member_present_same_pid_new_birth"
            )
        ],
    }
    direct = exact.get(key)
    if direct is not None:
        return direct
    prefix_builders = (
        ("negative.managed_service_", managed_service_negative_variants),
        ("negative.membership_", membership_negative_variants),
        ("negative.device_", device_negative_variants),
    )
    for prefix, builder in prefix_builders:
        if key.startswith(prefix):
            return builder(key)
    return ["case"]


def parse_input_descriptor(descriptor: str) -> dict[str, str]:
    fields = {}
    for part in descriptor.split("|"):
        require("=" in part, "negative input descriptor has an untyped component")
        key, value = part.split("=", 1)
        require(key and value, "negative input descriptor has an empty component")
        require(key not in fields, f"negative input descriptor repeats {key}")
        fields[key] = value
    return fields


def selected_specs_for_descriptor(fields: dict[str, str]) -> list[dict]:
    selected = fields.pop("selected", None)
    unit_id = fields.pop("unit_id", None)
    model_type = fields.pop("model_type", None)
    if unit_id is not None or model_type is not None:
        require(
            unit_id is not None and model_type is not None and selected is None,
            "negative input has an ambiguous prepared selector",
        )
        fields["selector_mutation"] = "malformed_unit_model_pair"
        return [{"unit_id": unit_id, "model_type": model_type}]
    if selected is None:
        return UNIT_SPECS
    if ":" in selected:
        selected_unit, selected_model_type = selected.split(":", 1)
        require(
            any(
                spec["unit_id"] == selected_unit
                and spec["model_type"] == selected_model_type
                for spec in UNIT_SPECS
            ),
            "negative input names an unknown selected unit/model pair",
        )
        return [{"unit_id": selected_unit, "model_type": selected_model_type}]
    matches = [spec for spec in UNIT_SPECS if spec["model_type"] == selected]
    require(len(matches) == 1, "negative input names an unknown selected model type")
    return matches


def composition_proof_slots(locus: str, fields: dict[str, str]) -> tuple[str, str]:
    membership_slot = "present"
    device_slot = "present"
    if locus == "release_composition" and fields.get("mutation") == "missing":
        if fields.get("proof") == "membership":
            membership_slot = "missing"
        elif fields.get("proof") == "device":
            device_slot = "missing"
    return membership_slot, device_slot


def canonical_input_descriptors(descriptor: str) -> list[str]:
    fields = parse_input_descriptor(descriptor)
    if "selected_mode" in fields:
        require("mode" not in fields, "negative input has two mode authorities")
        fields["mode"] = fields.pop("selected_mode")
    phase = fields.pop("phase")
    mode = fields.pop("mode")
    attempted = fields.pop("attempted")
    locus = fields.pop("locus")
    require(phase in {"pre", "post"}, "negative input phase is not typed")
    require(mode in MEMBERSHIP_MODES, "negative input mode is not typed")
    require(attempted in {"true", "false"}, "negative attempted state is not typed")
    request = fields.pop("request", "accepted" if attempted == "true" else "absent")
    ack = fields.pop("ack", "valid" if attempted == "true" else "absent")
    effect_calls = fields.pop("effect_calls", "1" if attempted == "true" else "0")
    credit_stage = fields.pop("credit_stage", "after_both_proofs")
    require(request in ACTION_REQUEST_STATES, "negative request state is not typed")
    require(ack in ACTION_ACK_STATES, "negative ACK state is not typed")
    require(effect_calls in {"0", "1"}, "negative effect count is not typed")
    require(
        credit_stage in {"before_both_proofs", "after_both_proofs"},
        "negative credit stage is not typed",
    )
    require(
        effect_calls == ("1" if attempted == "true" else "0"),
        "negative effect count differs from attempted",
    )
    default_post = "verified_released" if attempted == "true" else "unchanged_intact"
    post_membership = fields.pop("post_membership", default_post)
    post_device = fields.pop("post_device", default_post)
    if fields.get("operation_leaf") is not None:
        require("candidate_operation_leaf" not in fields, "operation candidate repeats")
        fields["candidate_operation_leaf"] = fields.pop("operation_leaf")
    composition_locus = locus in {"release_composition", "release_device_proof"}
    input_path = (
        "proof_composition"
        if composition_locus and credit_stage == "after_both_proofs"
        else "raw_verification"
    )
    if (
        attempted == "false"
        and ack == "absent"
        and post_membership == post_device == "unchanged_intact"
    ):
        baseline = "intact_no_action_v1"
    elif attempted == "true" and post_membership == post_device == "verified_released":
        baseline = "released_after_attempt_v1"
    else:
        baseline = "explicit_action_or_post_ambiguity_v1"
    if input_path == "proof_composition":
        baseline = "validated_attempted_and_acknowledged_v1"
    shared = [
        f"baseline={baseline}",
        "prepared_profile=known_flm_system_managed",
        "prepared_authority=single_structural_input",
        "typed_selector_authority=single_present_semantic_field_per_component",
    ]
    mode_identity_domain = (
        "direct_controller_pid_birth_executable_only"
        if mode == "direct_process"
        else (
            "managed_controller_manager_service_identity_config_instance_birth_"
            "start_generation_serving_pid_birth_executable"
        )
    )
    membership_after_defaults = {
        "unchanged_intact": (
            "present_fresh_healthy_complete_one_exact_root_one_exact_member_"
            "declared_one"
        ),
        "verified_released": (
            "present_fresh_healthy_complete_one_exact_root_zero_members_declared_zero"
        ),
    }.get(post_membership, "explicit_fault_state")
    device_after_defaults = {
        "unchanged_intact": (
            "present_fresh_healthy_complete_exact_count_one_one_exact_record_"
            "plus_one_unrelated_out_of_scope"
        ),
        "verified_released": (
            "present_fresh_healthy_complete_exact_count_zero_no_target_candidates_"
            "plus_one_unrelated_out_of_scope"
        ),
    }.get(post_device, "explicit_fault_state")
    if mode == "direct_process":
        service_state = "not_applicable"
    elif post_membership == "unchanged_intact":
        service_state = "running_before_running_after_same_start_generation"
    elif post_membership == "verified_released":
        service_state = "running_before_stopped_after"
    else:
        service_state = "running_before_explicit_after_fault"
    if input_path == "proof_composition":
        require(
            phase == "post"
            and request == "accepted"
            and attempted == "true"
            and ack == "valid"
            and effect_calls == "1"
            and post_membership == post_device == "verified_released",
            "proof composition lacks a validated attempted-and-acknowledged context",
        )
        membership_slot, device_slot = composition_proof_slots(locus, fields)
        fixed = [
            *shared,
            "selected_operation=service_termination",
            f"mode_identity_domain={mode_identity_domain}",
            "expected_binding_defaults=unit_model_mode_operation_common14_owner",
            "selected_claim_defaults=present_nonzero_exact_identity_and_generation",
            "membership_proof_defaults=exact_shared_binding_and_owner",
            "device_proof_defaults=exact_shared_binding_owner_and_claim_tuple",
            f"path={input_path}",
            "proof_input_contract=expected_binding_and_typed_slots",
            "validated_action_context=attempted_and_acknowledged",
            "expected_proof_binding=prepared_authority",
            f"membership_proof_slot={membership_slot}",
            f"device_proof_slot={device_slot}",
            "proof_payload_validity=derived_independently_per_present_slot",
            f"mode={mode}",
            f"credit_stage={credit_stage}",
            f"locus={locus}",
        ]
    else:
        fixed = [
            *shared,
            "selected_operation=service_termination",
            f"mode_identity_domain={mode_identity_domain}",
            "common14_defaults=present_nonzero_exact_at_seven_loci",
            "mode_identity_defaults=present_nonzero_exact_and_matched",
            "membership_before_defaults=present_fresh_healthy_complete_owned",
            "membership_before_record_defaults=one_exact_root_one_exact_pid_birth_executable_member_declared_one",
            f"membership_after_defaults={membership_after_defaults}",
            "membership_generation_defaults=present_checked_uint64",
            "device_before_defaults=present_fresh_healthy_complete_exact_key_count_one_plus_one_unrelated_out_of_scope",
            f"device_after_defaults={device_after_defaults}",
            "device_claim_defaults=exact_device_claim_owner_and_generation",
            "device_generation_defaults=present_checked_uint64",
            "action_target_defaults=exact_semantic_tuple",
            f"service_state={service_state}",
            f"path={input_path}",
            "proof_input_contract=absent_and_proofs_derived_internally",
            "validated_action_context=not_supplied",
            "membership_proof_slot=not_supplied",
            "device_proof_slot=not_supplied",
            f"phase={phase}",
            f"mode={mode}",
            f"request={request}",
            f"attempted={attempted}",
            f"ack={ack}",
            f"effect_calls={effect_calls}",
            f"credit_stage={credit_stage}",
            f"post_membership={post_membership}",
            f"post_device={post_device}",
            f"locus={locus}",
        ]
    selected_specs = selected_specs_for_descriptor(fields)
    fault = [f"{key}={fields[key]}" for key in sorted(fields)]
    return [
        "|".join(
            [
                *fixed[:4],
                f"selected_unit_id={spec['unit_id']}",
                f"selected_model_type={spec['model_type']}",
                *fixed[4:],
                *fault,
            ]
        )
        for spec in selected_specs
    ]


def negative_case_entries() -> list[tuple[str, str]]:
    entries = []
    for row in NEGATIVE_ROWS:
        key = row.split("=", 1)[0]
        for variant in negative_variants(key):
            entries.extend(
                (key, descriptor) for descriptor in canonical_input_descriptors(variant)
            )
    descriptors = [descriptor for _, descriptor in entries]
    require(
        len(descriptors) == len(set(descriptors)),
        "canonical negative inputs are not value-wise unique",
    )
    return entries


def negative_case_manifest() -> list[str]:
    return [descriptor for _, descriptor in negative_case_entries()]


def negative_labeled_manifest() -> list[str]:
    return [
        f"fault_class={key}|input={descriptor}"
        for key, descriptor in negative_case_entries()
    ]


def require_manifest_input_contract(item: dict[str, str]) -> None:
    require("timing" not in item, "negative input retained a credit timing alias")
    require(
        item["credit_stage"] in {"before_both_proofs", "after_both_proofs"},
        "negative input credit stage changed",
    )
    if item["path"] == "raw_verification":
        require(
            item["proof_input_contract"] == "absent_and_proofs_derived_internally"
            and item["validated_action_context"] == "not_supplied"
            and item["membership_proof_slot"] == "not_supplied"
            and item["device_proof_slot"] == "not_supplied"
            and "proof_payload_validity" not in item
            and item["effect_calls"] == ("1" if item["attempted"] == "true" else "0"),
            "raw verification input contract changed",
        )
        return
    require(item["path"] == "proof_composition", "negative input path changed")
    require(
        not {
            "phase",
            "request",
            "attempted",
            "ack",
            "effect_calls",
            "post_membership",
            "post_device",
        }.intersection(item),
        "proof composition retained mutable raw-action evidence",
    )
    expected_membership, expected_device = composition_proof_slots(item["locus"], item)
    require(
        item["proof_input_contract"] == "expected_binding_and_typed_slots"
        and item["validated_action_context"] == "attempted_and_acknowledged"
        and item["expected_proof_binding"] == "prepared_authority"
        and item["membership_proof_slot"] == expected_membership
        and item["device_proof_slot"] == expected_device
        and item["proof_payload_validity"] == "derived_independently_per_present_slot"
        and item["credit_stage"] == "after_both_proofs",
        "proof composition input contract changed",
    )


def require_negative_case_manifest(rows: dict[str, str]) -> None:
    manifest = negative_case_manifest()
    labeled_manifest = negative_labeled_manifest()
    require(len(NEGATIVE_ROWS) == 132, "negative fault-class count changed")
    require(len(manifest) == NEGATIVE_VARIANT_COUNT, "negative variant count changed")
    parsed_manifest = [parse_input_descriptor(item) for item in manifest]
    require(
        {item["path"] for item in parsed_manifest}
        == {"raw_verification", "proof_composition"},
        "negative input-domain partition changed",
    )
    path_counts = {
        path: sum(item["path"] == path for item in parsed_manifest)
        for path in ("raw_verification", "proof_composition")
    }
    require(
        path_counts
        == {
            "raw_verification": RAW_NEGATIVE_VARIANT_COUNT,
            "proof_composition": PROOF_COMPOSITION_NEGATIVE_VARIANT_COUNT,
        },
        "negative input-domain cardinality changed",
    )
    for item in parsed_manifest:
        require_manifest_input_contract(item)
    entries = negative_case_entries()
    for key in {key for key, _ in entries}:
        selected_units = {
            parse_input_descriptor(item)["selected_unit_id"]
            for item_key, item in entries
            if item_key == key
        }
        require(selected_units == set(UNIT_IDS), f"{key} unit matrix is incomplete")
    require(
        canonical_json_sha256(manifest) == NEGATIVE_MANIFEST_SHA256,
        "negative case manifest changed",
    )
    require(
        canonical_json_sha256(labeled_manifest) == NEGATIVE_LABELED_MANIFEST_SHA256,
        "negative labeled manifest changed",
    )
    pair_count = len(manifest) * (len(manifest) - 1) // 2
    require(pair_count == NEGATIVE_UNORDERED_PAIR_COUNT, "pair count changed")
    require(
        rows["synthetic.negative_fault_class_count"] == str(len(NEGATIVE_ROWS))
        and rows["synthetic.negative_variant_count"] == str(len(manifest))
        and rows["synthetic.negative_case_manifest_sha256"] == NEGATIVE_MANIFEST_SHA256
        and rows["synthetic.negative_labeled_manifest_sha256"]
        == NEGATIVE_LABELED_MANIFEST_SHA256
        and rows["synthetic.negative_unordered_pair_count"] == str(pair_count)
        and rows["synthetic.negative_input_descriptor_unique"] == "passed"
        and rows["synthetic.negative_pairwise_unique"] == "passed"
        and rows["synthetic.negative_input_execution"] == "passed",
        "negative manifest observation changed",
    )
    require(
        rows["synthetic.input_domain_partition"]
        == "raw_verification_and_proof_composition",
        "negative input-domain observation changed",
    )


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def find_exact_row(rows, key: str, value: str, label: str) -> dict:
    require(type(rows) is list, f"{label} registry is unavailable")
    matches = [row for row in rows if type(row) is dict and row.get(key) == value]
    require(len(matches) == 1, f"{label} must contain {value} exactly once")
    return matches[0]


def expected_roots(spec: dict) -> dict:
    model_type = spec["model_type"]
    unit_id = spec["unit_id"]
    return {
        "implementation": (
            f"src/cpp/server/residency/later/windows_xdna2/flm_npu/" f"{model_type}/rec"
        ),
        "tests": (f"test/residency/later/windows_xdna2/flm_npu/{model_type}/rec"),
        "outputs": f"plan/evidence/later-promotion/{unit_id}",
    }


def require_unit_record(row: dict, spec: dict) -> None:
    selector = row.get("selector")
    require(type(selector) is dict, f"{spec['unit_id']} selector is unavailable")
    expected_selector = {
        "base_variant": "flm-npu",
        "platform": "windows-xdna2",
        "backend_channel": "single",
        "model_type": spec["model_type"],
        "operation_template": "REC",
        "operation_leaves": REC_OPERATION_LEAVES,
    }
    require(selector == expected_selector, f"{spec['unit_id']} selector changed")
    require(
        row.get("initial_state")
        == {"capability_level": "unsupported", "delivery_state": "absent"},
        f"{spec['unit_id']} initial state changed",
    )
    require(row.get("evidence_ceiling") == "modeled", "evidence ceiling changed")
    require(row.get("issue_id") == spec["issue_id"], "issue binding changed")
    require(row.get("material_profiles") == {}, "material profiles changed")
    require("runtime_bindings" not in row, "per-unit runtime bindings were introduced")
    require(
        row.get("constraints")
        == ["flm_type_slot", "npu_cross_family", "model_type_pool", "ownership"],
        "REC constraints changed",
    )
    require(row.get("recovery") == "flm_system_managed", "recovery profile changed")
    require(row.get("fallbacks") == FALLBACKS, "fallback mapping changed")
    require(
        row.get("delivery_gate") == f"release_verified:{spec['unit_id']}",
        "delivery gate changed",
    )
    require(row.get("evidence_gate_set") == spec["gate"], "evidence gate changed")
    require(
        row.get("compatibility_contracts") == ["H-NPU-FLM-CONFLICT-XDNA2-v1"],
        "compatibility closure changed",
    )
    require(row.get("expected_roots") == expected_roots(spec), "roots changed")
    require(
        canonical_json_sha256(row) == spec["record_sha256"],
        f"{spec['unit_id']} exact record changed",
    )


def require_generated_contracts(generated: dict, unit_rows: dict[str, dict]) -> None:
    rows = generated.get("promotion_units")
    require(type(rows) is list, "generated promotion units are unavailable")
    for spec in UNIT_SPECS:
        wrapper = find_exact_row(rows, "id", spec["unit_id"], "generated profiles")
        require(
            canonical_json_sha256(wrapper) == spec["wrapper_sha256"],
            f"{spec['unit_id']} generated wrapper changed",
        )
        require(
            wrapper.get("contract") == unit_rows[spec["unit_id"]],
            f"{spec['unit_id']} generated contract differs from inventory",
        )
        require(
            wrapper.get("unit_kind") == "later_runtime"
            and wrapper.get("capability_level") == "unsupported"
            and wrapper.get("delivery_state") == "absent",
            f"{spec['unit_id']} generated state changed",
        )


def require_inventory_contract(repo_root: Path) -> dict:
    inventory_path = repo_root / INVENTORY_PATH
    generated_path = repo_root / GENERATED_PROFILES_PATH
    require(file_sha256(inventory_path) == INVENTORY_SHA256, "inventory bytes changed")
    require(
        file_sha256(generated_path) == GENERATED_PROFILES_SHA256,
        "generated residency profile bytes changed",
    )
    inventory = parse_json_object(inventory_path.read_bytes(), INVENTORY_PATH)
    generated = parse_json_object(generated_path.read_bytes(), GENERATED_PROFILES_PATH)
    require(
        inventory.get("runtime_binding_kinds") == RUNTIME_BINDING_NAMES,
        "global runtime-binding vocabulary changed",
    )
    require(
        canonical_json_sha256(RUNTIME_BINDING_NAMES) == RUNTIME_BINDING_DIGEST,
        "global runtime-binding vocabulary digest changed",
    )
    roster = inventory.get("later_promotion_roster")
    unit_rows = {}
    for spec in UNIT_SPECS:
        row = find_exact_row(roster, "unit_id", spec["unit_id"], "later-promotion")
        require_unit_record(row, spec)
        unit_rows[spec["unit_id"]] = row
    require_generated_contracts(generated, unit_rows)
    require(
        inventory.get("fallback_registry", {}).get(FALLBACK_ID) == FALLBACK_DEFINITION,
        "fallback definition changed",
    )
    require(
        inventory.get("recovery_profiles", {}).get("flm_system_managed")
        == RECOVERY_PROFILE,
        "FLM recovery profile changed",
    )
    require(
        canonical_json_sha256(RECOVERY_PROFILE) == RECOVERY_PROFILE_DIGEST,
        "FLM recovery profile digest changed",
    )
    gate_sets = inventory.get("gate_sets")
    require(type(gate_sets) is dict, "gate-set registry is unavailable")
    for spec in UNIT_SPECS:
        require(gate_sets.get(spec["gate"]) == GATE_SET, "REC gate set changed")
    require(canonical_json_sha256(GATE_SET) == GATE_SET_DIGEST, "gate digest changed")
    backend_versions = parse_json_object(
        (repo_root / "src/cpp/resources/backend_versions.json").read_bytes(),
        "backend versions",
    )
    require(backend_versions.get("flm") == FLM_SELECTOR, "FLM selector changed")
    require(
        canonical_json_sha256(FLM_SELECTOR) == FLM_SELECTOR_DIGEST,
        "FLM selector digest changed",
    )
    bindings = [{"unit_id": unit_id, "fallbacks": FALLBACKS} for unit_id in UNIT_IDS]
    require(
        canonical_json_sha256(bindings) == FALLBACK_BINDING_LIST_DIGEST,
        "fallback binding-list digest changed",
    )
    return {
        "bindings": bindings,
        "fallback_ids": [FALLBACK_ID],
        "fallback_rows": [
            f"fallback_binding.{spec['model_type']}={FALLBACK_ID}"
            for spec in UNIT_SPECS
        ],
    }


def require_live_source_audit(repo_root: Path) -> None:
    for relative_path, expected_digest in TASK_BASE_LIVE_SOURCE_SHA256.items():
        path = repo_root / relative_path
        require(path.is_file(), f"TASK_BASE source is unavailable: {relative_path}")
        require(
            file_sha256(path) == expected_digest,
            f"TASK_BASE source changed: {relative_path}",
        )


def expected_output_rows(platform_id: str) -> list[str]:
    return [
        (
            f"platform.current={platform_id}"
            if row.startswith("platform.current=")
            else row
        )
        for row in EXPECTED_LINUX_ROWS
    ]


def rows_with_prefixes(rows: list[str], prefixes: tuple[str, ...]) -> list[str]:
    return [row for row in rows if row.split("=", 1)[0].startswith(prefixes)]


def rows_with_keys(rows: list[str], keys: set[str]) -> list[str]:
    selected = [row for row in rows if row.split("=", 1)[0] in keys]
    require(
        {row.split("=", 1)[0] for row in selected} == keys,
        "claim evidence row selection is incomplete",
    )
    return selected


def claim_expectations(inventory_contract: dict) -> dict:
    rows = EXPECTED_LINUX_ROWS
    fallbacks = inventory_contract["fallback_rows"]
    current_rows = [
        *rows_with_prefixes(rows, ("current.",)),
        *fallbacks,
        *rows_with_keys(
            rows,
            {
                "profile.release_membership",
                "profile.release_device_claim",
                "runtime_authority",
            },
        ),
    ]
    source_rows = rows_with_prefixes(rows, ("upstream.", "source."))
    synthetic_rows = [
        *rows_with_prefixes(
            rows,
            (
                "profile.",
                "inventory.",
                "unit.",
                "operation.",
                "runtime_binding.",
                "runtime_identity.",
                "membership.",
                "direct.",
                "managed.",
                "device.",
                "release.",
                "negative.",
                "disposition.",
                "synthetic.",
            ),
        ),
        *fallbacks,
        *rows_with_keys(rows, {"runtime_authority"}),
    ]
    native_device_rows = [
        *fallbacks,
        *rows_with_keys(
            rows,
            {
                "native.windows_npu_device_claim",
                "platform.current",
                "profile.release_device_claim",
            },
        ),
    ]
    native_service_rows = [
        *fallbacks,
        *rows_with_keys(
            rows,
            {
                "native.windows_service_membership",
                "platform.current",
                "profile.release_membership",
            },
        ),
    ]
    return {
        "current_lemonade_flm_recovery_authority": {
            "status": "fallback",
            "platforms": ["linux", "windows"],
            "rows": sorted(current_rows),
            "limitations": [
                (
                    "The current integration owns one direct child PID or process "
                    "handle but has no durable service-membership or NPU device-claim "
                    "proof, so all three FLM REC units retain the catalog fallback."
                )
            ],
        },
        "fastflowlm_v0946_exact_source_topology": {
            "status": "passed",
            "platforms": ["linux", "windows"],
            "rows": sorted(source_rows),
            "limitations": [
                (
                    "The pinned main and server sources establish a monolithic "
                    "serving process with in-process worker threads only inside the "
                    "inspected closure; they do not prove package, SCM, runtime "
                    "membership, or device release."
                )
            ],
        },
        "native_windows_flm_device_claim": {
            "status": "deferred",
            "platforms": ["windows"],
            "rows": sorted(native_device_rows),
            "limitations": [
                (
                    "A native Windows XDNA2 observation binding FLM ownership to "
                    "release of each exact NPU device claim is unavailable, so "
                    "physical NPU authority remains deferred."
                )
            ],
        },
        "native_windows_flm_service_membership": {
            "status": "deferred",
            "platforms": ["windows"],
            "rows": sorted(native_service_rows),
            "limitations": [
                (
                    "A native Windows observation of each selected FLM process or "
                    "service membership, instance identity, termination, and empty "
                    "post-membership is unavailable."
                )
            ],
        },
        "synthetic_flm_service_ownership_verifier": {
            "status": "passed",
            "platforms": ["linux", "windows"],
            "rows": sorted(synthetic_rows),
            "limitations": [
                (
                    "Injected evidence proves only per-unit service termination, "
                    "ownership, device-claim, and two-leg release decisions; other "
                    "REC leaves are inventory applicability only, with no native "
                    "Windows, physical NPU, catalog, or runtime authority."
                )
            ],
        },
    }


def load_result_contract(repo_root: Path):
    path = repo_root / "test/residency/prototypes/result_contract.py"
    spec = importlib.util.spec_from_file_location("prototype_result_contract", path)
    require(spec is not None and spec.loader is not None, "result contract unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_result_identity(result: dict) -> None:
    require(result["schema"] == "residency_prototype_result/v1", "schema changed")
    require(result["task_id"] == TASK_ID, "task identity changed")
    require(result["prototype_id"] == PROTOTYPE_ID, "prototype identity changed")
    require(result["task_base_commit"] == TASK_BASE, "TASK_BASE changed")
    require(
        result["source"] == {"path": SOURCE_PATH, "sha256": EXPECTED_SOURCE_SHA256},
        "result source binding changed",
    )
    require(result["outcome"] == "mixed", "result outcome changed")
    require(result["runtime_authority"] == "none", "result granted authority")
    require(
        result["fallback_state"] == {"authority": "legacy_runtime", "status": "active"},
        "active fallback state changed",
    )


def require_claims(result: dict, observation_id: str, inventory_contract: dict) -> None:
    expected = claim_expectations(inventory_contract)
    require(
        set().union(*(set(item["rows"]) for item in expected.values()))
        == set(EXPECTED_LINUX_ROWS),
        "claim evidence leaves observed rows unpartitioned",
    )
    claims = {claim["id"]: claim for claim in result["claims"]}
    require(len(claims) == len(result["claims"]), "claim IDs are not unique")
    require(set(claims) == set(expected), "claim closure changed")
    require(
        [claim["id"] for claim in result["claims"]] == sorted(expected),
        "claims are not in ASCII ID order",
    )
    for claim_id, contract in expected.items():
        claim = claims[claim_id]
        require(claim["status"] == contract["status"], f"{claim_id} status changed")
        require(
            claim["platforms"] == contract["platforms"],
            f"{claim_id} platform scope changed",
        )
        require(claim["affected_units"] == UNIT_IDS, f"{claim_id} units changed")
        require(
            claim["fallback_ids"] == inventory_contract["fallback_ids"],
            f"{claim_id} fallback IDs changed",
        )
        require(
            claim["fallback_bindings"] == inventory_contract["bindings"],
            f"{claim_id} fallback bindings changed",
        )
        require(
            claim["evidence"]
            == [{"observation_id": observation_id, "rows": contract["rows"]}],
            f"{claim_id} evidence changed",
        )
        require(
            claim["limitations"] == contract["limitations"],
            f"{claim_id} limitations changed",
        )


def require_result(repo_root: Path, contract=None):
    if contract is None:
        contract = load_result_contract(repo_root)
    inventory_contract = require_inventory_contract(repo_root)
    result = contract.load_task_result(repo_root, TASK_ID)
    result_root = repo_root / "docs/research/residency-prototype-results/sha256"
    matches = []
    for path in sorted(result_root.glob("*.json")):
        raw = path.read_bytes()
        require(path.stem == hashlib.sha256(raw).hexdigest(), "result address changed")
        candidate = parse_json_object(raw, path.name)
        if candidate.get("task_id") == TASK_ID:
            matches.append(candidate)
    require(matches == [result], "TASK-018 must have one content-addressed result")
    canonical = (
        json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    require(
        any(path.read_bytes() == canonical for path in result_root.glob("*.json")),
        "TASK-018 result is not canonical UTF-8 JSON",
    )
    require_result_identity(result)
    require(len(result["observations"]) == 1, "TASK-018 needs one observation")
    observation = result["observations"][0]
    require(
        observation["environment"]["platform"] == "linux",
        "initial observation must remain Linux",
    )
    require(
        observation["output"]["rows"] == EXPECTED_LINUX_ROWS,
        "initial observation row closure changed",
    )
    require_claims(result, observation["id"], inventory_contract)
    return contract, result


def require_preprocessor_surface(source_text: str) -> None:
    include_directive = re.compile(r"\s*#\s*include\b(.*)")
    standard_include = re.compile(r"\s*<([a-z0-9_./]+)>\s*")
    includes = []
    directives = []
    for line in source_text.splitlines():
        include = include_directive.fullmatch(line)
        if include is not None:
            match = standard_include.fullmatch(include.group(1))
            require(match is not None, "prototype has a non-standard include")
            includes.append(match.group(1))
        elif line.lstrip().startswith("#"):
            directives.append(line.strip())
    require(includes, "prototype standard-library surface is unavailable")
    require(
        set(includes) <= ALLOWED_STANDARD_HEADERS
        and len(includes) == len(set(includes)),
        "prototype header surface changed",
    )
    require("iostream" in includes, "prototype output header is unavailable")
    require(
        directives == ALLOWED_PLATFORM_DIRECTIVES,
        "prototype platform-directive surface changed",
    )
    require(
        source_text.count("#") == len(includes) + len(directives),
        "prototype contains an unparsed preprocessor token",
    )


def require_prototype_source_semantics(source_bytes: bytes) -> None:
    require(
        b"\\\n" not in source_bytes and b"\\\r" not in source_bytes,
        "prototype contains a source line splice",
    )
    source_text = source_bytes.decode("utf-8")
    lowered = source_text.lower()
    require(
        "//" not in source_text and "/*" not in source_text,
        "prototype contains comment-obscured content",
    )
    require(
        "%:" not in source_text and "??=" not in source_text,
        "prototype contains an alternate preprocessor token",
    )
    require_preprocessor_surface(source_text)
    symbols = set(re.findall(r"\bstd\s*::\s*([a-z_][a-z0-9_]*)\b", lowered))
    require(
        symbols <= ALLOWED_STANDARD_SYMBOLS and "cout" in symbols,
        "prototype standard-library symbol surface changed",
    )
    for pattern in FORBIDDEN_PROTOTYPE_SOURCE_PATTERNS:
        require(pattern.search(lowered) is None, "prototype contains an effect API")


def require_rejected_source(candidate: bytes, label: str) -> None:
    try:
        require_prototype_source_semantics(candidate)
    except (AssertionError, UnicodeDecodeError):
        return
    raise AssertionError(f"source guard accepted {label}")


def require_source_guard_regressions(source_bytes: bytes) -> None:
    adversaries = {
        "process call": source_bytes + b'\nint x = system("true");\n',
        "execve call": source_bytes + b"\nint x = execve(0, 0, 0);\n",
        "service call": source_bytes + b"\nint x = OpenServiceA(0, 0, 0);\n",
        "device call": source_bytes + b"\nint x = DeviceIoControl(0,0,0,0,0,0,0,0);\n",
        "non-standard include": b"#include <filesystem>\n" + source_bytes,
        "comment-obscured token": source_bytes + b"\n// system\n",
        "line splice": source_bytes + b"\\\n",
    }
    for label, candidate in adversaries.items():
        require_rejected_source(candidate, label)
    require(
        hashlib.sha256(source_bytes + b"\n").hexdigest() != EXPECTED_SOURCE_SHA256,
        "source digest adversary was not distinct",
    )


def require_probe_source(source: Path) -> None:
    source_bytes = source.read_bytes()
    require(
        EXPECTED_SOURCE_SHA256 != SOURCE_SHA256_PLACEHOLDER,
        "TASK-018 expected source SHA-256 placeholder has not been replaced",
    )
    require(
        hashlib.sha256(source_bytes).hexdigest() == EXPECTED_SOURCE_SHA256,
        "prototype differs from audited bytes",
    )
    require_prototype_source_semantics(source_bytes)
    require_source_guard_regressions(source_bytes)


def current_platform() -> str:
    return {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(
        platform.system(), ""
    )


def compiler_command(
    compiler: str, source: str, output: str, platform_id: str
) -> list[str]:
    if platform_id == "windows":
        return [
            compiler,
            "/nologo",
            "/std:c++17",
            "/W4",
            "/WX",
            "/EHsc",
            source,
            f"/Fe:{output}",
        ]
    return [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-pthread",
        source,
        "-o",
        output,
    ]


def compiler_version(compiler: str, platform_id: str) -> str:
    compiler_name = Path(compiler).name.lower()
    command = (
        [compiler]
        if platform_id == "windows" and compiler_name in {"cl", "cl.exe"}
        else [compiler, "--version"]
    )
    completed = subprocess.run(
        command, check=False, capture_output=True, text=True, timeout=30
    )
    lines = [
        line.strip()
        for line in (completed.stdout + completed.stderr).splitlines()
        if line.strip()
    ]
    require(lines, "compiler version is unavailable")
    return lines[0]


def normalized_architecture() -> str:
    machine = platform.machine().strip().lower()
    return {"amd64": "x86_64", "arm64": "aarch64", "x64": "x86_64"}.get(
        machine, machine
    )


def parse_probe_output(stdout: bytes) -> tuple[list[str], dict[str, str]]:
    require(stdout.endswith(b"\n"), "prototype stdout lacks a final newline")
    lines = stdout.decode("utf-8").splitlines()
    require(lines, "prototype emitted no rows")
    rows = {}
    for line in lines:
        require(line.count("=") == 1, f"malformed row: {line}")
        key, value = line.split("=", 1)
        require(key and value, f"empty row field: {line}")
        require(key not in rows, f"duplicate row key: {key}")
        rows[key] = value
    return lines, rows


def recorded_observation_for_platform(result: dict, platform_id: str):
    observed_ids = {
        evidence["observation_id"]
        for claim in result["claims"]
        if claim["status"] in {"passed", "fallback"}
        and platform_id in claim["platforms"]
        for evidence in claim["evidence"]
    }
    if not observed_ids:
        return None
    matches = [
        observation
        for observation in result["observations"]
        if observation["id"] in observed_ids
        and observation["environment"]["platform"] == platform_id
    ]
    if not matches:
        return None
    require(len(matches) == 1, "platform observation binding is ambiguous")
    return matches[0]


def require_observation_selection_regressions() -> None:
    linux_observation = {
        "id": "linux-observation",
        "environment": {"platform": "linux"},
    }
    result = {
        "claims": [
            {
                "status": "passed",
                "platforms": ["linux", "windows"],
                "evidence": [{"observation_id": "linux-observation"}],
            }
        ],
        "observations": [linux_observation],
    }
    require(
        recorded_observation_for_platform(result, "linux") is linux_observation,
        "Linux observation selection regression failed",
    )
    require(
        recorded_observation_for_platform(result, "windows") is None,
        "unrecorded Windows replay did not remain unbound",
    )


def run_probe(repo_root: Path, source: Path, contract, recorded_observation):
    platform_id = current_platform()
    require(platform_id, "unsupported probe platform")
    compiler_executable, compiler = contract.resolve_replay_compiler(
        recorded_observation, platform_id, os.environ
    )
    version = (
        contract.attest_recorded_observation_toolchain(
            recorded_observation, compiler_executable, compiler, platform_id
        )
        if recorded_observation is not None
        else compiler_version(compiler_executable, platform_id)
    )
    suffix = ".exe" if platform_id == "windows" else ""
    executable_name = f"task018{suffix}"
    logical_source = source.relative_to(repo_root).as_posix()
    logical_output = f"$TMPDIR/{executable_name}"
    logical_compile = compiler_command(
        compiler, logical_source, logical_output, platform_id
    )
    with tempfile.TemporaryDirectory(prefix="residency-task018-") as directory:
        executable = Path(directory) / executable_name
        actual_compile = compiler_command(
            compiler_executable, str(source), str(executable), platform_id
        )
        subprocess.run(
            actual_compile,
            cwd=directory,
            check=True,
            capture_output=True,
            timeout=30,
        )
        run_command = [
            (
                f".{os.sep}{executable.name}"
                if platform_id == "windows"
                else f"./{executable.name}"
            )
        ]
        completed = subprocess.run(
            run_command,
            cwd=directory,
            check=False,
            capture_output=True,
            timeout=30,
        )
    lines, rows = parse_probe_output(completed.stdout)
    binding = {
        "compile_command": logical_compile,
        "command": run_command,
        "environment": {
            "platform": platform_id,
            "architecture": normalized_architecture(),
        },
        "toolchain": {"compiler": compiler, "version": version},
        "exit_code": completed.returncode,
        "output": {
            "stdout_sha256": hashlib.sha256(completed.stdout).hexdigest(),
            "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest(),
            "rows": lines,
        },
    }
    return completed, lines, rows, binding


def require_field_inventory_dimensions() -> None:
    require(
        len(DIRECT_PRE_IDENTITY_FIELDS) == len(set(DIRECT_PRE_IDENTITY_FIELDS)) == 4
        and len(MANAGED_PRE_IDENTITY_FIELDS)
        == len(set(MANAGED_PRE_IDENTITY_FIELDS))
        == 8,
        "mode identity field inventory changed",
    )
    membership_mutations = {
        fields["shape"]: fields["record_mutation"]
        for fields in (
            parse_input_descriptor(item) for item in membership_shape_variants("pre")
        )
    }
    require(
        set(membership_mutations) == set(MEMBERSHIP_SHAPES)
        and "zero_member_declared_zero" in MEMBERSHIP_SHAPES_BY_PHASE["pre"]
        and "zero_member_declared_zero" not in MEMBERSHIP_SHAPES_BY_PHASE["post"]
        and "root_record_count=0" in membership_mutations["zero_root"]
        and "member_record_count=0" in membership_mutations["zero_member_declared_zero"]
        and "duplicate_same_identity" in membership_mutations["duplicate_root"]
        and "duplicate_same_pid_birth_executable"
        in membership_mutations["duplicate_member"]
        and "member_class=unknown" in membership_mutations["unknown_member"]
        and "member_class=external_package"
        in membership_mutations["external_package_member"]
        and "member_class=model_store" in membership_mutations["model_store_member"],
        "membership record mutation inventory changed",
    )
    require(
        set(MEMBERSHIP_SHAPES_BY_PHASE["post"])
        == {"zero_root", "wrong_root", "duplicate_root", "declared_count_mismatch"}
        and all(
            "member_record_count=0" in mutation
            for mutation in POST_MEMBERSHIP_SHAPE_MUTATIONS.values()
        ),
        "post membership shapes no longer preserve the zero-member axis",
    )
    for component in ("pid", "birth_token", "executable_digest"):
        for mutation in ("missing", "zero", "mismatch"):
            require(
                f"member_{component}_{mutation}" in membership_mutations,
                "membership member-tuple mutation inventory changed",
            )
    prepared_device_fields = {
        fields["field"]
        for fields in (
            parse_input_descriptor(item)
            for item in device_specific_identity_variants("pre", "missing")
        )
        if fields["locus"] == "prepared_authority"
    }
    require(
        prepared_device_fields == {"device.claim_identity"},
        "prepared authority gained a duplicate device-owner identity",
    )
    release_device_fields = {
        parse_input_descriptor(item)["field"]
        for item in release_device_claim_tuple_variants("missing")
    }
    require(
        release_device_fields == {"device.claim_identity", "device.claim_generation"}
        and "runtime_binding.device_identity" in COMMON_CAUSAL_TOKEN_FIELDS,
        "device identity was duplicated outside common14",
    )
    claim_tuple_mutations = {
        parse_input_descriptor(item)["mutation"]
        for mutation in ("missing", "zero", "present_nonzero_nonexact")
        for item in release_device_claim_tuple_variants(mutation)
    }
    require(
        claim_tuple_mutations == {"missing", "zero", "present_nonzero_nonexact"},
        "claim-tuple mutation families changed",
    )
    require(
        {
            parse_input_descriptor(item)["field"]
            for item in release_proof_common_token_variants()
        }
        == set(COMMON_CAUSAL_TOKEN_FIELDS),
        "proof common-token splice inventory changed",
    )
    key_shapes = {
        item["shape"]: item["claim_set_mutation"]
        for item in (
            parse_input_descriptor(variant)
            for variant in device_target_key_variants("pre")
        )
    }
    require(
        "exact_count=2,candidate_count=2" in key_shapes["duplicate_target_key"]
        and "two_identical_exact_target_records" in key_shapes["duplicate_target_key"]
        and "exact_count=1,candidate_count=2" in key_shapes["ambiguous_target_key"]
        and "one_exact_target_record_plus_one_wrong_generation_candidate"
        in key_shapes["ambiguous_target_key"],
        "device target-key record shapes changed",
    )
    post_key_shapes = {
        item["shape"]: item["claim_set_mutation"]
        for item in map(parse_input_descriptor, device_target_key_variants("post"))
    }
    require(
        "exact_count=0,candidate_count=1"
        in post_key_shapes["candidate_wrong_generation"]
        and "one_wrong_generation_candidate"
        in post_key_shapes["candidate_wrong_generation"],
        "post target-key candidate shape changed",
    )
    for shapes, exact_count in ((key_shapes, 1), (post_key_shapes, 0)):
        for field in ("identity", "generation"):
            for mutation, value in (("missing", "absent"), ("zero", "0")):
                shape = f"unrelated_claim_{field}_{mutation}"
                record = shapes[shape]
                require(
                    f"exact_count={exact_count},candidate_count={exact_count}" in record
                    and f"malformed_record_field=device.claim_{field}" in record
                    and f"malformed_value={value}" in record,
                    "malformed unrelated claim-record closure changed",
                )
    before_absent = {
        parse_input_descriptor(item)["mutation_state"]
        for item in mode_phase_variants(
            "pre", "device_before", "device_before_claim_absent"
        )
    }
    after_present = {
        parse_input_descriptor(item)["mutation_state"]
        for item in mode_phase_variants(
            "post", "device_after", "device_after_claim_present"
        )
    }
    require(
        before_absent
        == {
            "exact_count=0,candidate_count=0,records=no_target_candidates,"
            "unrelated_claim_count=1"
        }
        and after_present
        == {
            "exact_count=1,candidate_count=1,records=one_exact_target_record,"
            "unrelated_claim_count=1"
        },
        "device target-key absence or presence shape changed",
    )


def require_selector_splice_dimensions() -> None:
    for locus in ("action_target", "ack_target"):
        selectors = [
            parse_input_descriptor(item)
            for item in target_crosswire_variants(locus)
            if "kind=unit_or_model" in item
        ]
        require(
            {item["field"] for item in selectors}
            == {"selection.unit_id", "selection.model_type"}
            and all(item["mutation"] == "valid_other" for item in selectors),
            f"{locus} independent selector axes changed",
        )
    for item in map(parse_input_descriptor, release_proof_unit_model_splice_variants()):
        selected_unit, selected_model = item["selected"].split(":", 1)
        selected = {"unit_id": selected_unit, "model_type": selected_model}
        for proof in ("membership", "device"):
            for component in ("unit_id", "model_type"):
                actual = item[f"{proof}_proof_{component}"]
                mutated = (
                    proof == item["mutated_proof"]
                    and f"selection.{component}" == item["field"]
                )
                require(
                    (actual != selected[component]) == mutated,
                    "proof selector splice changed more than one axis",
                )


def require_manifest_dimensions() -> None:
    require_field_inventory_dimensions()
    require_selector_splice_dimensions()
    require(
        len(prepared_selection_variants("missing")) == 6, "selector missing changed"
    )
    require(
        len(prepared_selection_variants("unknown")) == 6, "selector unknown changed"
    )
    require(len(unit_model_crosswire_variants()) == 12, "selector crosswire changed")
    require(len(operation_leaf_variants()) == 10, "operation leaf matrix changed")
    require(len(operation_leaf_missing_variants()) == 2, "operation missing changed")
    require(len(membership_shape_variants("pre")) == 36, "pre shapes changed")
    require(len(membership_shape_variants("post")) == 8, "post shapes changed")
    require(len(ownership_variants("pre")) == 6, "pre ownership changed")
    require(len(ownership_variants("post")) == 6, "post ownership changed")
    require(
        len(common_token_locus_variants(COMMON_PRE_LOCI, "missing")) == 112,
        "common-token pre loci changed",
    )
    require(
        len(common_token_locus_variants(COMMON_POST_LOCI, "missing")) == 84,
        "common-token post loci changed",
    )
    require(
        len(common_token_anchor_mismatch_variants()) == 28, "anchor mismatch changed"
    )
    require(
        len(common_token_crossleg_variants(COMMON_PRE_LOCI[1:])) == 84,
        "pre cross-leg loci changed",
    )
    require(
        len(common_token_crossleg_variants(COMMON_POST_LOCI)) == 84,
        "post cross-leg loci changed",
    )
    for locus in ("action_target", "ack_target"):
        require(
            len(semantic_target_field_variants(locus, "missing")) == 21,
            f"{locus} missing fields changed",
        )
        require(
            len(semantic_target_field_variants(locus, "zero")) == 21,
            f"{locus} zero fields changed",
        )
        require(
            len(semantic_target_field_variants(locus, "mismatch")) == 19,
            f"{locus} mismatch fields changed",
        )
    require(
        len(target_crosswire_variants("action_target")) == 36,
        "target crosswire changed",
    )
    require(len(target_crosswire_variants("ack_target")) == 36, "ACK crosswire changed")
    require(
        len(mode_pre_identity_variants("missing")) == 24,
        "mode pre identity changed",
    )
    require(
        len(device_specific_identity_variants("pre", "missing")) == 17,
        "device pre identity changed",
    )
    require(
        len(device_specific_identity_variants("post", "missing")) == 15,
        "device post identity changed",
    )
    require(
        len(mode_post_identity_variants("direct_process", "missing")) == 4,
        "direct post changed",
    )
    require(
        len(mode_post_identity_variants("managed_service", "missing")) == 9,
        "managed post changed",
    )
    require(len(device_target_key_variants("pre")) == 12, "device key pre changed")
    require(len(device_target_key_variants("post")) == 10, "device key post changed")
    require(len(evidence_liveness_expiry_variants("pre")) == 2, "lease pre changed")
    require(
        len(evidence_liveness_expiry_variants("no_action")) == 4,
        "lease no-action changed",
    )
    require(len(evidence_liveness_expiry_variants("post")) == 2, "lease post changed")
    require(
        len(service_start_generation_variants("missing")) == 2,
        "service generation changed",
    )
    require(
        len(device_claim_generation_variants("before", "missing")) == 4,
        "claim-generation anchor changed",
    )
    require(
        len(device_claim_generation_variants("after", "missing")) == 2,
        "claim-generation post changed",
    )
    require(
        len(leg_selector_scalar_variants("pre", "missing")) == 12,
        "leg selectors changed",
    )
    require(len(selection_crosswire_variants("pre")) == 90, "leg crosswires changed")
    require(
        len(early_release_credit_variants("no_action")) == 2
        and len(early_release_credit_variants("attempted")) == 4,
        "partial release modes changed",
    )
    require(
        len(release_proof_presence_variants("membership")) == 2,
        "proof presence changed",
    )
    require(
        len(release_proof_unit_model_splice_variants()) == 48,
        "proof unit splice changed",
    )
    require(len(release_proof_mode_splice_variants()) == 4, "proof mode splice changed")
    require(
        len(release_proof_operation_splice_variants()) == 20,
        "proof operation splice changed",
    )
    require(len(release_proof_owner_variants()) == 26, "proof owner splice changed")
    require(
        len(release_proof_common_token_variants()) == 56,
        "proof common14 splice changed",
    )
    require(
        len(release_device_claim_tuple_variants("missing")) == 4,
        "proof claim tuple changed",
    )
    require(len(precondition_fault_variants()) == 819, "precondition faults changed")
    require(
        len(precondition_action_variants("attempted")) == 4914,
        "precondition attempted matrix changed",
    )
    require(
        len(precondition_action_variants("ack_only")) == 2457,
        "precondition ACK-only matrix changed",
    )
    require(
        "negative.each_precondition_fault_no_action"
        not in {row.split("=", 1)[0] for row in NEGATIVE_ROWS},
        "base preconditions were duplicated as no-action fixtures",
    )


def require_action_matrix(rows: dict[str, str]) -> None:
    categories = {
        "pre_dispatch_clean": 2,
        "no_action_intact": 4,
        "contradiction": 16,
        "verified_release": 2,
    }
    variants = []
    for category, count in categories.items():
        category_variants = action_state_variants(category)
        require(len(category_variants) == count, f"{category} action count changed")
        variants.extend(category_variants)
    require(len(variants) == len(set(variants)) == 24, "action states are not closed")
    for variant in variants:
        fields = dict(part.split("=", 1) for part in variant.split("|"))
        require(
            fields["effect_calls"] == ("1" if fields["attempted"] == "true" else "0"),
            "effect calls differ from attempted",
        )
        if fields["attempted"] == "true":
            require(
                fields["post_membership"]
                == fields["post_device"]
                == "verified_released",
                "attempted action lacks verified-released post evidence",
            )
        if (
            fields["request"] in {"accepted", "rejected"}
            and fields["attempted"] == "false"
        ):
            require(
                fields["phase"] == "post"
                and fields["post_membership"]
                == fields["post_device"]
                == "unchanged_intact",
                "recognized no-action evidence is not post/intact",
            )
    for action_state in ("attempted", "ack_only"):
        expected_post = (
            "verified_released" if action_state == "attempted" else "unchanged_intact"
        )
        require(
            all(
                f"post_membership={expected_post}" in variant
                and f"post_device={expected_post}" in variant
                for variant in precondition_action_variants(action_state)
            ),
            f"{action_state} precondition precedence baseline changed",
        )
    require(len(no_action_changed_leg_variants()) == 18, "changed-leg matrix changed")
    require(
        rows["synthetic.action_state_matrix"] == "passed"
        and rows["synthetic.precondition_action_dominance"] == "passed"
        and rows["disposition.effect_calls_equal_attempted"] == "passed"
        and rows["direct.termination_effect_calls"] == "1"
        and rows["managed.termination_effect_calls"] == "1",
        "action-state observations changed",
    )


def require_target_semantics(rows: dict[str, str]) -> None:
    tuple_value = "unit_model_leaf_mode_common14_and_full_identity"
    require(
        rows["runtime_identity.selector_authority"] == "single_typed_logical_field"
        and rows["runtime_identity.prepared_authority"] == "single_structural_input"
        and rows["runtime_identity.prepared_authority_identity"]
        == "one_full_mode_identity"
        and rows["runtime_identity.variant_selector"] == "derived_from_closed_selector"
        and rows["runtime_identity.hidden_causal_identity"] == "absent",
        "prepared authority or selector authority changed",
    )
    require(
        rows["direct.termination_target_semantic_tuple"] == tuple_value
        and rows["managed.termination_target_semantic_tuple"] == tuple_value,
        "termination target semantic tuple changed",
    )
    require(
        rows["direct.termination_ack_target"] == "semantic_tuple_matched"
        and rows["managed.termination_ack_target"] == "semantic_tuple_matched"
        and rows["direct.termination_ack_action_token"] == "matched"
        and rows["managed.termination_ack_action_token"] == "matched",
        "termination acknowledgement binding changed",
    )
    for field in [*DIRECT_TARGET_FIELDS, *MANAGED_TARGET_FIELDS]:
        require(rows[field] == "present_nonzero_exact", f"{field} binding changed")
    require(
        rows["direct.prepared_membership_identity_binding"] == "matched"
        and rows["managed.prepared_membership_identity_binding"] == "matched",
        "prepared-to-membership identity binding changed",
    )
    require(
        rows["direct.identity_shape"] == "controller_and_process_pid_birth_executable"
        and rows["managed.identity_shape"]
        == "controller_manager_service_instance_generation_and_serving_process",
        "mode-specific structural identity changed",
    )
    require(
        rows["direct.service_state"] == "not_applicable"
        and rows["direct.managed_only_fields"] == "structurally_absent",
        "direct inputs gained managed-service fields",
    )


def require_device_lookup_semantics(rows: dict[str, str]) -> None:
    require(
        rows["device.claim_lookup_scope"] == "keyed_target_claim"
        and rows["device.unrelated_claims"] == "out_of_scope"
        and rows["device.target_claim_cardinality"] == "zero_or_one"
        and rows["device.duplicate_target_keys"] == "rejected"
        and rows["device.claim_presence"] == "derived_from_target_key_count"
        and rows["device.prepared_claim_owner_identity"] == "present_nonzero_exact"
        and rows["device.prepared_claim_identity"] == "present_nonzero_exact"
        and rows["device.prepared_claim_generation"] == "23"
        and rows["device.before_claim_anchor_binding"] == "matched"
        and rows["synthetic.device_target_claim_lookup"] == "passed",
        "device target-claim lookup changed",
    )


def require_probe_semantics(rows: dict[str, str]) -> None:
    require_manifest_dimensions()
    require(rows["operation.behavioral_leaf"] == "service_termination", "leaf changed")
    require(
        rows["operation.other_rec_leaves"] == "inventory_applicability_only",
        "non-behavioral REC scope changed",
    )
    require(
        rows["runtime_identity.required_token_count"] == "14"
        and rows["direct.common_causal_tokens"] == "14_exact"
        and rows["managed.common_causal_tokens"] == "14_exact"
        and rows["device.common_causal_tokens"] == "14_exact"
        and rows["runtime_identity.evidence_locus_count"] == "7"
        and rows["runtime_identity.exact_match_across_all_loci"] == "passed"
        and rows["runtime_identity.common_token_storage"]
        == "one_value_per_token_per_locus"
        and rows["release.runtime_identity"] == "all_14_matched",
        "common causal-token binding changed",
    )
    require(
        rows["membership.input_contract"] == "bounded_root_and_member_records"
        and rows["membership.member_record_identity"] == "process_pid_birth_executable"
        and rows["direct.membership_shape_facts"] == "derived_from_bounded_records"
        and rows["managed.membership_shape_facts"] == "derived_from_bounded_records",
        "membership records or derived shape facts changed",
    )
    for model_type in ("embedding", "llm", "transcription"):
        require(
            rows[f"unit.{model_type}.direct_process"] == "verified_release"
            and rows[f"unit.{model_type}.managed_service"] == "verified_release",
            f"{model_type} positive matrix changed",
        )
    require_action_matrix(rows)
    require_target_semantics(rows)
    require(
        rows["managed.service_state_before"] == "running"
        and rows["managed.service_state_before_generation"] == "matched"
        and rows["managed.service_state_after"] == "stopped",
        "managed service-state binding changed",
    )
    require(
        rows["device.claim_generation_before"]
        == rows["device.claim_generation_after"]
        == "23"
        and rows["device.claim_generation_binding"] == "present_nonzero_exact",
        "device claim-generation binding changed",
    )
    require(
        rows["device.device_identity_alias"] == "runtime_binding_device_identity"
        and rows["device.owner_identity_shape"] == "full_mode_specific_identity"
        and rows["device.direct_owner_identity_fields"] == "4_exact"
        and rows["device.managed_owner_identity_fields"] == "9_exact"
        and rows["device.before_owner_identity"] == "matched"
        and rows["device.before_claim_identity"] == "matched"
        and rows["device.after_owner_identity"] == "matched"
        and rows["device.after_claim_identity"] == "matched",
        "device-specific identity binding changed",
    )
    require_device_lookup_semantics(rows)
    require(
        rows["release.direct_membership_and_device"] == "verified_release"
        and rows["release.managed_membership_and_device"] == "verified_release"
        and rows["release.proof_types"] == "distinct_membership_and_device"
        and rows["release.membership_proof_scope"] == "membership_and_owner_only"
        and rows["release.device_proof_scope"] == "exact_device_claim_tuple"
        and rows["release.shared_proof_binding"]
        == "unit_model_mode_operation_common14_owner"
        and rows["release.raw_decision_proof_input"] == "absent"
        and rows["release.composition_expected_binding"] == "prepared_authority"
        and rows["release.composition_validated_action_context"]
        == "attempted_and_acknowledged"
        and rows["release.composition_membership_proof_slot"] == "present"
        and rows["release.composition_device_proof_slot"] == "present"
        and rows["release.composition_proof_validity"] == "derived_from_payload"
        and rows["release.composition_credit_stage"] == "after_both_proofs"
        and rows["release.device_claim_tuple"]
        == "device_identity_claim_identity_generation_matched"
        and rows["release.device_identity_composition_axis"]
        == "common14_runtime_binding_device_identity"
        and rows["device.action_token"] == "common14_binding"
        and rows["release.action_token"] == "common14_binding"
        and rows["current.release_decision"] == "fallback",
        "synthetic and current release decisions were conflated",
    )
    require(
        rows["disposition.membership_only_release"] == "quarantine"
        and rows["disposition.device_only_release"] == "quarantine"
        and rows["disposition.partial_proof_claims"] == "maximum"
        and rows["disposition.partial_proof_release_credit"] == "0"
        and rows["disposition.pre_dispatch_unavailable_authority"] == "fallback"
        and rows["disposition.precondition_effect_calls"] == "0"
        and rows["disposition.precondition_claims"] == "preserved",
        "partial-proof or pre-dispatch disposition changed",
    )
    require(
        rows["negative.each_unit_model_crosswire"] == "unknown"
        and len(unit_model_crosswire_variants()) == 12
        and rows["disposition.precondition_effect_calls"] == "0"
        and rows["disposition.precondition_claims"] == "preserved",
        "unit/model selector crosswire disposition changed",
    )
    require(
        rows["negative.each_membership_shape_pre"] == "unknown"
        and rows["negative.each_membership_shape_post"] == "quarantine"
        and len(membership_shape_variants("pre")) == 36
        and len(membership_shape_variants("post")) == 8
        and "duplicate_root" in MEMBERSHIP_SHAPES
        and "duplicate_member" in MEMBERSHIP_SHAPES
        and rows["disposition.precondition_effect_calls"] == "0"
        and rows["disposition.precondition_claims"] == "preserved"
        and rows["disposition.post_action_claims"] == "maximum"
        and rows["disposition.quarantine_release_credit"] == "0",
        "membership-shape phase disposition changed",
    )
    require(
        rows["disposition.fallback_release_credit"] == "0"
        and rows["disposition.quarantine_release_credit"] == "0"
        and rows["disposition.unverified_release_credit"] == "0"
        and rows["synthetic.zero_release_credit_matrix"] == "passed",
        "unverified release gained credit",
    )
    for row in NEGATIVE_ROWS:
        key, value = row.split("=", 1)
        require(rows[key] == value, f"{key} did not fail closed")
    require_negative_case_manifest(rows)
    require(
        rows["native.windows_service_membership"] == "deferred"
        and rows["native.windows_npu_device_claim"] == "deferred"
        and rows["runtime_authority"] == "none",
        "native deferral or authority changed",
    )


def require_observation_binding(contract, recorded_observation, binding: dict) -> None:
    require(
        recorded_observation is not None,
        "recorded-observation attestation selected no observation",
    )
    contract.require_recorded_observation_body(recorded_observation, binding)


def require_probe(
    repo_root: Path, contract, result: dict, attest_recorded_observation: bool
) -> None:
    source = repo_root / SOURCE_PATH
    require_probe_source(source)
    platform_id = current_platform()
    recorded = (
        recorded_observation_for_platform(result, platform_id)
        if attest_recorded_observation
        else None
    )
    if attest_recorded_observation:
        contract.require_recorded_observation_environment(
            recorded,
            platform_id,
            normalized_architecture(),
        )
    completed, lines, rows, binding = run_probe(repo_root, source, contract, recorded)
    require(completed.returncode == 0, "prototype returned nonzero")
    require(completed.stderr == b"", "prototype emitted stderr")
    expected = expected_output_rows(platform_id)
    require(len(expected) == 411, "row cardinality changed")
    require(len(NEGATIVE_ROWS) == 132, "negative cardinality changed")
    require(len({row.split("=", 1)[0] for row in expected}) == 411, "duplicate keys")
    require(lines == expected, "prototype row order or closure changed")
    require_probe_semantics(rows)
    require(rows["platform.current"] == platform_id, "platform binding changed")
    if attest_recorded_observation:
        require_observation_binding(contract, recorded, binding)


def require_upstream_row_closure() -> None:
    expected = {f"upstream.{key}={value}" for key, value in UPSTREAM_SOURCE.items()}
    require(
        expected <= set(EXPECTED_LINUX_ROWS),
        "FastFlowLM immutable source closure changed",
    )


def require_cmake_and_plan(repo_root: Path) -> None:
    cmake = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    cmake_block = "\n".join(
        [
            "set(_RESIDENCY_PROTOTYPE_TASK018_TEST_SRC",
            f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/{SOURCE_PATH}"',
            ")",
            'if(BUILD_TESTING AND EXISTS "${_RESIDENCY_PROTOTYPE_TASK018_TEST_SRC}")',
            "    add_executable(test_residency_prototype_task018",
            "        ${_RESIDENCY_PROTOTYPE_TASK018_TEST_SRC}",
            "    )",
            (
                "    add_cpp_ci_test(ResidencyPrototypeContractTask018 CI ON COMMAND "
                "test_residency_prototype_task018)"
            ),
            (
                "    set_tests_properties(ResidencyPrototypeContractTask018 PROPERTIES "
                "TIMEOUT 45)"
            ),
            "endif()",
        ]
    )
    require(
        cmake.count(cmake_block) == 1,
        "TASK-018 CMake declaration is not one closed BUILD_TESTING block",
    )
    require(cmake.count(SOURCE_PATH) == 1, "TASK-018 source is not registered once")
    plan = (repo_root / PLAN_PATH).read_text(encoding="utf-8")
    task_row = next(
        (line for line in plan.splitlines() if line.startswith("| TASK-018 |")), ""
    )
    require(task_row.endswith("| ✅ | 2026-08-16 |"), "TASK-018 is not complete")
    ownership_rows = [
        line for line in plan.splitlines() if line.startswith("| TASK-014–TASK-018 |")
    ]
    require(len(ownership_rows) == 1, "Phase-2 ownership row is ambiguous")
    for required_text in (
        "CMakeLists.txt",
        SOURCE_PATH,
        "test/residency/prototypes/",
        "docs/research/residency-prototype-results/",
        "no production authority",
    ):
        require(
            required_text in ownership_rows[0], f"ownership row omits {required_text}"
        )


def main(arguments: list[str]) -> int:
    attest_recorded_observation = parse_replay_mode(arguments)
    repo_root = repository_root()
    required = (
        repo_root / "test/residency/prototypes/result_contract.py",
        repo_root / SOURCE_PATH,
    )
    if not all(path.is_file() for path in required):
        fail_unavailable()
    contract = load_result_contract(repo_root)
    try:
        require_upstream_row_closure()
        require_observation_selection_regressions()
        require_live_source_audit(repo_root)
        contract, result = require_result(repo_root, contract)
        require_probe(repo_root, contract, result, attest_recorded_observation)
        require_cmake_and_plan(repo_root)
    except contract.PrototypeResultError as error:
        print(
            f"{Path(__file__).name}: {contract.public_diagnostic(error)}",
            file=sys.stderr,
        )
        return 1
    except (AssertionError, OSError, subprocess.SubprocessError) as error:
        print(
            f"{Path(__file__).name}: "
            f"{contract.public_operational_failure(attest_recorded_observation, error)}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
