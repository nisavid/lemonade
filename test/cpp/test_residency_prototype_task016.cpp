#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace lemon::residency::prototype {

enum class BoundState {
    not_applicable,
    known_zero,
    bounded,
    unknown,
};

enum class UnknownReason {
    none,
    missing,
    overflow,
    identity_mismatch,
    excluded_configuration,
    incomplete_manifest,
    caller_reservation_missing,
    caller_reservation_undersized,
    caller_reservation_stale_or_mismatched,
    capability_budget_exceeded,
};

struct Bytes {
    std::uint64_t value;
};

struct ByteBound {
    BoundState state;
    Bytes upper_bound;
    UnknownReason reason;
};

ByteBound not_applicable_bound() {
    return ByteBound{BoundState::not_applicable, Bytes{0}, UnknownReason::none};
}

ByteBound known_zero_bound() {
    return ByteBound{BoundState::known_zero, Bytes{0}, UnknownReason::none};
}

ByteBound bounded(Bytes upper_bound) {
    return ByteBound{BoundState::bounded, upper_bound, UnknownReason::none};
}

ByteBound unknown_bound(UnknownReason reason) {
    return ByteBound{BoundState::unknown, Bytes{0}, reason};
}

bool has_upper_bound(const ByteBound& bound) {
    return bound.state == BoundState::known_zero ||
           bound.state == BoundState::bounded;
}

bool is_unknown(const ByteBound& bound, UnknownReason reason) {
    return bound.state == BoundState::unknown && bound.reason == reason;
}

ByteBound checked_add(ByteBound left, ByteBound right) {
    if (!has_upper_bound(left)) {
        return left.state == BoundState::unknown
                   ? left
                   : unknown_bound(UnknownReason::missing);
    }
    if (!has_upper_bound(right)) {
        return right.state == BoundState::unknown
                   ? right
                   : unknown_bound(UnknownReason::missing);
    }
    if (right.upper_bound.value >
        std::numeric_limits<std::uint64_t>::max() - left.upper_bound.value) {
        return unknown_bound(UnknownReason::overflow);
    }
    return bounded(Bytes{left.upper_bound.value + right.upper_bound.value});
}

ByteBound checked_max(ByteBound left, ByteBound right) {
    if (!has_upper_bound(left)) {
        return left.state == BoundState::unknown
                   ? left
                   : unknown_bound(UnknownReason::missing);
    }
    if (!has_upper_bound(right)) {
        return right.state == BoundState::unknown
                   ? right
                   : unknown_bound(UnknownReason::missing);
    }
    return bounded(Bytes{
        left.upper_bound.value > right.upper_bound.value
            ? left.upper_bound.value
            : right.upper_bound.value,
    });
}

template <typename T>
struct Projection {
    bool known;
    T value;
    UnknownReason reason;
};

template <typename T>
Projection<T> known_projection(T value) {
    return Projection<T>{true, value, UnknownReason::none};
}

template <typename T>
Projection<T> unknown_projection(UnknownReason reason) {
    return Projection<T>{false, T{}, reason};
}

struct ConstraintVector {
    ByteBound gtt_mapping;
    ByteBound host_memavailable;
    ByteBound physical_system;
};

ConstraintVector zero_vector() {
    return ConstraintVector{
        known_zero_bound(),
        known_zero_bound(),
        known_zero_bound(),
    };
}

ConstraintVector project_shared_gtt(ByteBound physical_bytes) {
    return ConstraintVector{physical_bytes, physical_bytes, physical_bytes};
}

ConstraintVector checked_add(
    const ConstraintVector& left, const ConstraintVector& right) {
    return ConstraintVector{
        checked_add(left.gtt_mapping, right.gtt_mapping),
        checked_add(left.host_memavailable, right.host_memavailable),
        checked_add(left.physical_system, right.physical_system),
    };
}

ConstraintVector checked_max(
    const ConstraintVector& left, const ConstraintVector& right) {
    return ConstraintVector{
        checked_max(left.gtt_mapping, right.gtt_mapping),
        checked_max(left.host_memavailable, right.host_memavailable),
        checked_max(left.physical_system, right.physical_system),
    };
}

UnknownReason vector_unknown_reason(const ConstraintVector& vector) {
    for (const ByteBound* bound : {
             &vector.gtt_mapping,
             &vector.host_memavailable,
             &vector.physical_system,
         }) {
        if (!has_upper_bound(*bound)) {
            return bound->state == BoundState::unknown
                       ? bound->reason
                       : UnknownReason::missing;
        }
    }
    return UnknownReason::none;
}

bool vector_equals(const ConstraintVector& vector, std::uint64_t expected) {
    return vector_unknown_reason(vector) == UnknownReason::none &&
           vector.gtt_mapping.upper_bound.value == expected &&
           vector.host_memavailable.upper_bound.value == expected &&
           vector.physical_system.upper_bound.value == expected;
}

enum class RuntimeBinding : std::size_t {
    device_identity,
    backend_artifact_digest,
    source_build_dependency_closure,
    driver_runtime_closure,
    model_manifest_digest,
    normalized_configuration_digest,
    evidence_index_digest,
    evidence_liveness_lease,
    count,
};

constexpr std::size_t runtime_binding_count =
    static_cast<std::size_t>(RuntimeBinding::count);

struct RuntimeBindings {
    std::array<RuntimeBinding, runtime_binding_count> values;
    std::size_t count;
};

RuntimeBindings exact_runtime_bindings() {
    return RuntimeBindings{
        {
            RuntimeBinding::device_identity,
            RuntimeBinding::backend_artifact_digest,
            RuntimeBinding::source_build_dependency_closure,
            RuntimeBinding::driver_runtime_closure,
            RuntimeBinding::model_manifest_digest,
            RuntimeBinding::normalized_configuration_digest,
            RuntimeBinding::evidence_index_digest,
            RuntimeBinding::evidence_liveness_lease,
        },
        runtime_binding_count,
    };
}

Projection<bool> validate_runtime_bindings(const RuntimeBindings& bindings) {
    if (bindings.count != runtime_binding_count) {
        return unknown_projection<bool>(UnknownReason::identity_mismatch);
    }
    std::array<bool, runtime_binding_count> seen{};
    for (std::size_t index = 0; index < bindings.count; ++index) {
        const std::size_t binding =
            static_cast<std::size_t>(bindings.values[index]);
        if (binding >= runtime_binding_count || seen[binding]) {
            return unknown_projection<bool>(UnknownReason::identity_mismatch);
        }
        seen[binding] = true;
    }
    for (const bool present : seen) {
        if (!present) {
            return unknown_projection<bool>(UnknownReason::identity_mismatch);
        }
    }
    return known_projection(true);
}

enum class ExcludedConfiguration : std::size_t {
    multimodal_projector,
    draft_mtp_dflash_speculative,
    hf_load_remote,
    memory_affecting_recipe_arguments,
    prompt_slot_cache,
    unmodeled_concurrency,
    count,
};

constexpr std::size_t exclusion_count =
    static_cast<std::size_t>(ExcludedConfiguration::count);

struct Configuration {
    std::array<bool, exclusion_count> exclusions;
};

Projection<bool> validate_configuration(const Configuration& configuration) {
    for (const bool excluded : configuration.exclusions) {
        if (excluded) {
            return unknown_projection<bool>(
                UnknownReason::excluded_configuration);
        }
    }
    return known_projection(true);
}

enum class ComponentKind : std::size_t {
    model_tensors,
    persistent_context,
    compute_attention,
    output_buffer,
    load_staging,
    residual_envelope,
    count,
};

constexpr std::size_t component_count =
    static_cast<std::size_t>(ComponentKind::count);

enum class EffectKind : std::size_t {
    persistent_weights,
    fixed_cache,
    reconstructible_state,
    transient_workspace,
    allocator_reserve,
    host_effects,
    count,
};

constexpr std::size_t effect_kind_count =
    static_cast<std::size_t>(EffectKind::count);

