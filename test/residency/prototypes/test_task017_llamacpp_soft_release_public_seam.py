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

FAILURE = "TASK-017 llama.cpp soft-release prototype is unavailable"
TASK_BASE = "baf6e251a7c39e9503d24e2b5d5cc4cf365229bd"
TASK_ID = "TASK-017"
SOURCE_PATH = "test/cpp/test_residency_prototype_task017.cpp"
SOURCE_SHA256_PLACEHOLDER = "__TASK017_SOURCE_SHA256__"
EXPECTED_SOURCE_SHA256 = (
    "c9dfc7d88b47fef2c0831d0238b4337cb26ab2152e11d66b472f33e4bc2512be"
)
PROTOTYPE_ID = "verified_llamacpp_soft_release"
TASK_BASE_LIVE_SOURCE_SHA256 = {
    "src/cpp/include/lemon/wrapped_server.h": (
        "86ea156341e5bdab7cfeb8163f1f8eff767ae9e67ce95cd9094d18e18b5bce63"
    ),
    "src/cpp/server/backends/llamacpp/llamacpp_server.cpp": (
        "9b4e1b8007b2f43ef6279350690be1f49f2ba904406fb4ed5370d53fa8574bd4"
    ),
    "src/cpp/server/eviction_engine.cpp": (
        "b3893b209476c562d2cde4559b2430202b3e5bce09deecfb322a40b7ead41de5"
    ),
}

ROCM_UNIT_ID = "H-ROCM-PRE-GTT-HOST-v1"
VULKAN_UNIT_ID = "H-VULKAN-PRE-GTT-HOST-v1"
UNIT_IDS = [ROCM_UNIT_ID, VULKAN_UNIT_ID]
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
REQUIRED_IDENTITY_TOKEN_NAMES = [
    *RUNTIME_BINDING_NAMES,
    "resident_id",
    "resident_generation",
    "backend_instance_birth_token",
    "topology_generation",
    "allocation_group_id",
    "observation_contract_digest",
    "action_lease",
    "action_lease_claim_generation",
    "pre_observation_generation",
    "ledger_generation",
]
REQUIRED_IDENTITY_TOKEN_COUNT = len(REQUIRED_IDENTITY_TOKEN_NAMES)

ROCM_FALLBACKS = {
    "invalid_reporting_evidence": "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
    "valid_reporting_without_action_authority": "hatchery_rocm_pressure_report_only_v1",
}
VULKAN_FALLBACKS = {
    "invalid_reporting_evidence": "residency_pressure_disabled_invalid_evidence_v1",
    "valid_reporting_without_action_authority": (
        "residency_pressure_report_only_unvalidated_v1"
    ),
}
FALLBACK_DEFINITIONS = {
    "hatchery_rocm_pressure_disabled_invalid_evidence_v1": {
        "operations": ["PRE"],
        "guard": "Hatchery reporting evidence invalid",
        "effect": "disable pressure automation and preserve residency",
    },
    "hatchery_rocm_pressure_report_only_v1": {
        "operations": ["PRE"],
        "guard": ("Hatchery reporting evidence valid but action authority unavailable"),
        "effect": "report only and preserve residency",
    },
    "residency_pressure_disabled_invalid_evidence_v1": {
        "operations": ["PRE"],
        "guard": "reporting evidence missing, stale, unhealthy, or incoherent",
        "effect": "disable pressure automation and preserve residency",
    },
    "residency_pressure_report_only_unvalidated_v1": {
        "operations": ["PRE"],
        "guard": "reporting evidence valid but action authority unavailable",
        "effect": "report only and preserve residency",
    },
}
ROCM_UNIT_RECORD_SHA256 = (
    "3eba92525ccaa67b8a4118ba0b97a17eb3282e8a395eecc95bca7bece899d841"
)
VULKAN_UNIT_RECORD_SHA256 = (
    "6b9839af85839fb18533fa80dfb41dda88bdfccd6666271bbed0c39700bb6826"
)