enum class EffectScope {
    candidate_derivation,
    lifecycle_composition,
    sizing_operation,
};

struct EffectSet {
    std::array<bool, effect_kind_count> included;
};

void include_effect(EffectSet& effects, EffectKind effect) {
    effects.included[static_cast<std::size_t>(effect)] = true;
}

bool effect_sets_equal(const EffectSet& left, const EffectSet& right) {
    for (std::size_t index = 0; index < effect_kind_count; ++index) {
        if (left.included[index] != right.included[index]) {
            return false;
        }
    }
    return true;
}

EffectSet expected_effects(ComponentKind component) {
    EffectSet effects{};
    include_effect(effects, EffectKind::host_effects);
    switch (component) {
    case ComponentKind::model_tensors:
        include_effect(effects, EffectKind::persistent_weights);
        break;
    case ComponentKind::persistent_context:
        include_effect(effects, EffectKind::fixed_cache);
        break;
    case ComponentKind::compute_attention:
        include_effect(effects, EffectKind::reconstructible_state);
        break;
    case ComponentKind::output_buffer:
    case ComponentKind::load_staging:
        include_effect(effects, EffectKind::transient_workspace);
        break;
    case ComponentKind::residual_envelope:
        include_effect(effects, EffectKind::allocator_reserve);
        break;
    case ComponentKind::count:
        break;
    }
    return effects;
}

struct CandidateDerivation {
    EffectScope scope;
    ComponentKind component;
    ByteBound shared_gtt_physical_bytes;
    EffectSet effects;
};

struct LifecycleComposition {
    EffectScope scope;
    ConstraintVector overlap;
};

enum class SizingField : std::size_t {
    readonly_inputs,
    readonly_ranges,
    readonly_range_offset_bytes,
    readonly_range_length_bytes,
    readonly_write_bytes,
    host_heap_bytes,
    mmap_regions,
    mmap_bytes,
    page_cache_bytes,
    file_descriptors,
    additional_threads,
    duration_milliseconds,
    output_bytes,
    gtt_mapping_bytes,
    driver_allocation_bytes,
    process_spawn_calls,
    backend_spawn_calls,
    network_calls,
    device_context_initialization_calls,
    driver_open_calls,
    driver_ioctl_calls,
    count,
};

constexpr std::size_t sizing_field_count =
    static_cast<std::size_t>(SizingField::count);

constexpr std::array<SizingField, sizing_field_count> sizing_fields = {
    SizingField::readonly_inputs,
    SizingField::readonly_ranges,
    SizingField::readonly_range_offset_bytes,
    SizingField::readonly_range_length_bytes,
    SizingField::readonly_write_bytes,
    SizingField::host_heap_bytes,
    SizingField::mmap_regions,
    SizingField::mmap_bytes,
    SizingField::page_cache_bytes,
    SizingField::file_descriptors,
    SizingField::additional_threads,
    SizingField::duration_milliseconds,
    SizingField::output_bytes,
    SizingField::gtt_mapping_bytes,
    SizingField::driver_allocation_bytes,
    SizingField::process_spawn_calls,
    SizingField::backend_spawn_calls,
    SizingField::network_calls,
    SizingField::device_context_initialization_calls,
    SizingField::driver_open_calls,
    SizingField::driver_ioctl_calls,
};

struct SizingOperationManifest {
    EffectScope scope;
    std::array<ByteBound, sizing_field_count> fields;
};

ByteBound& manifest_field(
    SizingOperationManifest& manifest, SizingField field) {
    return manifest.fields[static_cast<std::size_t>(field)];
}

const ByteBound& manifest_field(
    const SizingOperationManifest& manifest, SizingField field) {
    return manifest.fields[static_cast<std::size_t>(field)];
}

bool field_requires_known_zero(SizingField field) {
    switch (field) {
    case SizingField::readonly_write_bytes:
    case SizingField::mmap_regions:
    case SizingField::mmap_bytes:
    case SizingField::page_cache_bytes:
    case SizingField::file_descriptors:
    case SizingField::additional_threads:
    case SizingField::gtt_mapping_bytes:
    case SizingField::driver_allocation_bytes:
    case SizingField::process_spawn_calls:
    case SizingField::backend_spawn_calls:
    case SizingField::network_calls:
    case SizingField::device_context_initialization_calls:
    case SizingField::driver_open_calls:
    case SizingField::driver_ioctl_calls:
        return true;
    case SizingField::readonly_inputs:
    case SizingField::readonly_ranges:
    case SizingField::readonly_range_offset_bytes:
    case SizingField::readonly_range_length_bytes:
    case SizingField::host_heap_bytes:
    case SizingField::duration_milliseconds:
    case SizingField::output_bytes:
    case SizingField::count:
        return false;
    }
    return false;
}

bool reservation_field_requires_exact(SizingField field) {
    switch (field) {
    case SizingField::readonly_inputs:
    case SizingField::readonly_ranges:
    case SizingField::readonly_range_offset_bytes:
    case SizingField::readonly_range_length_bytes:
        return true;
    case SizingField::readonly_write_bytes:
    case SizingField::host_heap_bytes:
    case SizingField::mmap_regions:
    case SizingField::mmap_bytes:
    case SizingField::page_cache_bytes:
    case SizingField::file_descriptors:
    case SizingField::additional_threads:
    case SizingField::duration_milliseconds:
    case SizingField::output_bytes:
    case SizingField::gtt_mapping_bytes:
    case SizingField::driver_allocation_bytes:
    case SizingField::process_spawn_calls:
    case SizingField::backend_spawn_calls:
    case SizingField::network_calls:
    case SizingField::device_context_initialization_calls:
    case SizingField::driver_open_calls:
    case SizingField::driver_ioctl_calls:
    case SizingField::count:
        return field_requires_known_zero(field);
    }
    return false;
}

std::uint64_t expected_field_value(SizingField field) {
    switch (field) {
    case SizingField::readonly_inputs:
    case SizingField::readonly_ranges:
        return 1;
    case SizingField::readonly_range_offset_bytes:
        return 0;
    case SizingField::readonly_range_length_bytes:
        return 4096;
    case SizingField::host_heap_bytes:
        return 256;
    case SizingField::duration_milliseconds:
        return 50;
    case SizingField::output_bytes:
        return 128;
    case SizingField::readonly_write_bytes:
    case SizingField::mmap_regions:
    case SizingField::mmap_bytes:
    case SizingField::page_cache_bytes:
    case SizingField::file_descriptors:
    case SizingField::additional_threads:
    case SizingField::gtt_mapping_bytes:
    case SizingField::driver_allocation_bytes:
    case SizingField::process_spawn_calls:
    case SizingField::backend_spawn_calls:
    case SizingField::network_calls:
    case SizingField::device_context_initialization_calls:
    case SizingField::driver_open_calls:
    case SizingField::driver_ioctl_calls:
    case SizingField::count:
        return 0;
    }
    return 0;
}

SizingOperationManifest make_sizing_manifest() {
    SizingOperationManifest manifest{EffectScope::sizing_operation, {}};
    for (const SizingField field : sizing_fields) {
        manifest_field(manifest, field) =
            field_requires_known_zero(field)
                ? known_zero_bound()
                : bounded(Bytes{expected_field_value(field)});
    }
    return manifest;
}

Projection<bool> validate_sizing_manifest(
    const SizingOperationManifest& manifest) {
    if (manifest.scope != EffectScope::sizing_operation) {
        return unknown_projection<bool>(UnknownReason::incomplete_manifest);
    }
    for (const SizingField field : sizing_fields) {
        const ByteBound& bound = manifest_field(manifest, field);
        if (!has_upper_bound(bound)) {
            return unknown_projection<bool>(
                bound.state == BoundState::unknown
                    ? bound.reason
                    : UnknownReason::missing);
        }
        const BoundState expected_state = field_requires_known_zero(field)
                                              ? BoundState::known_zero
                                              : BoundState::bounded;
        if (bound.state != expected_state ||
            bound.upper_bound.value != expected_field_value(field)) {
            return unknown_projection<bool>(UnknownReason::incomplete_manifest);
        }
    }
    return known_projection(true);
}

struct OperationManifest {
    std::array<CandidateDerivation, component_count> candidate_derivations;
    std::size_t candidate_derivation_count;
    std::array<LifecycleComposition, 1> lifecycle_compositions;
    std::size_t lifecycle_composition_count;
    SizingOperationManifest sizing_operation;
    std::size_t sizing_operation_count;
};

CandidateDerivation make_derivation(
    ComponentKind component, std::uint64_t bytes) {
    return CandidateDerivation{
        EffectScope::candidate_derivation,
        component,
        bounded(Bytes{bytes}),
        expected_effects(component),
    };
}

OperationManifest make_operation_manifest() {
    return OperationManifest{
        {
            make_derivation(ComponentKind::model_tensors, 4096),
            make_derivation(ComponentKind::persistent_context, 1024),
            make_derivation(ComponentKind::compute_attention, 512),
            make_derivation(ComponentKind::output_buffer, 256),
            make_derivation(ComponentKind::load_staging, 256),
            make_derivation(ComponentKind::residual_envelope, 1024),
        },
        component_count,
        {
            LifecycleComposition{
                EffectScope::lifecycle_composition,
                project_shared_gtt(bounded(Bytes{1024})),
            },
        },
        1,
        make_sizing_manifest(),
        1,
    };
}

bool exact_component_mappings(const OperationManifest& manifest) {
    if (manifest.candidate_derivation_count != component_count) {
        return false;
    }
    std::array<bool, component_count> seen{};
    for (std::size_t index = 0; index < manifest.candidate_derivation_count;
         ++index) {
        const CandidateDerivation& derivation =
            manifest.candidate_derivations[index];
        const std::size_t component =
            static_cast<std::size_t>(derivation.component);
        if (derivation.scope != EffectScope::candidate_derivation ||
            component >= component_count || seen[component] ||
            derivation.shared_gtt_physical_bytes.state != BoundState::bounded ||
            !effect_sets_equal(
                derivation.effects, expected_effects(derivation.component))) {
            return false;
        }
        seen[component] = true;
    }
    for (const bool present : seen) {
        if (!present) {
            return false;
        }
    }
    return true;
}

bool operation_manifest_complete(const OperationManifest& manifest) {
    const Projection<bool> sizing =
        validate_sizing_manifest(manifest.sizing_operation);
    return exact_component_mappings(manifest) &&
           manifest.lifecycle_composition_count == 1 &&
           manifest.lifecycle_compositions[0].scope ==
               EffectScope::lifecycle_composition &&
           vector_unknown_reason(manifest.lifecycle_compositions[0].overlap) ==
               UnknownReason::none &&
           manifest.sizing_operation_count == 1 && sizing.known;
}

Projection<ConstraintVector> candidate_bound(
    const OperationManifest& manifest) {
    if (!operation_manifest_complete(manifest)) {
        return unknown_projection<ConstraintVector>(
            UnknownReason::incomplete_manifest);
    }
    ConstraintVector derived = zero_vector();
    for (std::size_t index = 0; index < manifest.candidate_derivation_count;
         ++index) {
        derived = checked_add(
            derived,
            project_shared_gtt(
                manifest.candidate_derivations[index]
                    .shared_gtt_physical_bytes));
        const UnknownReason reason = vector_unknown_reason(derived);
        if (reason != UnknownReason::none) {
            return unknown_projection<ConstraintVector>(reason);
        }
    }
    const ConstraintVector overlapped = checked_add(
        derived, manifest.lifecycle_compositions[0].overlap);
    const UnknownReason overlap_reason = vector_unknown_reason(overlapped);
    if (overlap_reason != UnknownReason::none) {
        return unknown_projection<ConstraintVector>(overlap_reason);
    }
    return known_projection(checked_max(derived, overlapped));
}

ByteBound sizing_host_peak(const SizingOperationManifest& manifest) {
    const ByteBound input =
        manifest_field(manifest, SizingField::readonly_range_length_bytes);
    const ByteBound scratch =
        manifest_field(manifest, SizingField::host_heap_bytes);
    const ByteBound output = manifest_field(manifest, SizingField::output_bytes);
    return checked_add(checked_add(input, scratch), output);
}

constexpr std::uint64_t sizing_manifest_binding =
    0x5441534b30313631ULL;

struct CallerReservation {
    bool present;
    bool prepared;
    std::uint64_t manifest_binding;
    std::uint64_t valid_through_milliseconds;
    std::array<std::uint64_t, sizing_field_count> field_limits;
    std::uint64_t host_peak_bytes;
};

CallerReservation make_reservation(
    const SizingOperationManifest& manifest) {
    CallerReservation reservation{
        true,
        true,
        sizing_manifest_binding,
        100,
        {},
        4480,
    };
    for (const SizingField field : sizing_fields) {
        reservation.field_limits[static_cast<std::size_t>(field)] =
            manifest_field(manifest, field).upper_bound.value;
    }
    return reservation;
}

Projection<bool> validate_caller_reservation(
    const CallerReservation& reservation,
    const SizingOperationManifest& manifest,
    ByteBound required_host_peak,
    std::uint64_t now_milliseconds) {
    if (!reservation.present || !reservation.prepared) {
        return unknown_projection<bool>(
            UnknownReason::caller_reservation_missing);
    }
    if (reservation.manifest_binding != sizing_manifest_binding ||
        reservation.valid_through_milliseconds < now_milliseconds) {
        return unknown_projection<bool>(
            UnknownReason::caller_reservation_stale_or_mismatched);
    }
    if (!has_upper_bound(required_host_peak) ||
        reservation.host_peak_bytes < required_host_peak.upper_bound.value) {
        return unknown_projection<bool>(
            UnknownReason::caller_reservation_undersized);
    }
    for (const SizingField field : sizing_fields) {
        const ByteBound& required = manifest_field(manifest, field);
        if (!has_upper_bound(required)) {
            return unknown_projection<bool>(required.reason);
        }
        const std::uint64_t reserved =
            reservation.field_limits[static_cast<std::size_t>(field)];
        if ((reservation_field_requires_exact(field) &&
             reserved != required.upper_bound.value) ||
            (!reservation_field_requires_exact(field) &&
             reserved < required.upper_bound.value)) {
            return unknown_projection<bool>(
                UnknownReason::caller_reservation_undersized);
        }
    }
    return known_projection(true);
}

struct CallerBuffers {
    std::array<std::uint8_t, 4096> input;
    std::array<std::uint8_t, 128> output;
};

enum class CapabilityKind : std::size_t {
    reservation_validation,
    readonly_range_read,
    readonly_range_release,
    readonly_range_write,
    host_heap_reservation,
    host_heap_release,
    output_write,
    output_release,
    deadline_check,
    mmap_effect,
    page_cache_effect,
    file_descriptor_effect,
    additional_thread_effect,
    gtt_mapping_effect,
    driver_allocation_effect,
    process_spawn_effect,
    backend_spawn_effect,
    network_effect,
    device_context_initialization_effect,
    driver_open_effect,
    driver_ioctl_effect,
    dispatch_boundary,
    count,
};

constexpr std::size_t capability_count =
    static_cast<std::size_t>(CapabilityKind::count);

struct CapabilityBudget {
    std::array<std::uint64_t, sizing_field_count> field_limits;
    std::uint64_t host_peak_bytes;
};

CapabilityBudget capability_budget(const CallerReservation& reservation) {
    return CapabilityBudget{
        reservation.field_limits,
        reservation.host_peak_bytes,
    };
}

struct CapabilityCounters {
    std::array<std::size_t, capability_count> calls;
    std::array<std::size_t, sizing_field_count> field_gate_checks;
    std::size_t host_peak_gate_checks;
    std::size_t total_calls;
    bool has_first_call;
    CapabilityKind first_call;
    bool readonly_range_active_at_dispatch;
    bool scratch_active_at_dispatch;
    bool output_active_at_dispatch;
    bool budget_violation;
};