SOURCE_OUTPUT_ROWS = [
    "source.llamacpp_slot_erase=logical_cache_reset",
    "source.llamacpp_ack_validation=absent",
    "source.llamacpp_physical_observer=absent",
    "source.llamacpp_ledger_reconciliation=absent",
    "source.llamacpp_empty_slot_success=possible",
    "source.wrapped_server_default_downsize=noop_success",
    "source.wrapped_server_default_support=unsupported",
    "source.downsized_state_physical_proof=absent",
    "source.current_release_authority=fallback",
]
MECHANISM_OUTPUT_ROWS = [
    "mechanism.id=llamacpp_slot_cache_erase_v1",
    "mechanism.capability=explicit",
    "mechanism.slot_selection=exact",
    "mechanism.acknowledgement=exact",
    "mechanism.physical_observation=required",
    "mechanism.ledger_reconciliation=required",
]
IDENTITY_OUTPUT_ROWS = [
    "identity.device_identity=required",
    "identity.backend_artifact_digest=required",
    "identity.source_build_dependency_closure=required",
    "identity.driver_runtime_closure=required",
    "identity.model_manifest_digest=required",
    "identity.normalized_configuration_digest=required",
    "identity.evidence_index_digest=required",
    "identity.evidence_liveness_lease=required",
    "identity.resident_id=required",
    "identity.resident_generation=required",
    "identity.backend_instance_birth_token=required",
    "identity.topology_generation=required",
    "identity.allocation_group_id=required",
    "identity.ledger_generation=required",
    "identity.action_lease=required",
    "identity.action_lease_binding=matched",
    "identity.action_lease_state=idle_soft_reclaiming",
    "identity.action_lease_operation=pressure_soft_release",
    "identity.action_lease_slot_set_binding=matched",
    "identity.action_lease_claim_generation_binding=matched",
    "identity.observation_contract_digest=required",
    "identity.pre_observation_generation=required",
    "identity.post_observation_generation=derived_checked",
    "identity.required_token_count=18",
    "identity.required_tokens_nonzero=passed",
    "identity.exact_match=passed",
]
SLOT_OUTPUT_ROWS = [
    "slot.request_count=2",
    "slot.request_ids=3.7",
    "slot.ack_count=2",
    "slot.ack_ids=3.7",
    "slot.ack_match=passed",
]
OBSERVATION_OUTPUT_ROWS = [
    "observation.before_present=passed",
    "observation.before_fresh=passed",
    "observation.before_skew=passed",
    "observation.before_healthy=passed",
    "observation.before_complete=passed",
    "observation.after_present=passed",
    "observation.after_fresh=passed",
    "observation.after_skew=passed",
    "observation.after_healthy=passed",
    "observation.after_complete=passed",
    "observation.generation_before=41",
    "observation.generation_after=42",
    "observation.generation_increment=passed",
]
PHYSICAL_OUTPUT_ROWS = [
    "physical.cache_gtt_before_bytes=2048",
    "physical.cache_gtt_after_bytes=0",
    "physical.cache_gtt_released_bytes=2048",
    "physical.cache_host_before_bytes=1024",
    "physical.cache_host_after_bytes=0",
    "physical.cache_host_released_bytes=1024",
    "physical.global_gtt_headroom_before_present=passed",
    "physical.global_gtt_headroom_before_bytes=4096",
    "physical.global_gtt_headroom_after_present=passed",
    "physical.global_gtt_headroom_after_bytes=5632",
    "physical.global_host_headroom_before_present=passed",
    "physical.global_host_headroom_before_bytes=2048",
    "physical.global_host_headroom_after_present=passed",
    "physical.global_host_headroom_after_bytes=2816",
    "physical.unrelated_gtt_growth_bytes=512",
    "physical.unrelated_host_growth_bytes=256",
    "physical.global_gtt_headroom_improvement_bytes=1536",
    "physical.global_host_headroom_improvement_bytes=768",
    "physical.global_headroom_delta_checked=passed",
    "physical.target_attribution=exact",
    "physical.cache_group_zero=passed",
    "physical.release=verified",
]
CAUSAL_OUTPUT_ROWS = [
    "causal.global_delta_not_credited=passed",
    "causal.owned_gtt_release_bytes=2048",
    "causal.owned_host_release_bytes=1024",
    "causal.mechanism_completion=observed",
    "causal.effect_binding=matched",
]
RESIDENT_OUTPUT_ROWS = [
    "resident.process_identity=preserved",
    "resident.weights=preserved",
    "resident.pin_before_known=passed",
    "resident.pin_after_known=passed",
    "resident.pin=preserved",
    "resident.pin_soft_release=allowed",
    "resident.model_residency=preserved",
]
LEDGER_OUTPUT_ROWS = [
    "ledger.before_claim_groups=2",
    "ledger.before_group_0_id=model_weights",
    "ledger.before_group_0_effect=persistent_weights",
    "ledger.before_group_0_gtt_bytes=8192",
    "ledger.before_group_0_host_bytes=4096",
    "ledger.before_group_1_id=slot_cache",
    "ledger.before_group_1_effect=reconstructible_state",
    "ledger.before_group_1_gtt_bytes=2048",
    "ledger.before_group_1_host_bytes=1024",
    "ledger.after_claim_groups=1",
    "ledger.after_group_0_id=model_weights",
    "ledger.after_group_0_effect=persistent_weights",
    "ledger.after_group_0_gtt_bytes=8192",
    "ledger.after_group_0_host_bytes=4096",
    "ledger.after_slot_cache=absent",
    "ledger.removed_group_count=1",
    "ledger.released_claim_group=reconstructible_state",
    "ledger.retained_claim_group=persistent_weights",
    "ledger.other_claims=unchanged",
    "ledger.generation_before=17",
    "ledger.generation_after=18",
    "ledger.generation_increment=passed",
    "ledger.credit_before_ack_bytes=0",
    "ledger.credit_before_physical_verification_bytes=0",
    "ledger.credited_gtt_bytes=2048",
    "ledger.credited_host_bytes=1024",
    "ledger.reconciliation_after_verification=passed",
    "ledger.atomic_commit=passed",
    "ledger.stale_generation=blocked",
    "ledger.commit_failure=quarantined",
]
NEGATIVE_OUTPUT_ROWS = [
    "negative.slot_set_missing=unknown",
    "negative.slot_set_empty=unknown",
    "negative.slot_id_duplicate=unknown",
    "negative.slot_count_oversized=unknown",
    "negative.ack_missing=quarantine",
    "negative.ack_count_mismatch=unknown",
    "negative.ack_id_mismatch=unknown",
    "negative.ack_partial=unknown",
    "negative.dispatch_failure=unknown",
    "negative.dispatch_completed_without_attempt=quarantine",
    "negative.invalid_precondition_after_dispatch_attempt=quarantine",
    "negative.valid_precondition_without_dispatch=verified_intact",
    "negative.valid_precondition_without_dispatch_changed_post=quarantine",
    "negative.unsupported_mechanism_after_dispatch_attempt=quarantine",
    "negative.unsupported_mechanism_without_dispatch_changed_post=quarantine",
    "negative.ack_without_physical_delta=verified_intact",
    "negative.ack_without_physical_delta_each_axis_changed=unknown",
    "negative.pre_identity_mismatch=unknown",
    "negative.post_identity_mismatch=unknown",
    "negative.backend_instance_birth_token_mismatch=unknown",
    "negative.process_identity_missing=quarantine",
    "negative.process_identity_zero=quarantine",
    "negative.process_identity_changed=quarantine",
    "negative.weights_identity_missing=quarantine",
    "negative.weights_identity_zero=quarantine",
    "negative.weights_identity_changed=quarantine",
    "negative.model_residency_identity_missing=quarantine",
    "negative.model_residency_identity_zero=quarantine",
    "negative.model_residency_identity_changed=quarantine",
    "negative.pin_missing=quarantine",
    "negative.pin_changed=quarantine",
    "negative.device_identity_mismatch=unknown",
    "negative.backend_artifact_mismatch=unknown",
    "negative.source_build_dependency_mismatch=unknown",
    "negative.driver_runtime_mismatch=unknown",
    "negative.model_manifest_mismatch=unknown",
    "negative.normalized_configuration_mismatch=unknown",
    "negative.evidence_index_mismatch=unknown",
    "negative.evidence_liveness_missing=unknown",
    "negative.evidence_liveness_expired=unknown",
    "negative.observation_contract_mismatch=unknown",
    "negative.topology_generation_mismatch=unknown",
    "negative.allocation_group_mismatch=unknown",
    "negative.action_lease_missing=unknown",
    "negative.action_lease_mismatch=unknown",
    "negative.action_lease_identity_mismatch=unknown",
    "negative.action_lease_evidence_liveness_lease_mismatch=unknown",
    "negative.action_lease_wrong_state=unknown",
    "negative.action_lease_wrong_operation=unknown",
    "negative.action_lease_wrong_slot_set=unknown",
    "negative.action_lease_wrong_claim_generation=unknown",
    "negative.action_lease_wrong_pre_observation_generation=unknown",
    "negative.action_lease_wrong_observation_contract=unknown",
    "negative.resident_generation_mismatch=unknown",
    "negative.observation_generation_mismatch=unknown",
    "negative.each_required_identity_token_zero=unknown",
    "negative.observation_before_missing=unknown",
    "negative.observation_before_stale=unknown",
    "negative.observation_before_skew=unknown",
    "negative.observation_before_unhealthy=unknown",
    "negative.observation_before_incomplete=unknown",
    "negative.observation_before_gtt_effect_missing=unknown",
    "negative.observation_before_host_effect_missing=unknown",
    "negative.observation_after_missing=unknown",
    "negative.observation_after_stale=unknown",
    "negative.observation_after_skew=unknown",
    "negative.observation_after_unhealthy=unknown",
    "negative.observation_after_incomplete=unknown",
    "negative.release_partial=unknown",
    "negative.gtt_release_partial=unknown",
    "negative.host_release_partial=unknown",
    "negative.cache_nonzero_after=unknown",
    "negative.gtt_cache_nonzero_after=unknown",
    "negative.host_cache_nonzero_after=unknown",
    "negative.gtt_effect_missing=unknown",
    "negative.host_effect_missing=unknown",
    "negative.global_gtt_headroom_missing=unknown",
    "negative.global_host_headroom_missing=unknown",
    "negative.ledger_claim_group_missing=unknown",
    "negative.ledger_claim_group_duplicate=unknown",
    "negative.ledger_claim_group_wrong_id=unknown",
    "negative.ledger_weights_group_wrong_id=unknown",
    "negative.ledger_claim_group_wrong_effect=unknown",
    "negative.ledger_weights_group_wrong_effect=unknown",
    "negative.ledger_claim_group_wrong_allocation=unknown",
    "negative.ledger_weights_group_wrong_allocation=unknown",
    "negative.ledger_weights_gtt_measurement_missing=unknown",
    "negative.ledger_weights_host_measurement_missing=unknown",
    "negative.ledger_cache_gtt_measurement_missing=unknown",
    "negative.ledger_cache_host_measurement_missing=unknown",
    "negative.ledger_weights_bytes_mismatch=unknown",
    "negative.ledger_weights_gtt_bytes_mismatch=unknown",
    "negative.ledger_weights_host_bytes_mismatch=unknown",
    "negative.ledger_cache_bytes_mismatch=unknown",
    "negative.ledger_cache_gtt_bytes_mismatch=unknown",
    "negative.ledger_cache_host_bytes_mismatch=unknown",
    "negative.effect_out_of_envelope=unknown",
    "negative.gtt_release_exceeds_maximum=unknown",
    "negative.host_release_exceeds_maximum=unknown",
    "negative.unrelated_demand_miscredit=unknown",
    "negative.gtt_causal_mismatch=unknown",
    "negative.host_causal_mismatch=unknown",
    "negative.unrelated_gtt_growth_missing=unknown",
    "negative.unrelated_host_growth_missing=unknown",
    "negative.observation_generation_increment_overflow=unknown",
    "negative.arithmetic_overflow=unknown",
    "negative.gtt_causal_checked_add_overflow=unknown",
    "negative.host_causal_checked_add_overflow=unknown",
    "negative.ledger_generation_increment_overflow=unknown",
    "negative.global_headroom_subtraction_underflow=unknown",
    "negative.gtt_global_headroom_subtraction_underflow=unknown",
    "negative.host_global_headroom_subtraction_underflow=unknown",
    "negative.cache_release_subtraction_underflow=unknown",
    "negative.gtt_cache_release_subtraction_underflow=unknown",
    "negative.host_cache_release_subtraction_underflow=unknown",
    "negative.ledger_generation_stale=unknown",
    "negative.ledger_commit_failure=unknown",
    "negative.active_use=unknown",
]
DISPOSITION_OUTPUT_ROWS = [
    "disposition.pre_dispatch_failure=verified_intact",
    "disposition.pre_dispatch_post_observation=unchanged_fresh",
    "disposition.ack_without_effect=verified_intact",
    "disposition.ack_without_effect_ledger=unchanged",
    "disposition.ack_without_effect_credit_bytes=0",
    "disposition.ack_without_effect_post_observation=unchanged_fresh",
    "disposition.post_dispatch_ambiguous=quarantine",
    "disposition.post_dispatch_observation=unavailable",
    "disposition.invalid_post_residency=not_preserved",
    "disposition.ambiguous_claims=maximum",
    "disposition.active_use=preserved",
]
UNSUPPORTED_OUTPUT_ROWS = [
    "unsupported.capability_absent=detected",
    "unsupported.successful_noop=detected",
    "unsupported.dispatch_calls=0",
    "unsupported.post_observation=unchanged_fresh",
    "unsupported.ledger=unchanged",
    "unsupported.residency=preserved",
    "unsupported.pressure_mode=report_only",
    "unsupported.reporting_valid=report_only",
    "unsupported.reporting_missing=disabled_invalid_evidence",
    "unsupported.reporting_stale=disabled_invalid_evidence",
    "unsupported.reporting_skew=disabled_invalid_evidence",
    "unsupported.reporting_unhealthy=disabled_invalid_evidence",
    "unsupported.reporting_incomplete=disabled_invalid_evidence",
    "unsupported.reporting_incoherent=disabled_invalid_evidence",
    "unsupported.invalid_reporting_dispatch_calls=0",
    "unsupported.invalid_reporting_post_observation=unchanged_fresh",
    "unsupported.invalid_reporting_ledger=unchanged",
    "unsupported.invalid_reporting_residency=preserved",
]
SELECTED_FALLBACK_OUTPUT_ROWS = [
    "selected_fallback.rocm_valid_reporting="
    + ROCM_FALLBACKS["valid_reporting_without_action_authority"],
    "selected_fallback.rocm_invalid_reporting="
    + ROCM_FALLBACKS["invalid_reporting_evidence"],
    "selected_fallback.vulkan_valid_reporting="
    + VULKAN_FALLBACKS["valid_reporting_without_action_authority"],
    "selected_fallback.vulkan_invalid_reporting="
    + VULKAN_FALLBACKS["invalid_reporting_evidence"],
]
SYNTHETIC_OUTPUT_ROWS = [
    "synthetic.observation_source=injected",
    "synthetic.verified_soft_release=passed",
    "synthetic.fail_closed_matrix=passed",
    "synthetic.negative_fixture_shapes=passed",
    "synthetic.unsupported_mechanism=fallback",
]
NATIVE_OUTPUT_ROWS = [
    "native.llamacpp_rocm_physical_release=deferred",
    "native.llamacpp_vulkan_physical_release=deferred",
]