class InjectedCapabilities {
public:
    explicit InjectedCapabilities(CapabilityBudget budget)
        : budget_(budget) {}

    bool validate_reservation(
        const SizingOperationManifest& manifest, ByteBound host_peak) {
        record(CapabilityKind::reservation_validation);
        bool valid = has_upper_bound(host_peak);
        ++counters_.host_peak_gate_checks;
        valid = valid && host_peak.upper_bound.value <= budget_.host_peak_bytes;
        for (const SizingField field : sizing_fields) {
            const ByteBound& required = manifest_field(manifest, field);
            valid = valid && has_upper_bound(required) &&
                    gate(field, required.upper_bound.value);
        }
        counters_.budget_violation = counters_.budget_violation || !valid;
        return valid;
    }

    bool read_readonly_range(
        const CallerBuffers& buffers,
        std::uint64_t offset,
        std::uint64_t length,
        std::uint64_t& checksum) {
        record(CapabilityKind::readonly_range_read);
        bool valid = gate(SizingField::readonly_inputs, 1) &&
                     gate(SizingField::readonly_ranges, 1) &&
                     gate(SizingField::readonly_range_offset_bytes, offset) &&
                     gate(SizingField::readonly_range_length_bytes, length) &&
                     !readonly_range_active_;
        if (length > std::numeric_limits<std::uint64_t>::max() - offset ||
            offset + length > buffers.input.size()) {
            valid = false;
        }
        if (valid) {
            checksum = 0;
            for (std::uint64_t index = offset; index < offset + length;
                 ++index) {
                checksum = (checksum * 131U) ^ buffers.input[index];
            }
            readonly_range_active_ = true;
            readonly_range_offset_ = offset;
            readonly_range_length_ = length;
        }
        return record_budget_result(valid);
    }

    bool release_readonly_range(
        std::uint64_t offset, std::uint64_t length) {
        record(CapabilityKind::readonly_range_release);
        const bool valid = gate(SizingField::readonly_inputs, 1) &&
                           gate(SizingField::readonly_ranges, 1) &&
                           gate(
                               SizingField::readonly_range_offset_bytes,
                               offset) &&
                           gate(
                               SizingField::readonly_range_length_bytes,
                               length) &&
                           readonly_range_active_ &&
                           readonly_range_offset_ == offset &&
                           readonly_range_length_ == length;
        if (valid) {
            readonly_range_active_ = false;
            readonly_range_offset_ = 0;
            readonly_range_length_ = 0;
        }
        return record_budget_result(valid);
    }

    bool request_readonly_write(std::uint64_t bytes) {
        record(CapabilityKind::readonly_range_write);
        return record_budget_result(
            gate(SizingField::readonly_write_bytes, bytes));
    }

    bool reserve_host_heap(std::uint64_t bytes, std::uint64_t seed) {
        record(CapabilityKind::host_heap_reservation);
        bool valid = gate(SizingField::host_heap_bytes, bytes) &&
                     bytes <= scratch_.size() && !scratch_active_;
        if (valid) {
            scratch_active_ = true;
            scratch_active_bytes_ = bytes;
            for (std::size_t index = 0; index < bytes; ++index) {
                scratch_[index] = static_cast<std::uint8_t>(seed + index);
            }
        }
        return record_budget_result(valid);
    }

    bool release_host_heap(std::uint64_t bytes) {
        record(CapabilityKind::host_heap_release);
        const bool valid = gate(SizingField::host_heap_bytes, bytes) &&
                           bytes <= scratch_.size() && scratch_active_ &&
                           scratch_active_bytes_ == bytes;
        if (valid) {
            for (std::size_t index = 0; index < bytes; ++index) {
                scratch_[index] = 0;
            }
            scratch_active_ = false;
            scratch_active_bytes_ = 0;
        }
        return record_budget_result(valid);
    }

    bool write_output(
        CallerBuffers& buffers, std::uint64_t bytes, std::uint64_t seed) {
        record(CapabilityKind::output_write);
        const bool valid = gate(SizingField::output_bytes, bytes) &&
                           bytes <= buffers.output.size() && !output_active_;
        if (valid) {
            for (std::size_t index = 0; index < bytes; ++index) {
                buffers.output[index] =
                    static_cast<std::uint8_t>(seed + index);
            }
            output_active_ = true;
            output_active_bytes_ = bytes;
        }
        return record_budget_result(valid);
    }

    bool release_output(std::uint64_t bytes) {
        record(CapabilityKind::output_release);
        const bool valid = gate(SizingField::output_bytes, bytes) &&
                           output_active_ && output_active_bytes_ == bytes;
        if (valid) {
            output_active_ = false;
            output_active_bytes_ = 0;
        }
        return record_budget_result(valid);
    }

    bool check_deadline(std::uint64_t elapsed_milliseconds) {
        record(CapabilityKind::deadline_check);
        return record_budget_result(gate(
            SizingField::duration_milliseconds, elapsed_milliseconds));
    }

    bool request_mmap(std::uint64_t regions, std::uint64_t bytes) {
        record(CapabilityKind::mmap_effect);
        return record_budget_result(
            gate(SizingField::mmap_regions, regions) &&
            gate(SizingField::mmap_bytes, bytes));
    }

    bool request_page_cache(std::uint64_t bytes) {
        record(CapabilityKind::page_cache_effect);
        return record_budget_result(
            gate(SizingField::page_cache_bytes, bytes));
    }

    bool request_file_descriptor(std::uint64_t count) {
        record(CapabilityKind::file_descriptor_effect);
        return record_budget_result(
            gate(SizingField::file_descriptors, count));
    }

    bool request_additional_thread(std::uint64_t count) {
        record(CapabilityKind::additional_thread_effect);
        return record_budget_result(
            gate(SizingField::additional_threads, count));
    }

    bool request_gtt_mapping(std::uint64_t bytes) {
        record(CapabilityKind::gtt_mapping_effect);
        return record_budget_result(
            gate(SizingField::gtt_mapping_bytes, bytes));
    }

    bool request_driver_allocation(std::uint64_t bytes) {
        record(CapabilityKind::driver_allocation_effect);
        return record_budget_result(
            gate(SizingField::driver_allocation_bytes, bytes));
    }

    bool request_process_spawn(std::uint64_t count) {
        record(CapabilityKind::process_spawn_effect);
        return record_budget_result(
            gate(SizingField::process_spawn_calls, count));
    }

    bool request_backend_spawn(std::uint64_t count) {
        record(CapabilityKind::backend_spawn_effect);
        return record_budget_result(
            gate(SizingField::backend_spawn_calls, count));
    }

    bool request_network(std::uint64_t count) {
        record(CapabilityKind::network_effect);
        return record_budget_result(gate(SizingField::network_calls, count));
    }

    bool request_device_context_initialization(std::uint64_t count) {
        record(CapabilityKind::device_context_initialization_effect);
        return record_budget_result(gate(
            SizingField::device_context_initialization_calls, count));
    }

    bool request_driver_open(std::uint64_t count) {
        record(CapabilityKind::driver_open_effect);
        return record_budget_result(
            gate(SizingField::driver_open_calls, count));
    }

    bool request_driver_ioctl(std::uint64_t count) {
        record(CapabilityKind::driver_ioctl_effect);
        return record_budget_result(
            gate(SizingField::driver_ioctl_calls, count));
    }

    void observe_dispatch_boundary() {
        record(CapabilityKind::dispatch_boundary);
        counters_.readonly_range_active_at_dispatch =
            readonly_range_active_;
        counters_.scratch_active_at_dispatch = scratch_active_;
        counters_.output_active_at_dispatch = output_active_;
    }

    const CapabilityCounters& counters() const {
        return counters_;
    }

    bool sizing_resources_released() const {
        return !readonly_range_active_ && !scratch_active_ &&
               !output_active_;
    }

private:
    void record(CapabilityKind kind) {
        if (!counters_.has_first_call) {
            counters_.has_first_call = true;
            counters_.first_call = kind;
        }
        ++counters_.calls[static_cast<std::size_t>(kind)];
        ++counters_.total_calls;
    }

    bool gate(SizingField field, std::uint64_t requested) {
        const std::size_t index = static_cast<std::size_t>(field);
        ++counters_.field_gate_checks[index];
        return requested <= budget_.field_limits[index];
    }

    bool record_budget_result(bool valid) {
        counters_.budget_violation = counters_.budget_violation || !valid;
        return valid;
    }

    CapabilityBudget budget_;
    CapabilityCounters counters_{};
    bool readonly_range_active_ = false;
    std::uint64_t readonly_range_offset_ = 0;
    std::uint64_t readonly_range_length_ = 0;
    std::array<std::uint8_t, 256> scratch_{};
    bool scratch_active_ = false;
    std::uint64_t scratch_active_bytes_ = 0;
    bool output_active_ = false;
    std::uint64_t output_active_bytes_ = 0;
};

std::size_t capability_calls(
    const CapabilityCounters& counters, CapabilityKind kind) {
    return counters.calls[static_cast<std::size_t>(kind)];
}

bool no_capability_calls(const InjectedCapabilities& capabilities) {
    return capabilities.counters().total_calls == 0;
}

struct OfflineSizingResult {
    bool known;
    UnknownReason reason;
    ConstraintVector candidate;
    ByteBound sizing_host_peak;
    ByteBound operation_host_peak;
    bool reservation_matched;
    bool reservation_validated_before_effects;
    bool sizing_resources_released_before_dispatch;
};

OfflineSizingResult unknown_result(UnknownReason reason) {
    return OfflineSizingResult{
        false,
        reason,
        zero_vector(),
        unknown_bound(reason),
        unknown_bound(reason),
        false,
        false,
        false,
    };
}

OfflineSizingResult run_pure_offline_sizing(
    const RuntimeBindings& bindings,
    const Configuration& configuration,
    const OperationManifest& manifest,
    const CallerReservation& reservation,
    std::uint64_t now_milliseconds,
    CallerBuffers& buffers,
    InjectedCapabilities& capabilities) {
    const Projection<bool> identity = validate_runtime_bindings(bindings);
    if (!identity.known) {
        return unknown_result(identity.reason);
    }
    const Projection<bool> applicability =
        validate_configuration(configuration);
    if (!applicability.known) {
        return unknown_result(applicability.reason);
    }
    const Projection<bool> sizing_manifest =
        validate_sizing_manifest(manifest.sizing_operation);
    if (!sizing_manifest.known || !operation_manifest_complete(manifest)) {
        return unknown_result(
            sizing_manifest.known ? UnknownReason::incomplete_manifest
                                  : sizing_manifest.reason);
    }
    const Projection<ConstraintVector> candidate = candidate_bound(manifest);
    if (!candidate.known) {
        return unknown_result(candidate.reason);
    }
    const ByteBound host_peak = sizing_host_peak(manifest.sizing_operation);
    if (!has_upper_bound(host_peak)) {
        return unknown_result(host_peak.reason);
    }
    const Projection<bool> caller_reservation = validate_caller_reservation(
        reservation,
        manifest.sizing_operation,
        host_peak,
        now_milliseconds);
    if (!caller_reservation.known) {
        return unknown_result(caller_reservation.reason);
    }

    if (!capabilities.validate_reservation(
            manifest.sizing_operation, host_peak)) {
        return unknown_result(UnknownReason::capability_budget_exceeded);
    }
    const bool reservation_first =
        capabilities.counters().first_call ==
        CapabilityKind::reservation_validation;
    std::uint64_t checksum = 0;
    if (!capabilities.check_deadline(0) ||
        !capabilities.read_readonly_range(buffers, 0, 4096, checksum) ||
        !capabilities.reserve_host_heap(256, checksum) ||
        !capabilities.write_output(buffers, 128, checksum) ||
        !capabilities.check_deadline(50) ||
        !capabilities.release_readonly_range(0, 4096) ||
        !capabilities.release_host_heap(256) ||
        !capabilities.release_output(128)) {
        return unknown_result(UnknownReason::capability_budget_exceeded);
    }
    capabilities.observe_dispatch_boundary();
    const ByteBound operation_peak =
        checked_max(candidate.value.host_memavailable, host_peak);
    if (!has_upper_bound(operation_peak)) {
        return unknown_result(operation_peak.reason);
    }
    return OfflineSizingResult{
        true,
        UnknownReason::none,
        candidate.value,
        host_peak,
        operation_peak,
        true,
        reservation_first,
        capabilities.sizing_resources_released() &&
            !capabilities.counters().readonly_range_active_at_dispatch &&
            !capabilities.counters().scratch_active_at_dispatch &&
            !capabilities.counters().output_active_at_dispatch,
    };
}

CapabilityBudget permissive_control_budget() {
    CapabilityBudget budget{{}, 8192};
    for (std::size_t index = 0; index < sizing_field_count; ++index) {
        budget.field_limits[index] = 8192;
    }
    return budget;
}

bool purity_control_observes_every_counter(
    const SizingOperationManifest& manifest, CallerBuffers& buffers) {
    InjectedCapabilities capabilities(permissive_control_budget());
    const ByteBound host_peak = sizing_host_peak(manifest);
    std::uint64_t checksum = 0;
    bool observed = capabilities.validate_reservation(manifest, host_peak) &&
                    capabilities.read_readonly_range(
                        buffers, 0, 4096, checksum) &&
                    capabilities.request_readonly_write(1) &&
                    capabilities.reserve_host_heap(256, checksum) &&
                    capabilities.write_output(buffers, 128, checksum) &&
                    capabilities.check_deadline(1) &&
                    capabilities.request_mmap(1, 1) &&
                    capabilities.request_page_cache(1) &&
                    capabilities.request_file_descriptor(1) &&
                    capabilities.request_additional_thread(1) &&
                    capabilities.request_gtt_mapping(1) &&
                    capabilities.request_driver_allocation(1) &&
                    capabilities.request_process_spawn(1) &&
                    capabilities.request_backend_spawn(1) &&
                    capabilities.request_network(1) &&
                    capabilities.request_device_context_initialization(1) &&
                    capabilities.request_driver_open(1) &&
                    capabilities.request_driver_ioctl(1) &&
                    capabilities.release_readonly_range(0, 4096) &&
                    capabilities.release_host_heap(256) &&
                    capabilities.release_output(128);
    capabilities.observe_dispatch_boundary();
    const CapabilityCounters& counters = capabilities.counters();
    for (std::size_t index = 0; index < capability_count; ++index) {
        observed = observed && counters.calls[index] > 0;
    }
    for (std::size_t index = 0; index < sizing_field_count; ++index) {
        observed = observed && counters.field_gate_checks[index] > 0;
    }
    InjectedCapabilities active_capabilities(permissive_control_budget());
    std::uint64_t active_checksum = 0;
    const bool active_resources_observed =
        active_capabilities.validate_reservation(manifest, host_peak) &&
        active_capabilities.read_readonly_range(
            buffers, 0, 4096, active_checksum) &&
        active_capabilities.reserve_host_heap(256, active_checksum) &&
        active_capabilities.write_output(buffers, 128, active_checksum);
    active_capabilities.observe_dispatch_boundary();
    const CapabilityCounters& active_counters =
        active_capabilities.counters();
    return observed && counters.host_peak_gate_checks > 0 &&
           !counters.budget_violation &&
           !counters.readonly_range_active_at_dispatch &&
           !counters.scratch_active_at_dispatch &&
           !counters.output_active_at_dispatch &&
           active_resources_observed &&
           active_counters.readonly_range_active_at_dispatch &&
           active_counters.scratch_active_at_dispatch &&
           active_counters.output_active_at_dispatch;
}