ALLOWED_STANDARD_HEADERS = {
    "array",
    "cstddef",
    "cstdint",
    "iostream",
    "limits",
}
ALLOWED_STANDARD_SYMBOLS = {
    "array",
    "cout",
    "numeric_limits",
    "size_t",
    "uint32_t",
    "uint64_t",
    "uint8_t",
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
        r"\b(?:extern|system|popen|posix_spawn[a-z0-9_]*|fork|vfork|clone|"
        r"exec[a-z0-9_]*|createprocess[a-z0-9_]*|shellexecute[a-z0-9_]*|"
        r"winexec|hip[a-z0-9_]*|hsa_[a-z0-9_]*|ioctl|syscall|dlopen|dlsym|"
        r"loadlibrary[a-z0-9_]*|getprocaddress|open|openat|fopen|creat|"
        r"mmap|munmap|mprotect|madvise|pthread_create|thrd_create|pipe|pipe2|"
        r"dup|dup2|dup3|eventfd|memfd_create|socket|socketpair|accept|accept4|"
        r"bind|listen|getaddrinfo|connect|send|recv|epoll_create[a-z0-9_]*|"
        r"inotify_init[a-z0-9_]*|timerfd_create|signalfd|pidfd_open|"
        r"createfile[a-z0-9_]*|wsasocket[a-z0-9_]*|winhttp[a-z0-9_]*|"
        r"internetopen[a-z0-9_]*|__builtin_[a-z0-9_]*|_?alloca)\b"
    ),
    re.compile(r"\b_spawn[a-z0-9_]*\b"),
    re.compile(r"\b(?:asm|__asm__?)\b"),
    re.compile(
        r"\b(?:malloc|calloc|realloc|free|new|delete|make_unique|make_shared)\b"
    ),
    re.compile(r"\busing\s+namespace\s+std\b|\bnamespace\s+[a-z0-9_]+\s*=\s*std\b"),
)


def fallback_output_rows() -> list[str]:
    return [
        "fallback_binding.rocm_pressure_invalid="
        + ROCM_FALLBACKS["invalid_reporting_evidence"],
        "fallback_binding.rocm_pressure_report="
        + ROCM_FALLBACKS["valid_reporting_without_action_authority"],
        "fallback_binding.vulkan_pressure_invalid="
        + VULKAN_FALLBACKS["invalid_reporting_evidence"],
        "fallback_binding.vulkan_pressure_report="
        + VULKAN_FALLBACKS["valid_reporting_without_action_authority"],
    ]


def expected_output_rows(platform_id: str) -> list[str]:
    return [
        *SOURCE_OUTPUT_ROWS,
        *MECHANISM_OUTPUT_ROWS,
        *IDENTITY_OUTPUT_ROWS,
        *SLOT_OUTPUT_ROWS,
        *OBSERVATION_OUTPUT_ROWS,
        *PHYSICAL_OUTPUT_ROWS,
        *CAUSAL_OUTPUT_ROWS,
        *RESIDENT_OUTPUT_ROWS,
        *LEDGER_OUTPUT_ROWS,
        *NEGATIVE_OUTPUT_ROWS,
        *DISPOSITION_OUTPUT_ROWS,
        *UNSUPPORTED_OUTPUT_ROWS,
        *SELECTED_FALLBACK_OUTPUT_ROWS,
        *SYNTHETIC_OUTPUT_ROWS,
        *NATIVE_OUTPUT_ROWS,
        *fallback_output_rows(),
        f"platform.current={platform_id}",
        "runtime_authority=none",
    ]