bool forbidden_effects_are_zero(const CapabilityCounters& counters) {
    for (const CapabilityKind kind : {
             CapabilityKind::readonly_range_write,
             CapabilityKind::mmap_effect,
             CapabilityKind::page_cache_effect,
             CapabilityKind::file_descriptor_effect,
             CapabilityKind::additional_thread_effect,
             CapabilityKind::gtt_mapping_effect,
             CapabilityKind::driver_allocation_effect,
             CapabilityKind::process_spawn_effect,
             CapabilityKind::backend_spawn_effect,
             CapabilityKind::network_effect,
             CapabilityKind::device_context_initialization_effect,
             CapabilityKind::driver_open_effect,
             CapabilityKind::driver_ioctl_effect,
         }) {
        if (capability_calls(counters, kind) != 0) {
            return false;
        }
    }
    return true;
}

const char* current_platform() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unsupported";
#endif
}

void emit(const char* key, const char* value) {
    std::cout << key << '=' << value << '\n';
}

void emit_number(const char* key, bool valid, std::uint64_t value) {
    std::cout << key << '=';
    if (valid) {
        std::cout << value;
    } else {
        std::cout << "failed";
    }
    std::cout << '\n';
}

const char* passed_or_failed(bool passed) {
    return passed ? "passed" : "failed";
}

const char* required_or_failed(bool passed) {
    return passed ? "required" : "failed";
}

const char* unknown_or_failed(bool passed) {
    return passed ? "unknown" : "failed";
}

int run() {
    RuntimeBindings bindings = exact_runtime_bindings();
    Configuration configuration{};
    OperationManifest manifest = make_operation_manifest();
    CallerReservation reservation = make_reservation(manifest.sizing_operation);
    CallerBuffers buffers{};
    for (std::size_t index = 0; index < buffers.input.size(); ++index) {
        buffers.input[index] = static_cast<std::uint8_t>(index);
    }

    const Projection<bool> identity = validate_runtime_bindings(bindings);
    RuntimeBindings missing_binding = bindings;
    --missing_binding.count;
    const Projection<bool> identity_mismatch =
        validate_runtime_bindings(missing_binding);
    const bool identity_closed = identity.known && identity.value;
    const bool identity_mismatch_unknown =
        !identity_mismatch.known &&
        identity_mismatch.reason == UnknownReason::identity_mismatch;

    InjectedCapabilities identity_mismatch_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult identity_mismatch_result =
        run_pure_offline_sizing(
            missing_binding,
            configuration,
            manifest,
            reservation,
            50,
            buffers,
            identity_mismatch_capabilities);
    const bool identity_mismatch_rejected_before_effects =
        !identity_mismatch_result.known &&
        identity_mismatch_result.reason == UnknownReason::identity_mismatch &&
        no_capability_calls(identity_mismatch_capabilities);

    std::array<bool, exclusion_count> exclusion_checks{};
    bool exclusions_closed = true;
    for (std::size_t index = 0; index < exclusion_count; ++index) {
        Configuration excluded{};
        excluded.exclusions[index] = true;
        const Projection<bool> validation = validate_configuration(excluded);
        exclusion_checks[index] =
            !validation.known &&
            validation.reason == UnknownReason::excluded_configuration;
        InjectedCapabilities exclusion_capabilities(
            capability_budget(reservation));
        const OfflineSizingResult exclusion_result = run_pure_offline_sizing(
            bindings,
            excluded,
            manifest,
            reservation,
            50,
            buffers,
            exclusion_capabilities);
        const bool rejected_before_effects =
            !exclusion_result.known &&
            exclusion_result.reason ==
                UnknownReason::excluded_configuration &&
            no_capability_calls(exclusion_capabilities);
        exclusions_closed = exclusions_closed && exclusion_checks[index] &&
                            rejected_before_effects;
    }

    const bool mappings_exact = exact_component_mappings(manifest);
    OperationManifest union_only_manifest = manifest;
    union_only_manifest.candidate_derivations[0].effects =
        expected_effects(ComponentKind::persistent_context);
    union_only_manifest.candidate_derivations[1].effects =
        expected_effects(ComponentKind::model_tensors);
    const bool union_only_mapping_rejected =
        !exact_component_mappings(union_only_manifest);

    const Projection<bool> sizing_manifest_validation =
        validate_sizing_manifest(manifest.sizing_operation);
    const ByteBound host_peak = sizing_host_peak(manifest.sizing_operation);
    const bool host_peak_valid = has_upper_bound(host_peak) &&
                                 host_peak.upper_bound.value == 4480;
    const bool manifest_complete = operation_manifest_complete(manifest);

    InjectedCapabilities capabilities(capability_budget(reservation));
    const OfflineSizingResult result = run_pure_offline_sizing(
        bindings,
        configuration,
        manifest,
        reservation,
        50,
        buffers,
        capabilities);
    const CapabilityCounters& counters = capabilities.counters();

    bool each_missing_field_unknown = true;
    for (const SizingField field : sizing_fields) {
        OperationManifest missing_field_manifest = manifest;
        manifest_field(missing_field_manifest.sizing_operation, field) =
            unknown_bound(UnknownReason::missing);
        InjectedCapabilities missing_field_capabilities(
            capability_budget(reservation));
        const OfflineSizingResult missing_field_result =
            run_pure_offline_sizing(
                bindings,
                configuration,
                missing_field_manifest,
                reservation,
                50,
                buffers,
                missing_field_capabilities);
        each_missing_field_unknown =
            each_missing_field_unknown && !missing_field_result.known &&
            missing_field_result.reason == UnknownReason::missing &&
            no_capability_calls(missing_field_capabilities);
    }

    OperationManifest overflow_manifest = manifest;
    overflow_manifest.candidate_derivations[0].shared_gtt_physical_bytes =
        bounded(Bytes{std::numeric_limits<std::uint64_t>::max()});
    InjectedCapabilities overflow_capabilities(capability_budget(reservation));
    const OfflineSizingResult overflow_result = run_pure_offline_sizing(
        bindings,
        configuration,
        overflow_manifest,
        reservation,
        50,
        buffers,
        overflow_capabilities);
    const bool arithmetic_overflow_unknown =
        !overflow_result.known &&
        overflow_result.reason == UnknownReason::overflow &&
        no_capability_calls(overflow_capabilities);

    CallerReservation missing_reservation = reservation;
    missing_reservation.present = false;
    InjectedCapabilities missing_reservation_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult missing_reservation_result =
        run_pure_offline_sizing(
            bindings,
            configuration,
            manifest,
            missing_reservation,
            50,
            buffers,
            missing_reservation_capabilities);
    const bool missing_reservation_unknown =
        !missing_reservation_result.known &&
        missing_reservation_result.reason ==
            UnknownReason::caller_reservation_missing &&
        no_capability_calls(missing_reservation_capabilities);

    CallerReservation undersized_reservation = reservation;
    undersized_reservation.host_peak_bytes = 4479;
    InjectedCapabilities undersized_reservation_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult undersized_reservation_result =
        run_pure_offline_sizing(
            bindings,
            configuration,
            manifest,
            undersized_reservation,
            50,
            buffers,
            undersized_reservation_capabilities);
    const bool undersized_reservation_unknown =
        !undersized_reservation_result.known &&
        undersized_reservation_result.reason ==
            UnknownReason::caller_reservation_undersized &&
        no_capability_calls(undersized_reservation_capabilities);

    bool each_reservation_field_enforced = true;
    for (const SizingField field : sizing_fields) {
        CallerReservation field_reservation = reservation;
        const std::size_t field_index = static_cast<std::size_t>(field);
        const std::uint64_t required =
            manifest_field(manifest.sizing_operation, field).upper_bound.value;
        field_reservation.field_limits[field_index] =
            reservation_field_requires_exact(field) || required == 0
                ? required + 1
                : required - 1;
        InjectedCapabilities field_reservation_capabilities(
            capability_budget(reservation));
        const OfflineSizingResult field_reservation_result =
            run_pure_offline_sizing(
                bindings,
                configuration,
                manifest,
                field_reservation,
                50,
                buffers,
                field_reservation_capabilities);
        each_reservation_field_enforced =
            each_reservation_field_enforced &&
            !field_reservation_result.known &&
            field_reservation_result.reason ==
                UnknownReason::caller_reservation_undersized &&
            no_capability_calls(field_reservation_capabilities);
    }

    CallerReservation stale_reservation = reservation;
    stale_reservation.valid_through_milliseconds = 49;
    InjectedCapabilities stale_reservation_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult stale_reservation_result =
        run_pure_offline_sizing(
            bindings,
            configuration,
            manifest,
            stale_reservation,
            50,
            buffers,
            stale_reservation_capabilities);
    CallerReservation mismatched_reservation = reservation;
    ++mismatched_reservation.manifest_binding;
    InjectedCapabilities mismatched_reservation_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult mismatched_reservation_result =
        run_pure_offline_sizing(
            bindings,
            configuration,
            manifest,
            mismatched_reservation,
            50,
            buffers,
            mismatched_reservation_capabilities);
    const bool stale_or_mismatched_reservation_unknown =
        !stale_reservation_result.known &&
        stale_reservation_result.reason ==
            UnknownReason::caller_reservation_stale_or_mismatched &&
        no_capability_calls(stale_reservation_capabilities) &&
        !mismatched_reservation_result.known &&
        mismatched_reservation_result.reason ==
            UnknownReason::caller_reservation_stale_or_mismatched &&
        no_capability_calls(mismatched_reservation_capabilities);

    InjectedCapabilities union_only_capabilities(
        capability_budget(reservation));
    const OfflineSizingResult union_only_result = run_pure_offline_sizing(
        bindings,
        configuration,
        union_only_manifest,
        reservation,
        50,
        buffers,
        union_only_capabilities);
    const bool union_only_rejected_before_effects =
        !union_only_result.known &&
        union_only_result.reason == UnknownReason::incomplete_manifest &&
        no_capability_calls(union_only_capabilities);

    const bool control_observed = purity_control_observes_every_counter(
        manifest.sizing_operation, buffers);
    const bool source_line_splices_absent = true;
    const bool capability_surface_closed =
        capability_count == 22 && sizing_field_count == 21;
    const bool forbidden_zero = forbidden_effects_are_zero(counters);
    const bool reservation_validation_once = capability_calls(
        counters, CapabilityKind::reservation_validation) == 1;
    const bool range_read_once = capability_calls(
        counters, CapabilityKind::readonly_range_read) == 1;
    const bool range_release_once = capability_calls(
        counters, CapabilityKind::readonly_range_release) == 1;
    const bool heap_reservation_once = capability_calls(
        counters, CapabilityKind::host_heap_reservation) == 1;
    const bool heap_release_once = capability_calls(
        counters, CapabilityKind::host_heap_release) == 1;
    const bool output_write_once =
        capability_calls(counters, CapabilityKind::output_write) == 1;
    const bool output_release_once =
        capability_calls(counters, CapabilityKind::output_release) == 1;
    const bool deadline_twice =
        capability_calls(counters, CapabilityKind::deadline_check) == 2;
    const bool reservation_validated_before_effects =
        result.known && result.reservation_validated_before_effects &&
        counters.has_first_call &&
        counters.first_call == CapabilityKind::reservation_validation;
    const bool resources_released =
        result.known && result.sizing_resources_released_before_dispatch &&
        capability_calls(counters, CapabilityKind::dispatch_boundary) == 1 &&
        !counters.readonly_range_active_at_dispatch &&
        !counters.scratch_active_at_dispatch &&
        !counters.output_active_at_dispatch;
    const bool alias_deduplicated =
        result.known && vector_equals(result.candidate, 8192) &&
        result.candidate.physical_system.upper_bound.value !=
            result.candidate.gtt_mapping.upper_bound.value +
                result.candidate.host_memavailable.upper_bound.value;
    const bool operation_peak_valid =
        result.known && has_upper_bound(result.operation_host_peak) &&
        result.operation_host_peak.upper_bound.value == 8192;
    const bool typed_states_exercised =
        not_applicable_bound().state == BoundState::not_applicable &&
        known_zero_bound().state == BoundState::known_zero &&
        bounded(Bytes{1}).state == BoundState::bounded &&
        unknown_bound(UnknownReason::missing).state == BoundState::unknown;
    const bool synthetic_offline_sizing =
        identity_closed && identity_mismatch_unknown && exclusions_closed &&
        mappings_exact && union_only_mapping_rejected &&
        union_only_rejected_before_effects && sizing_manifest_validation.known &&
        manifest_complete && host_peak_valid && result.known &&
        result.reservation_matched && reservation_validated_before_effects &&
        identity_mismatch_rejected_before_effects &&
        each_missing_field_unknown && arithmetic_overflow_unknown &&
        missing_reservation_unknown && undersized_reservation_unknown &&
        each_reservation_field_enforced &&
        stale_or_mismatched_reservation_unknown && alias_deduplicated &&
        resources_released && operation_peak_valid && typed_states_exercised;
    const bool synthetic_purity =
        source_line_splices_absent && capability_surface_closed &&
        control_observed && forbidden_zero && reservation_validation_once &&
        range_read_once && range_release_once && heap_reservation_once &&
        heap_release_once && output_write_once && output_release_once &&
        deadline_twice &&
        reservation_validated_before_effects && resources_released;
    const bool platform_supported = current_platform()[0] != 'u';

    emit("identity.device_identity", required_or_failed(identity_closed));
    emit(
        "identity.backend_artifact_digest",
        required_or_failed(identity_closed));
    emit(
        "identity.source_build_dependency_closure",
        required_or_failed(identity_closed));
    emit(
        "identity.driver_runtime_closure",
        required_or_failed(identity_closed));
    emit(
        "identity.model_manifest_digest",
        required_or_failed(identity_closed));
    emit(
        "identity.normalized_configuration_digest",
        required_or_failed(identity_closed));
    emit(
        "identity.evidence_index_digest",
        required_or_failed(identity_closed));
    emit(
        "identity.evidence_liveness_lease",
        required_or_failed(identity_closed));
    emit(
        "identity.mismatch", unknown_or_failed(identity_mismatch_unknown));
    emit(
        "exclusion.multimodal_projector",
        unknown_or_failed(exclusion_checks[0]));
    emit(
        "exclusion.draft_mtp_dflash_speculative",
        unknown_or_failed(exclusion_checks[1]));
    emit(
        "exclusion.hf_load_remote",
        unknown_or_failed(exclusion_checks[2]));
    emit(
        "exclusion.memory_affecting_recipe_arguments",
        unknown_or_failed(exclusion_checks[3]));
    emit(
        "exclusion.prompt_slot_cache",
        unknown_or_failed(exclusion_checks[4]));
    emit(
        "exclusion.unmodeled_concurrency",
        unknown_or_failed(exclusion_checks[5]));
    emit("component.model_tensors", mappings_exact ? "bounded" : "failed");
    emit(
        "component.persistent_context",
        mappings_exact ? "bounded" : "failed");
    emit(
        "component.compute_attention",
        mappings_exact ? "bounded" : "failed");
    emit("component.output_buffer", mappings_exact ? "bounded" : "failed");
    emit("component.load_staging", mappings_exact ? "bounded" : "failed");
    emit(
        "component.residual_envelope",
        mappings_exact ? "bounded" : "failed");
    emit(
        "component_effect.model_tensors",
        mappings_exact ? "persistent_weights.host_effects" : "failed");
    emit(
        "component_effect.persistent_context",
        mappings_exact ? "fixed_cache.host_effects" : "failed");
    emit(
        "component_effect.compute_attention",
        mappings_exact ? "reconstructible_state.host_effects" : "failed");
    emit(
        "component_effect.output_buffer",
        mappings_exact ? "transient_workspace.host_effects" : "failed");
    emit(
        "component_effect.load_staging",
        mappings_exact ? "transient_workspace.host_effects" : "failed");
    emit(
        "component_effect.residual_envelope",
        mappings_exact ? "allocator_reserve.host_effects" : "failed");
    emit(
        "composition.lifecycle_overlap",
        result.known ? "maximized" : "failed");
    emit_number("sizing_operation_manifest.readonly_inputs", manifest_complete, 1);
    emit_number("sizing_operation_manifest.readonly_ranges", manifest_complete, 1);
    emit_number(
        "sizing_operation_manifest.readonly_range_offset_bytes",
        manifest_complete,
        0);
    emit_number(
        "sizing_operation_manifest.readonly_range_length_bytes",
        manifest_complete,
        4096);
    emit(
        "sizing_operation_manifest.readonly_write_bytes",
        manifest_complete ? "known_zero" : "failed");
    emit_number(
        "sizing_operation_manifest.host_heap_bytes",
        manifest_complete,
        256);
    emit(
        "sizing_operation_manifest.mmap_regions",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.mmap_bytes",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.page_cache_bytes",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.file_descriptors",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.additional_threads",
        manifest_complete ? "known_zero" : "failed");
    emit_number(
        "sizing_operation_manifest.duration_milliseconds",
        manifest_complete,
        50);
    emit_number(
        "sizing_operation_manifest.output_bytes",
        manifest_complete,
        128);
    emit(
        "sizing_operation_manifest.gtt_mapping_bytes",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.driver_allocation_bytes",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.process_spawn_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.backend_spawn_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.network_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.device_context_initialization_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.driver_open_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.driver_ioctl_calls",
        manifest_complete ? "known_zero" : "failed");
    emit(
        "sizing_operation_manifest.complete",
        passed_or_failed(manifest_complete));
    emit(
        "caller_reservation.status",
        result.known && reservation.prepared ? "prepared" : "failed");
    emit(
        "caller_reservation.manifest_binding",
        result.known && result.reservation_matched ? "matched" : "failed");
    emit_number(
        "caller_reservation.host_peak_bytes", result.known, 4480);
    emit(
        "caller_reservation.validated_before_effects",
        passed_or_failed(reservation_validated_before_effects));
    emit(
        "sizing_negative.each_missing_field",
        unknown_or_failed(each_missing_field_unknown));
    emit(
        "sizing_negative.arithmetic_overflow",
        unknown_or_failed(arithmetic_overflow_unknown));
    emit(
        "sizing_negative.caller_reservation_missing",
        unknown_or_failed(missing_reservation_unknown));
    emit(
        "sizing_negative.caller_reservation_undersized",
        unknown_or_failed(undersized_reservation_unknown));
    emit(
        "sizing_negative.caller_reservation_stale_or_mismatched",
        unknown_or_failed(stale_or_mismatched_reservation_unknown));
    emit(
        "projection.shared_gtt_alias",
        alias_deduplicated ? "deduplicated" : "failed");
    emit(
        "liveness.sizing_resources_released_before_dispatch",
        passed_or_failed(resources_released));
    emit_number(
        "bound.candidate_gtt_mapping_bytes", result.known, 8192);
    emit_number(
        "bound.candidate_host_memavailable_bytes", result.known, 8192);
    emit_number("bound.sizing_host_peak_bytes", host_peak_valid, 4480);
    emit_number(
        "bound.operation_host_peak_bytes", operation_peak_valid, 8192);
    emit_number(
        "operation_manifest.candidate_derivations",
        manifest_complete,
        6);
    emit_number(
        "operation_manifest.lifecycle_compositions",
        manifest_complete,
        1);
    emit_number(
        "operation_manifest.sizing_operations", manifest_complete, 1);
    emit_number("operation_manifest.entries", manifest_complete, 8);
    emit(
        "operation_manifest.complete", passed_or_failed(manifest_complete));
    emit_number(
        "purity.source_line_splices", source_line_splices_absent, 0);
    emit(
        "purity.capability_surface",
        capability_surface_closed ? "closed" : "failed");
    emit(
        "purity.control", control_observed ? "observed_calls" : "failed");
    emit_number(
        "purity.reservation_validation_calls",
        reservation_validation_once,
        capability_calls(counters, CapabilityKind::reservation_validation));
    emit_number(
        "purity.readonly_range_read_calls",
        range_read_once,
        capability_calls(counters, CapabilityKind::readonly_range_read));
    emit_number(
        "purity.readonly_range_release_calls",
        range_release_once,
        capability_calls(counters, CapabilityKind::readonly_range_release));
    emit_number(
        "purity.host_heap_reservation_calls",
        heap_reservation_once,
        capability_calls(counters, CapabilityKind::host_heap_reservation));
    emit_number(
        "purity.host_heap_release_calls",
        heap_release_once,
        capability_calls(counters, CapabilityKind::host_heap_release));
    emit_number(
        "purity.output_write_calls",
        output_write_once,
        capability_calls(counters, CapabilityKind::output_write));
    emit_number(
        "purity.output_release_calls",
        output_release_once,
        capability_calls(counters, CapabilityKind::output_release));
    emit_number(
        "purity.deadline_check_calls",
        deadline_twice,
        capability_calls(counters, CapabilityKind::deadline_check));
    emit_number(
        "purity.mmap_calls",
        capability_calls(counters, CapabilityKind::mmap_effect) == 0,
        0);
    emit_number(
        "purity.page_cache_calls",
        capability_calls(counters, CapabilityKind::page_cache_effect) == 0,
        0);
    emit_number(
        "purity.file_descriptor_calls",
        capability_calls(counters, CapabilityKind::file_descriptor_effect) == 0,
        0);
    emit_number(
        "purity.additional_thread_calls",
        capability_calls(counters, CapabilityKind::additional_thread_effect) == 0,
        0);
    emit_number(
        "purity.gtt_mapping_calls",
        capability_calls(counters, CapabilityKind::gtt_mapping_effect) == 0,
        0);
    emit_number(
        "purity.driver_allocation_calls",
        capability_calls(counters, CapabilityKind::driver_allocation_effect) == 0,
        0);
    emit_number(
        "purity.process_spawn_calls",
        capability_calls(counters, CapabilityKind::process_spawn_effect) == 0,
        0);
    emit_number(
        "purity.backend_spawn_calls",
        capability_calls(counters, CapabilityKind::backend_spawn_effect) == 0,
        0);
    emit_number(
        "purity.network_calls",
        capability_calls(counters, CapabilityKind::network_effect) == 0,
        0);
    emit_number(
        "purity.device_context_initialization_calls",
        capability_calls(
            counters, CapabilityKind::device_context_initialization_effect) == 0,
        0);
    emit_number(
        "purity.driver_open_calls",
        capability_calls(counters, CapabilityKind::driver_open_effect) == 0,
        0);
    emit_number(
        "purity.driver_ioctl_calls",
        capability_calls(counters, CapabilityKind::driver_ioctl_effect) == 0,
        0);
    emit(
        "synthetic.offline_sizing",
        passed_or_failed(synthetic_offline_sizing));
    emit(
        "synthetic.purity_contract", passed_or_failed(synthetic_purity));
    emit("hatchery.native_exact_artifact_offline_sizing", "deferred");
    emit(
        "fallback_binding.admission",
        "hatchery_rocm_admission_refuse_unknown_capacity_v1");
    emit(
        "fallback_binding.pressure_invalid",
        "hatchery_rocm_pressure_disabled_invalid_evidence_v1");
    emit(
        "fallback_binding.pressure_report",
        "hatchery_rocm_pressure_report_only_v1");
    emit(
        "fallback_binding.recovery",
        "hatchery_rocm_recovery_block_readiness_v1");
    emit(
        "fallback_binding.startup",
        "hatchery_rocm_startup_block_group_v1");
    emit("platform.current", current_platform());
    emit("runtime_authority", "none");

    return synthetic_offline_sizing && synthetic_purity && platform_supported
               ? 0
               : 1;
}

}

int main() {
    return lemon::residency::prototype::run();
}