def fail_unavailable() -> None:
    print(FAILURE, file=sys.stderr)
    raise SystemExit(1)


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def load_result_contract(repo_root: Path):
    path = repo_root / "test/residency/prototypes/result_contract.py"
    if not path.is_file():
        fail_unavailable()
    spec = importlib.util.spec_from_file_location("prototype_result_contract", path)
    if spec is None or spec.loader is None:
        fail_unavailable()
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_json_object(raw: bytes) -> dict:
    def object_from_pairs(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise AssertionError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    value = json.loads(raw.decode("utf-8"), object_pairs_hook=object_from_pairs)
    require(type(value) is dict, "prototype result must be an object")
    return value


def canonical_json_sha256(value) -> str:
    payload = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def require_exact_fallbacks(row: dict, expected: dict[str, str], label: str) -> None:
    require(row.get("fallbacks") == expected, f"{label} fallback mapping changed")


def find_inventory_row(rows, key: str, expected_id: str, label: str) -> dict:
    require(type(rows) is list, f"{label} registry is unavailable")
    matches = [row for row in rows if type(row) is dict and row.get(key) == expected_id]
    require(len(matches) == 1, f"{label} must contain {expected_id} exactly once")
    return matches[0]


def require_rocm_inventory_row(row: dict) -> None:
    require(row.get("operation_template") == "PRE", "ROCm unit changed operation")
    require(
        row.get("operation_leaf") == "pressure_reclamation",
        "ROCm unit changed operation leaf",
    )
    require(
        row.get("capability_level") == "unsupported"
        and row.get("delivery_state") == "absent",
        "ROCm unit changed its inactive delivery state",
    )
    require_exact_fallbacks(row, ROCM_FALLBACKS, ROCM_UNIT_ID)


def require_vulkan_inventory_row(row: dict) -> None:
    selector = row.get("selector")
    require(type(selector) is dict, "Vulkan selector is unavailable")
    require(
        selector.get("operation_template") == "PRE", "Vulkan unit changed operation"
    )
    require(
        selector.get("operation_leaves") == ["pressure_reclamation"],
        "Vulkan unit changed operation leaves",
    )
    require(
        row.get("initial_state")
        == {"capability_level": "unsupported", "delivery_state": "absent"},
        "Vulkan unit changed its inactive delivery state",
    )
    require(
        row.get("delivery_gate") == f"release_verified:{VULKAN_UNIT_ID}",
        "Vulkan delivery gate changed",
    )
    require_exact_fallbacks(row, VULKAN_FALLBACKS, VULKAN_UNIT_ID)


def require_inventory_contract(repo_root: Path) -> dict:
    path = repo_root / "docs/research/portable-residency-capability-inventory.json"
    inventory = parse_json_object(path.read_bytes())
    require(
        inventory.get("runtime_binding_kinds") == RUNTIME_BINDING_NAMES,
        "inventory changed the closed runtime-binding vocabulary",
    )
    rocm = find_inventory_row(
        inventory.get("exact_cells"), "cell_id", ROCM_UNIT_ID, "exact-cell"
    )
    vulkan = find_inventory_row(
        inventory.get("later_promotion_roster"),
        "unit_id",
        VULKAN_UNIT_ID,
        "later-promotion",
    )
    require_rocm_inventory_row(rocm)
    require_vulkan_inventory_row(vulkan)
    require(
        rocm.get("runtime_bindings") == RUNTIME_BINDING_NAMES,
        "ROCm PRE unit changed its runtime-binding closure",
    )
    require(
        canonical_json_sha256(rocm) == ROCM_UNIT_RECORD_SHA256,
        "ROCm PRE unit changed its exact inventory record",
    )
    require(
        canonical_json_sha256(vulkan) == VULKAN_UNIT_RECORD_SHA256,
        "Vulkan PRE unit changed its exact inventory record",
    )
    fallback_registry = inventory.get("fallback_registry")
    require(type(fallback_registry) is dict, "fallback registry is unavailable")
    bindings = [
        {"unit_id": ROCM_UNIT_ID, "fallbacks": ROCM_FALLBACKS},
        {"unit_id": VULKAN_UNIT_ID, "fallbacks": VULKAN_FALLBACKS},
    ]
    fallback_ids = sorted(
        {
            *ROCM_FALLBACKS.values(),
            *VULKAN_FALLBACKS.values(),
        }
    )
    require(
        set(fallback_ids) <= set(fallback_registry),
        "TASK-017 fallback registry closure changed",
    )
    require(
        {
            fallback_id: fallback_registry.get(fallback_id)
            for fallback_id in fallback_ids
        }
        == FALLBACK_DEFINITIONS,
        "TASK-017 fallback registry definitions changed",
    )
    return {
        "bindings": bindings,
        "fallback_ids": fallback_ids,
        "fallback_rows": fallback_output_rows(),
    }


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


def normalized_architecture() -> str:
    machine = platform.machine().strip().lower()
    return {
        "amd64": "x86_64",
        "arm64": "aarch64",
        "x64": "x86_64",
    }.get(machine, machine)


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
    require(lines, f"recorded compiler {compiler} emitted no version")
    return lines[0]


def rows_with_keys(rows: list[str], keys: set[str]) -> list[str]:
    selected = [row for row in rows if row.split("=", 1)[0] in keys]
    require(
        {row.split("=", 1)[0] for row in selected} == keys,
        "claim evidence row selection is incomplete",
    )
    return selected


def claim_expectations(inventory_contract: dict) -> dict:
    bindings = {
        binding["unit_id"]: binding for binding in inventory_contract["bindings"]
    }
    all_bindings = inventory_contract["bindings"]
    all_fallbacks = inventory_contract["fallback_ids"]
    fallback_rows = inventory_contract["fallback_rows"]
    native_source_rows = [*SOURCE_OUTPUT_ROWS[:4], SOURCE_OUTPUT_ROWS[8]]
    valid_unsupported_rows = rows_with_keys(
        UNSUPPORTED_OUTPUT_ROWS,
        {
            "unsupported.capability_absent",
            "unsupported.successful_noop",
            "unsupported.dispatch_calls",
            "unsupported.post_observation",
            "unsupported.ledger",
            "unsupported.residency",
            "unsupported.pressure_mode",
            "unsupported.reporting_valid",
        },
    )
    valid_selected_fallback_rows = rows_with_keys(
        SELECTED_FALLBACK_OUTPUT_ROWS,
        {
            "selected_fallback.rocm_valid_reporting",
            "selected_fallback.vulkan_valid_reporting",
        },
    )
    invalid_unsupported_rows = rows_with_keys(
        UNSUPPORTED_OUTPUT_ROWS,
        {
            "unsupported.reporting_missing",
            "unsupported.reporting_stale",
            "unsupported.reporting_skew",
            "unsupported.reporting_unhealthy",
            "unsupported.reporting_incomplete",
            "unsupported.reporting_incoherent",
            "unsupported.invalid_reporting_dispatch_calls",
            "unsupported.invalid_reporting_post_observation",
            "unsupported.invalid_reporting_ledger",
            "unsupported.invalid_reporting_residency",
        },
    )
    invalid_selected_fallback_rows = rows_with_keys(
        SELECTED_FALLBACK_OUTPUT_ROWS,
        {
            "selected_fallback.rocm_invalid_reporting",
            "selected_fallback.vulkan_invalid_reporting",
        },
    )
    verified_rows = sorted(
        [
            *MECHANISM_OUTPUT_ROWS,
            *IDENTITY_OUTPUT_ROWS,
            *SLOT_OUTPUT_ROWS,
            *OBSERVATION_OUTPUT_ROWS,
            *PHYSICAL_OUTPUT_ROWS,
            *CAUSAL_OUTPUT_ROWS,
            *RESIDENT_OUTPUT_ROWS,
            *LEDGER_OUTPUT_ROWS,
            *NEGATIVE_OUTPUT_ROWS,
            *DISPOSITION_OUTPUT_ROWS,
            *SYNTHETIC_OUTPUT_ROWS[:4],
            *fallback_rows,
        ]
    )
    valid_unsupported_evidence_rows = sorted(
        [
            *valid_unsupported_rows,
            *valid_selected_fallback_rows,
            SYNTHETIC_OUTPUT_ROWS[0],
            SYNTHETIC_OUTPUT_ROWS[4],
        ]
    )
    invalid_unsupported_evidence_rows = sorted(
        [
            *invalid_unsupported_rows,
            *invalid_selected_fallback_rows,
            SYNTHETIC_OUTPUT_ROWS[0],
            SYNTHETIC_OUTPUT_ROWS[4],
        ]
    )
    return {
        "current_llamacpp_slot_erase_release_authority": {
            "status": "fallback",
            "units": UNIT_IDS,
            "fallbacks": all_fallbacks,
            "bindings": all_bindings,
            "rows": sorted([*SOURCE_OUTPUT_ROWS, *fallback_rows]),
            "limitations": [
                (
                    "The current slot-erase path resets logical prompt state but does "
                    "not validate acknowledgements, observe causal physical release, "
                    "or reconcile the ledger; it remains fallback-only."
                )
            ],
        },
        "llamacpp_rocm_native_physical_soft_release": {
            "status": "deferred",
            "units": [ROCM_UNIT_ID],
            "fallbacks": sorted(ROCM_FALLBACKS.values()),
            "bindings": [bindings[ROCM_UNIT_ID]],
            "rows": sorted(
                [
                    *native_source_rows,
                    NATIVE_OUTPUT_ROWS[0],
                    *fallback_rows[:2],
                ]
            ),
            "limitations": [
                (
                    "Native ROCm physical-release execution is deferred to the "
                    "Hatchery campaign; without causal physical and ledger proof, "
                    "both exact pressure fallbacks remain active."
                )
            ],
        },
        "llamacpp_vulkan_native_physical_soft_release": {
            "status": "deferred",
            "units": [VULKAN_UNIT_ID],
            "fallbacks": sorted(VULKAN_FALLBACKS.values()),
            "bindings": [bindings[VULKAN_UNIT_ID]],
            "rows": sorted(
                [
                    *native_source_rows,
                    NATIVE_OUTPUT_ROWS[1],
                    *fallback_rows[2:],
                ]
            ),
            "limitations": [
                (
                    "Native Vulkan physical-release execution is deferred to later "
                    "promotion work; without backend-specific proof, both exact "
                    "pressure fallbacks remain active."
                )
            ],
        },
        "synthetic_unsupported_valid_reporting_fallback_contract": {
            "status": "fallback",
            "units": UNIT_IDS,
            "fallbacks": all_fallbacks,
            "bindings": all_bindings,
            "rows": valid_unsupported_evidence_rows,
            "limitations": [
                (
                    "Injected unsupported and no-op outcomes with valid coherent "
                    "reporting evidence prove report-only fallback selection and "
                    "claim preservation only; they establish no native-backend or "
                    "physical-resource behavior."
                )
            ],
        },
        "synthetic_unsupported_invalid_reporting_fallback_contract": {
            "status": "fallback",
            "units": UNIT_IDS,
            "fallbacks": all_fallbacks,
            "bindings": all_bindings,
            "rows": invalid_unsupported_evidence_rows,
            "limitations": [
                (
                    "Missing, stale, skewed, unhealthy, incomplete, or incoherent "
                    "reporting evidence on injected unsupported and no-op outcomes "
                    "selects disabled-invalid-evidence and preserves claims; it "
                    "proves no native or physical behavior."
                )
            ],
        },
        "synthetic_verified_soft_release_contract": {
            "status": "passed",
            "units": UNIT_IDS,
            "fallbacks": all_fallbacks,
            "bindings": all_bindings,
            "rows": verified_rows,
            "limitations": [
                (
                    "Injected exact observations prove verifier and ledger-transition "
                    "semantics only; they grant no physical, catalog, or runtime "
                    "authority."
                )
            ],
        },
    }


def require_result_identity(repo_root: Path, result: dict) -> None:
    require(
        set(result)
        == {
            "schema",
            "task_id",
            "prototype_id",
            "task_base_commit",
            "source",
            "outcome",
            "runtime_authority",
            "fallback_state",
            "observations",
            "claims",
        },
        "TASK-017 result fields are not closed",
    )
    require(
        result["schema"] == "residency_prototype_result/v1",
        "result schema changed",
    )
    require(result["task_id"] == TASK_ID, "TASK-017 result changed its task ID")
    require(
        result["task_base_commit"] == TASK_BASE,
        "TASK-017 result changed its task base",
    )
    require(
        result["prototype_id"] == PROTOTYPE_ID,
        "TASK-017 result changed its prototype identity",
    )
    require(
        EXPECTED_SOURCE_SHA256 != SOURCE_SHA256_PLACEHOLDER,
        "TASK-017 expected source SHA-256 placeholder has not been replaced",
    )
    source = repo_root / SOURCE_PATH
    require(
        result["source"] == {"path": SOURCE_PATH, "sha256": EXPECTED_SOURCE_SHA256},
        "TASK-017 result does not bind its source bytes",
    )
    require(
        hashlib.sha256(source.read_bytes()).hexdigest() == EXPECTED_SOURCE_SHA256,
        "TASK-017 source differs from the audited red-seam bytes",
    )
    require(result["outcome"] == "mixed", "TASK-017 result must remain mixed")
    require(
        result["runtime_authority"] == "none",
        "TASK-017 prototype granted runtime authority",
    )
    require(
        result["fallback_state"] == {"authority": "legacy_runtime", "status": "active"},
        "TASK-017 prototype changed the active fallback authority",
    )


def require_result_claims(
    result: dict, observation_id: str, inventory_contract: dict
) -> None:
    expected = claim_expectations(inventory_contract)
    claims = {claim["id"]: claim for claim in result["claims"]}
    require(len(claims) == len(result["claims"]), "TASK-017 claim IDs must be unique")
    require(set(claims) == set(expected), "TASK-017 result changed its claim closure")
    for claim_id, contract_row in expected.items():
        claim = claims[claim_id]
        require(
            set(claim)
            == {
                "id",
                "status",
                "platforms",
                "affected_units",
                "fallback_ids",
                "fallback_bindings",
                "evidence",
                "limitations",
            },
            f"{claim_id} fields are not closed",
        )
        require(claim["status"] == contract_row["status"], f"{claim_id} changed status")
        require(claim["platforms"] == ["linux"], f"{claim_id} changed platform scope")
        require(
            claim["affected_units"] == contract_row["units"],
            f"{claim_id} changed its promotion-unit closure",
        )
        require(
            claim["fallback_ids"] == contract_row["fallbacks"],
            f"{claim_id} changed its fallback closure",
        )
        require(
            claim["fallback_bindings"] == contract_row["bindings"],
            f"{claim_id} cross-wired an inventory fallback",
        )
        require(
            claim["evidence"]
            == [{"observation_id": observation_id, "rows": contract_row["rows"]}],
            f"{claim_id} changed its exact observed evidence",
        )
        require(
            claim["limitations"] == contract_row["limitations"],
            f"{claim_id} changed its bounded limitation",
        )


def require_result(repo_root: Path):
    contract = load_result_contract(repo_root)
    inventory_contract = require_inventory_contract(repo_root)
    result = contract.load_task_result(repo_root, TASK_ID)
    result_root = repo_root / "docs/research/residency-prototype-results/sha256"
    matches = []
    for path in sorted(result_root.glob("*.json")):
        raw = path.read_bytes()
        require(
            path.stem == hashlib.sha256(raw).hexdigest(),
            f"{path.name} is not addressed by bytes",
        )
        candidate = parse_json_object(raw)
        if candidate.get("task_id") == TASK_ID:
            matches.append(candidate)
    require(matches == [result], "TASK-017 must have one byte-addressed result")
    canonical = (
        json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    require(
        any(path.read_bytes() == canonical for path in result_root.glob("*.json")),
        "TASK-017 result is not canonical UTF-8 JSON",
    )
    require_result_identity(repo_root, result)
    observations = result["observations"]
    require(len(observations) == 1, "TASK-017 must publish one probe observation")
    observation = observations[0]
    require(
        observation["environment"]["platform"] == "linux",
        "TASK-017 initial observation must remain Linux-scoped",
    )
    require(
        observation["output"]["rows"] == expected_output_rows("linux"),
        "TASK-017 initial observation changed its exact row order or closure",
    )
    require_result_claims(result, observation["id"], inventory_contract)
    return contract, result, inventory_contract


def require_documented_source_boundary(repo_root: Path) -> None:
    slot_docs = (repo_root / "docs/api/llamacpp.md").read_text(encoding="utf-8")
    slot_section = slot_docs.split("## `POST /v1/slots/{id}?action=erase`", 1)
    require(len(slot_section) == 2, "slot-erase documentation section is unavailable")
    erase_text = slot_section[1].split("## ", 1)[0]
    require(
        "Erase (clear) the prompt cache of a specific slot." in erase_text
        and "removes all cached context" in erase_text,
        "slot-erase documentation changed its logical-cache contract",
    )
    matrix = (
        repo_root / "docs/research/portable-residency-capability-matrix.md"
    ).read_text(encoding="utf-8")
    require(
        "perform only cataloged soft/hard actions, and verify release before credit"
        in matrix,
        "PRE verified-release-before-credit policy changed",
    )


def require_live_source_audit(repo_root: Path) -> None:
    for relative_path, expected_sha256 in TASK_BASE_LIVE_SOURCE_SHA256.items():
        path = repo_root / relative_path
        require(
            path.is_file(), f"TASK_BASE live source is unavailable: {relative_path}"
        )
        require(
            hashlib.sha256(path.read_bytes()).hexdigest() == expected_sha256,
            f"TASK_BASE live source provenance changed: {relative_path}",
        )
    require_documented_source_boundary(repo_root)


def require_prototype_preprocessor_surface(source_text: str) -> None:
    include_directive = re.compile(r"\s*#\s*include\b(.*)")
    standard_include = re.compile(r"\s*<([a-z0-9_./]+)>\s*")
    includes = []
    platform_directives = []
    for line in source_text.splitlines():
        directive = include_directive.fullmatch(line)
        if directive is not None:
            match = standard_include.fullmatch(directive.group(1))
            require(match is not None, "prototype has a non-standard include")
            includes.append(match.group(1))
        elif line.lstrip().startswith("#"):
            platform_directives.append(line.strip())
    require(includes, "prototype has no declared standard-library surface")
    require(
        set(includes) <= ALLOWED_STANDARD_HEADERS
        and len(includes) == len(set(includes)),
        "prototype changed its bounded standard-library header surface",
    )
    require("iostream" in includes, "prototype output header is unavailable")
    require(
        platform_directives == ALLOWED_PLATFORM_DIRECTIVES,
        "prototype changed its closed platform-directive shape",
    )
    require(
        source_text.count("#") == len(includes) + len(platform_directives),
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
        "prototype contains a comment-obscured token",
    )
    require(
        "%:" not in source_text and "??=" not in source_text,
        "prototype contains an alternate preprocessor token",
    )
    require_prototype_preprocessor_surface(source_text)
    standard_symbols = set(re.findall(r"\bstd\s*::\s*([a-z_][a-z0-9_]*)\b", lowered))
    require(
        standard_symbols <= ALLOWED_STANDARD_SYMBOLS and "cout" in standard_symbols,
        "prototype changed its bounded standard-library symbol surface",
    )
    for pattern in FORBIDDEN_PROTOTYPE_SOURCE_PATTERNS:
        require(
            pattern.search(lowered) is None,
            f"prototype contains forbidden effect API {pattern.pattern}",
        )


def require_probe_source(source: Path) -> None:
    source_bytes = source.read_bytes()
    require(
        EXPECTED_SOURCE_SHA256 != SOURCE_SHA256_PLACEHOLDER,
        "TASK-017 expected source SHA-256 placeholder has not been replaced",
    )
    require(
        hashlib.sha256(source_bytes).hexdigest() == EXPECTED_SOURCE_SHA256,
        "prototype differs from the audited red-seam bytes",
    )
    require_prototype_source_semantics(source_bytes)


def current_platform() -> str:
    return {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(
        platform.system(), ""
    )


def parse_probe_output(stdout: bytes) -> tuple[list[str], dict[str, str]]:
    require(stdout.endswith(b"\n"), "prototype probe stdout lacks its final newline")
    lines = stdout.decode("utf-8").splitlines()
    require(lines, "prototype probe emitted no observation rows")
    rows = {}
    for line in lines:
        require(line.count("=") == 1, f"prototype emitted a malformed row: {line}")
        key, value = line.split("=", 1)
        require(key and value, f"prototype emitted an empty row field: {line}")
        require(key not in rows, f"prototype emitted duplicate row: {key}")
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
    observations = [
        observation
        for observation in result["observations"]
        if observation["id"] in observed_ids
        and observation["environment"]["platform"] == platform_id
    ]
    require(
        len(observations) == 1,
        "current passed/fallback claims do not select one recorded observation",
    )
    return observations[0]


def run_probe(repo_root: Path, source: Path, recorded_observation):
    platform_id = current_platform()
    require(platform_id, "prototype probe ran on an unsupported platform")
    compiler = (
        recorded_observation["toolchain"]["compiler"]
        if recorded_observation is not None
        else os.environ.get("CXX", "cl" if platform_id == "windows" else "c++")
    )
    version = compiler_version(compiler, platform_id)
    suffix = ".exe" if platform_id == "windows" else ""
    executable_name = f"task017{suffix}"
    logical_source = source.relative_to(repo_root).as_posix()
    logical_output = f"$TMPDIR/{executable_name}"
    logical_compile_command = compiler_command(
        compiler, logical_source, logical_output, platform_id
    )
    with tempfile.TemporaryDirectory(prefix="residency-task017-") as directory:
        executable = Path(directory) / executable_name
        actual_compile_command = compiler_command(
            compiler, str(source), str(executable), platform_id
        )
        subprocess.run(actual_compile_command, cwd=directory, check=True, timeout=30)
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
        "compile_command": logical_compile_command,
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


def observation_body(observation: dict) -> dict:
    body = dict(observation)
    body.pop("id")
    return body


def require_observation_binding(recorded_observation, binding: dict) -> None:
    if recorded_observation is None:
        return
    require(
        observation_body(recorded_observation) == binding,
        "current passed/fallback claims are not bound to exact probe provenance",
    )


def require_exact_probe_rows(
    lines: list[str], rows: dict[str, str], platform_id: str
) -> None:
    expected = expected_output_rows(platform_id)
    require(len(expected) == 287, "TASK-017 row cardinality changed")
    require(len(NEGATIVE_OUTPUT_ROWS) == 118, "TASK-017 negative cardinality changed")
    require(lines == expected, "prototype changed its exact row order or closure")
    expected_keys = {row.split("=", 1)[0] for row in expected}
    require(set(rows) == expected_keys, "prototype changed its observation keys")


def require_slot_semantics(rows: dict[str, str]) -> None:
    require(
        rows["slot.request_count"] == "2" and rows["slot.request_ids"] == "3.7",
        "synthetic slot request changed",
    )
    require(
        rows["slot.ack_count"] == rows["slot.request_count"]
        and rows["slot.ack_ids"] == rows["slot.request_ids"]
        and rows["slot.ack_match"] == "passed",
        "synthetic slot acknowledgement is not exact",
    )


def require_identity_and_observation_semantics(rows: dict[str, str]) -> None:
    for binding_name in (
        *RUNTIME_BINDING_NAMES,
        "resident_id",
        "resident_generation",
        "backend_instance_birth_token",
        "topology_generation",
        "allocation_group_id",
        "ledger_generation",
        "action_lease",
        "observation_contract_digest",
        "pre_observation_generation",
    ):
        require(
            rows[f"identity.{binding_name}"] == "required",
            f"identity token {binding_name} is not required",
        )
    require(
        rows["identity.action_lease"] == "required"
        and rows["identity.action_lease_binding"] == "matched",
        "action lease is present but not value-bound",
    )
    require(
        rows["identity.action_lease_state"] == "idle_soft_reclaiming"
        and rows["identity.action_lease_operation"] == "pressure_soft_release"
        and rows["identity.action_lease_slot_set_binding"] == "matched"
        and rows["identity.action_lease_claim_generation_binding"] == "matched",
        "action lease is not bound to the idle soft-release operation and claims",
    )
    require(
        rows["identity.observation_contract_digest"] == "required"
        and rows["identity.post_observation_generation"] == "derived_checked"
        and int(rows["identity.required_token_count"]) == REQUIRED_IDENTITY_TOKEN_COUNT
        and rows["identity.required_tokens_nonzero"] == "passed"
        and rows["identity.exact_match"] == "passed",
        "required identity-token completeness is unproved",
    )
    for phase in ("before", "after"):
        for field in ("present", "fresh", "skew", "healthy", "complete"):
            require(
                rows[f"observation.{phase}_{field}"] == "passed",
                f"{phase} observation {field} proof is unavailable",
            )
    generation_before = int(rows["observation.generation_before"])
    generation_after = int(rows["observation.generation_after"])
    require(
        generation_after == generation_before + 1
        and rows["observation.generation_increment"] == "passed",
        "observation generation did not advance exactly once",
    )


def require_physical_semantics(rows: dict[str, str]) -> None:
    cache_gtt_before = int(rows["physical.cache_gtt_before_bytes"])
    cache_gtt_after = int(rows["physical.cache_gtt_after_bytes"])
    cache_gtt_released = int(rows["physical.cache_gtt_released_bytes"])
    cache_host_before = int(rows["physical.cache_host_before_bytes"])
    cache_host_after = int(rows["physical.cache_host_after_bytes"])
    cache_host_released = int(rows["physical.cache_host_released_bytes"])
    global_gtt_before = int(rows["physical.global_gtt_headroom_before_bytes"])
    global_gtt_after = int(rows["physical.global_gtt_headroom_after_bytes"])
    global_host_before = int(rows["physical.global_host_headroom_before_bytes"])
    global_host_after = int(rows["physical.global_host_headroom_after_bytes"])
    unrelated_gtt = int(rows["physical.unrelated_gtt_growth_bytes"])
    unrelated_host = int(rows["physical.unrelated_host_growth_bytes"])
    global_gtt = int(rows["physical.global_gtt_headroom_improvement_bytes"])
    global_host = int(rows["physical.global_host_headroom_improvement_bytes"])
    require(
        cache_gtt_before - cache_gtt_after == cache_gtt_released == 2048,
        "causal GTT release arithmetic changed",
    )
    require(
        cache_host_before - cache_host_after == cache_host_released == 1024,
        "causal host release arithmetic changed",
    )
    require(
        rows["physical.global_gtt_headroom_before_present"] == "passed"
        and rows["physical.global_gtt_headroom_after_present"] == "passed"
        and rows["physical.global_host_headroom_before_present"] == "passed"
        and rows["physical.global_host_headroom_after_present"] == "passed",
        "global headroom knownness is unavailable",
    )
    require(
        global_gtt_after - global_gtt_before == global_gtt
        and global_host_after - global_host_before == global_host,
        "global headroom observations do not bind the checked improvements",
    )
    require(
        global_gtt == cache_gtt_released - unrelated_gtt == 1536,
        "global GTT delta no longer carries unrelated growth",
    )
    require(
        global_host == cache_host_released - unrelated_host == 768,
        "global host delta no longer carries unrelated growth",
    )
    require(
        global_gtt != cache_gtt_released and global_host != cache_host_released,
        "synthetic control no longer distinguishes global and owned release",
    )
    require(
        rows["physical.global_headroom_delta_checked"] == "passed",
        "global headroom delta was not checked before subtraction",
    )
    require(
        rows["physical.cache_group_zero"] == "passed"
        and cache_gtt_after == 0
        and cache_host_after == 0,
        "released cache allocation group is not empty",
    )


def require_causal_and_resident_semantics(rows: dict[str, str]) -> None:
    require(
        rows["causal.owned_gtt_release_bytes"]
        == rows["physical.cache_gtt_released_bytes"]
        and rows["causal.owned_host_release_bytes"]
        == rows["physical.cache_host_released_bytes"],
        "causal release differs from the owned allocation-group delta",
    )
    require(
        rows["causal.global_delta_not_credited"] == "passed"
        and rows["causal.effect_binding"] == "matched",
        "global change was credited without causal effect binding",
    )
    for key in (
        "resident.process_identity",
        "resident.weights",
        "resident.pin",
        "resident.model_residency",
    ):
        require(rows[key] == "preserved", f"soft release changed {key}")
    require(
        rows["resident.pin_before_known"] == "passed"
        and rows["resident.pin_after_known"] == "passed"
        and rows["resident.pin_soft_release"] == "allowed",
        "pin incorrectly vetoed the non-destructive soft release",
    )


def require_ledger_semantics(rows: dict[str, str]) -> None:
    require(
        int(rows["ledger.before_claim_groups"]) == 2
        and int(rows["ledger.after_claim_groups"]) == 1,
        "ledger claim-group transition changed",
    )
    require(
        rows["ledger.before_group_0_id"] == "model_weights"
        and rows["ledger.before_group_0_effect"] == "persistent_weights"
        and rows["ledger.after_group_0_id"] == "model_weights"
        and rows["ledger.after_group_0_effect"] == "persistent_weights",
        "persistent claim-group identity or effect changed",
    )
    require(
        rows["ledger.before_group_0_gtt_bytes"]
        == rows["ledger.after_group_0_gtt_bytes"]
        == "8192"
        and rows["ledger.before_group_0_host_bytes"]
        == rows["ledger.after_group_0_host_bytes"]
        == "4096",
        "persistent claim-group bytes changed",
    )
    require(
        rows["ledger.before_group_1_id"] == "slot_cache"
        and rows["ledger.before_group_1_effect"] == "reconstructible_state"
        and rows["ledger.before_group_1_gtt_bytes"]
        == rows["physical.cache_gtt_before_bytes"]
        and rows["ledger.before_group_1_host_bytes"]
        == rows["physical.cache_host_before_bytes"],
        "released claim group is not bound to the observed cache allocation group",
    )
    require(
        rows["ledger.after_slot_cache"] == "absent"
        and rows["ledger.removed_group_count"] == "1"
        and rows["ledger.other_claims"] == "unchanged",
        "ledger did not remove exactly one target group while preserving others",
    )
    require(
        rows["ledger.released_claim_group"] == "reconstructible_state"
        and rows["ledger.retained_claim_group"] == "persistent_weights",
        "ledger released or retained the wrong claim group",
    )
    ledger_generation_before = int(rows["ledger.generation_before"])
    ledger_generation_after = int(rows["ledger.generation_after"])
    require(
        ledger_generation_after == ledger_generation_before + 1
        and rows["ledger.generation_increment"] == "passed",
        "ledger generation did not advance exactly once",
    )
    require(
        int(rows["ledger.credit_before_ack_bytes"]) == 0
        and int(rows["ledger.credit_before_physical_verification_bytes"]) == 0,
        "ledger credited release before proof closure",
    )
    require(
        rows["ledger.credited_gtt_bytes"] == rows["causal.owned_gtt_release_bytes"]
        and rows["ledger.credited_host_bytes"]
        == rows["causal.owned_host_release_bytes"],
        "ledger credit differs from causal owned release",
    )
    require(
        rows["ledger.reconciliation_after_verification"] == "passed"
        and rows["ledger.atomic_commit"] == "passed",
        "ledger transition did not follow verified physical release atomically",
    )


def require_fail_closed_semantics(rows: dict[str, str]) -> None:
    for row in NEGATIVE_OUTPUT_ROWS:
        key, expected = row.split("=", 1)
        require(rows[key] == expected, f"{key} did not fail closed")
    require(
        rows["negative.ack_missing"] == "quarantine",
        "missing acknowledgement after completed dispatch did not quarantine",
    )
    require(
        rows["negative.slot_count_oversized"] == "unknown"
        and rows["negative.dispatch_completed_without_attempt"] == "quarantine"
        and rows["negative.invalid_precondition_after_dispatch_attempt"] == "quarantine"
        and rows["negative.valid_precondition_without_dispatch"] == "verified_intact"
        and rows["negative.valid_precondition_without_dispatch_changed_post"]
        == "quarantine"
        and rows["negative.unsupported_mechanism_after_dispatch_attempt"]
        == "quarantine"
        and rows["negative.unsupported_mechanism_without_dispatch_changed_post"]
        == "quarantine",
        "contradictory dispatch combinations did not fail closed",
    )
    require(
        rows["negative.process_identity_missing"] == "quarantine"
        and rows["negative.process_identity_zero"] == "quarantine"
        and rows["negative.process_identity_changed"] == "quarantine"
        and rows["negative.weights_identity_missing"] == "quarantine"
        and rows["negative.weights_identity_zero"] == "quarantine"
        and rows["negative.weights_identity_changed"] == "quarantine"
        and rows["negative.model_residency_identity_missing"] == "quarantine"
        and rows["negative.model_residency_identity_zero"] == "quarantine"
        and rows["negative.model_residency_identity_changed"] == "quarantine"
        and rows["negative.pin_missing"] == "quarantine"
        and rows["negative.pin_changed"] == "quarantine",
        "resident presence or preservation faults did not quarantine",
    )
    require(
        rows["negative.action_lease_identity_mismatch"] == "unknown"
        and rows["negative.action_lease_evidence_liveness_lease_mismatch"] == "unknown",
        "one-sided action-lease identity mismatches did not fail closed",
    )
    require(
        rows["negative.ack_without_physical_delta_each_axis_changed"] == "unknown",
        "acknowledgement no-effect axes lack independent MCDC closure",
    )
    dimension_rows = (
        "negative.gtt_release_partial",
        "negative.host_release_partial",
        "negative.gtt_cache_nonzero_after",
        "negative.host_cache_nonzero_after",
        "negative.ledger_weights_gtt_bytes_mismatch",
        "negative.ledger_weights_host_bytes_mismatch",
        "negative.ledger_cache_gtt_bytes_mismatch",
        "negative.ledger_cache_host_bytes_mismatch",
        "negative.gtt_release_exceeds_maximum",
        "negative.host_release_exceeds_maximum",
        "negative.gtt_causal_mismatch",
        "negative.host_causal_mismatch",
        "negative.gtt_causal_checked_add_overflow",
        "negative.host_causal_checked_add_overflow",
        "negative.gtt_global_headroom_subtraction_underflow",
        "negative.host_global_headroom_subtraction_underflow",
        "negative.gtt_cache_release_subtraction_underflow",
        "negative.host_cache_release_subtraction_underflow",
    )
    require(
        all(rows[key] == "unknown" for key in dimension_rows),
        "GTT and host negative dimensions are not independently closed",
    )
    presence_rows = (
        "negative.observation_before_gtt_effect_missing",
        "negative.observation_before_host_effect_missing",
        "negative.ledger_weights_gtt_measurement_missing",
        "negative.ledger_weights_host_measurement_missing",
        "negative.ledger_cache_gtt_measurement_missing",
        "negative.ledger_cache_host_measurement_missing",
        "negative.unrelated_gtt_growth_missing",
        "negative.unrelated_host_growth_missing",
        "negative.global_gtt_headroom_missing",
        "negative.global_host_headroom_missing",
    )
    require(
        all(rows[key] == "unknown" for key in presence_rows),
        "typed effect or attribution presence closure weakened",
    )
    require(
        rows["disposition.pre_dispatch_failure"] == "verified_intact"
        and rows["disposition.pre_dispatch_post_observation"] == "unchanged_fresh"
        and rows["disposition.post_dispatch_ambiguous"] == "quarantine"
        and rows["disposition.post_dispatch_observation"] == "unavailable"
        and rows["disposition.invalid_post_residency"] == "not_preserved"
        and rows["disposition.ambiguous_claims"] == "maximum",
        "failure disposition weakened claim preservation",
    )
    require(
        rows["disposition.ack_without_effect"] == "verified_intact"
        and rows["disposition.ack_without_effect_ledger"] == "unchanged"
        and rows["disposition.ack_without_effect_credit_bytes"] == "0"
        and rows["disposition.ack_without_effect_post_observation"]
        == "unchanged_fresh",
        "coherent acknowledgement without effect was not verified intact",
    )
    require(
        rows["unsupported.capability_absent"] == "detected"
        and rows["unsupported.successful_noop"] == "detected"
        and rows["unsupported.dispatch_calls"] == "0",
        "unsupported mechanism was dispatched or misclassified",
    )
    require(
        rows["unsupported.ledger"] == "unchanged"
        and rows["unsupported.residency"] == "preserved"
        and rows["unsupported.post_observation"] == "unchanged_fresh"
        and rows["unsupported.pressure_mode"] == "report_only",
        "unsupported mechanism did not preserve fallback state",
    )
    require(
        rows["unsupported.reporting_valid"] == "report_only"
        and all(
            rows[f"unsupported.reporting_{state}"] == "disabled_invalid_evidence"
            for state in (
                "missing",
                "stale",
                "skew",
                "unhealthy",
                "incomplete",
                "incoherent",
            )
        ),
        "unsupported reporting evidence selected the wrong fallback branch",
    )
    require(
        rows["unsupported.invalid_reporting_dispatch_calls"] == "0"
        and rows["unsupported.invalid_reporting_post_observation"] == "unchanged_fresh"
        and rows["unsupported.invalid_reporting_ledger"] == "unchanged"
        and rows["unsupported.invalid_reporting_residency"] == "preserved",
        "invalid reporting fallback changed residency or dispatch state",
    )
    selected_fallbacks = {
        row.split("=", 1)[0]: row.split("=", 1)[1]
        for row in SELECTED_FALLBACK_OUTPUT_ROWS
    }
    require(
        all(rows[key] == value for key, value in selected_fallbacks.items()),
        "computed fallback selection changed",
    )
    require(
        rows["synthetic.fail_closed_matrix"] == "passed"
        and rows["synthetic.negative_fixture_shapes"] == "passed",
        "synthetic negative matrix or fixture-shape closure did not pass",
    )


def require_native_boundary(rows: dict[str, str]) -> None:
    require(
        rows["source.current_release_authority"] == "fallback",
        "current source escaped fallback without native proof",
    )
    require(
        rows["native.llamacpp_rocm_physical_release"] == "deferred"
        and rows["native.llamacpp_vulkan_physical_release"] == "deferred",
        "native physical-release deferral changed",
    )
    expected_fallbacks = {
        row.split("=", 1)[0]: row.split("=", 1)[1] for row in fallback_output_rows()
    }
    require(
        all(rows[key] == value for key, value in expected_fallbacks.items()),
        "active PRE fallback binding changed",
    )
    require(rows["runtime_authority"] == "none", "prototype granted authority")


def require_probe_semantics(rows: dict[str, str]) -> None:
    require_slot_semantics(rows)
    require_identity_and_observation_semantics(rows)
    require_physical_semantics(rows)
    require_causal_and_resident_semantics(rows)
    require_ledger_semantics(rows)
    require_fail_closed_semantics(rows)
    require_native_boundary(rows)


def require_probe(repo_root: Path, result: dict) -> None:
    source = repo_root / SOURCE_PATH
    require_probe_source(source)
    platform_id = current_platform()
    recorded_observation = recorded_observation_for_platform(result, platform_id)
    completed, lines, rows, binding = run_probe(repo_root, source, recorded_observation)
    require(completed.returncode == 0, "prototype probe returned a nonzero exit code")
    require(completed.stderr == b"", "prototype probe emitted stderr")
    require_exact_probe_rows(lines, rows, platform_id)
    require_probe_semantics(rows)
    require(
        rows.get("platform.current") == platform_id,
        "prototype did not bind its current platform",
    )
    require_observation_binding(recorded_observation, binding)


def require_cmake_and_plan(repo_root: Path) -> None:
    cmake = (repo_root / "CMakeLists.txt").read_text(encoding="utf-8")
    cmake_block = "\n".join(
        [
            "set(_RESIDENCY_PROTOTYPE_TASK017_TEST_SRC",
            f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/{SOURCE_PATH}"',
            ")",
            'if(BUILD_TESTING AND EXISTS "${_RESIDENCY_PROTOTYPE_TASK017_TEST_SRC}")',
            "    add_executable(test_residency_prototype_task017",
            "        ${_RESIDENCY_PROTOTYPE_TASK017_TEST_SRC}",
            "    )",
            (
                "    add_cpp_ci_test(ResidencyPrototypeContractTask017 CI ON COMMAND "
                "test_residency_prototype_task017)"
            ),
            (
                "    set_tests_properties(ResidencyPrototypeContractTask017 PROPERTIES "
                "TIMEOUT 45)"
            ),
            "endif()",
        ]
    )
    require(
        cmake.count(cmake_block) == 1,
        "TASK-017 CMake declaration is not one closed BUILD_TESTING block",
    )
    require(cmake.count(SOURCE_PATH) == 1, "TASK-017 C++ source is not registered once")

    plan = (repo_root / "plan/architecture-portable-residency-1.md").read_text(
        encoding="utf-8"
    )
    task_row = next(
        (line for line in plan.splitlines() if line.startswith("| TASK-017 |")), ""
    )
    require(
        task_row.endswith("| ✅ | 2026-08-16 |"),
        "TASK-017 is not recorded complete",
    )
    output_rows = [
        line for line in plan.splitlines() if line.startswith("| TASK-014–TASK-018 |")
    ]
    require(
        len(output_rows) == 1,
        "Phase-2 output ownership row is unavailable or ambiguous",
    )
    output_row = output_rows[0]
    for required_text in (
        "CMakeLists.txt",
        SOURCE_PATH,
        "test/residency/prototypes/",
        "docs/research/residency-prototype-results/",
        "no production authority",
    ):
        require(
            required_text in output_row,
            f"Phase-2 output ownership row omits {required_text}",
        )


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    required = (
        repo_root / "test/residency/prototypes/result_contract.py",
        repo_root / SOURCE_PATH,
    )
    if not all(path.is_file() for path in required):
        fail_unavailable()
    require_live_source_audit(repo_root)
    _, result, _ = require_result(repo_root)
    require_probe(repo_root, result)
    require_cmake_and_plan(repo_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
