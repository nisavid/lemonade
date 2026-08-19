#include "lemon/residency/journal.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lemon::residency {

struct ResidentHead {
    std::string daemon_epoch;
    ResidentState state;
};

struct JournalHistory::State {
    std::string journal_id;
    std::string daemon_epoch;
    std::uint64_t tip_sequence = 0;
    std::optional<std::string> tip_predecessor_checksum_sha256;
    std::string tip_checksum_sha256;
    std::unordered_map<std::string, ResidentHead> resident_heads;
    std::uint64_t live_resident_count = 0;
    std::uint64_t stale_epoch_live_resident_count = 0;
};

namespace {

using json = nlohmann::json;

constexpr char journal_record_domain[] = "lemonade.residency.journal-record/v1\0";
constexpr char authority_root_domain[] = "lemonade.residency.authority-root/v1\0";

class JournalFailure final : public std::runtime_error {
public:
    JournalFailure(JournalStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    JournalStatus status() const noexcept { return status_; }

private:
    JournalStatus status_;
};

[[noreturn]] void reject(JournalStatus status, std::string message) {
    throw JournalFailure(status, std::move(message));
}

std::string bounded_diagnostic(std::string message) {
    if (message.size() > max_journal_diagnostic_bytes) {
        std::size_t boundary = max_journal_diagnostic_bytes;
        while (boundary > 0 && (static_cast<unsigned char>(message[boundary]) & 0xc0) == 0x80) {
            --boundary;
        }
        message.resize(boundary);
    }
    return message;
}

bool schema_is_supported(SchemaVersion schema) noexcept {
    return schema.major == supported_journal_schema.major &&
           schema.minor == supported_journal_schema.minor;
}

bool is_lower_hex_digest(std::string_view value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool identifier_is_valid(std::string_view value) {
    return !value.empty() && value.size() <= max_journal_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

void require_identifier(std::string_view value, std::string_view label) {
    if (!identifier_is_valid(value)) {
        reject(JournalStatus::InvalidIdentifier, std::string(label) + " is invalid");
    }
}

std::string_view claim_family_wire(ClaimFamily value) noexcept {
    switch (value) {
    case ClaimFamily::ConsumableCapacity:
        return "consumable_capacity";
    case ClaimFamily::SafetyFloor:
        return "safety_floor";
    case ClaimFamily::CardinalityPool:
        return "cardinality_pool";
    case ClaimFamily::CompatibilityExclusivity:
        return "compatibility_exclusivity";
    }
    return {};
}

std::string_view completeness_wire(ClaimCompleteness value) noexcept {
    switch (value) {
    case ClaimCompleteness::NotApplicable:
        return "not_applicable";
    case ClaimCompleteness::KnownZero:
        return "known_zero";
    case ClaimCompleteness::Bounded:
        return "bounded";
    case ClaimCompleteness::Unknown:
        return "unknown";
    }
    return {};
}

std::string_view claim_unit_wire(ClaimUnit value) noexcept {
    switch (value) {
    case ClaimUnit::Bytes:
        return "bytes";
    case ClaimUnit::Count:
        return "count";
    }
    return {};
}

std::string_view resident_state_wire(ResidentState value) noexcept {
    switch (value) {
    case ResidentState::Prepared:
        return "prepared";
    case ResidentState::Provisional:
        return "provisional";
    case ResidentState::Active:
        return "active";
    case ResidentState::Suspended:
        return "suspended";
    case ResidentState::Quarantined:
        return "quarantined";
    case ResidentState::Released:
        return "released";
    }
    return {};
}

std::string_view disposition_wire(RecoveryDisposition value) noexcept {
    switch (value) {
    case RecoveryDisposition::VerifiedIntact:
        return "verified_intact";
    case RecoveryDisposition::VerifiedReleased:
        return "verified_released";
    case RecoveryDisposition::Quarantined:
        return "quarantined";
    }
    return {};
}

std::string_view origin_kind_wire(RecoveryOriginKind value) noexcept {
    switch (value) {
    case RecoveryOriginKind::PreparedLaunch:
        return "prepared_launch";
    case RecoveryOriginKind::RuntimeRealization:
        return "runtime_realization";
    case RecoveryOriginKind::LifecycleEffect:
        return "lifecycle_effect";
    case RecoveryOriginKind::JournalReplay:
        return "journal_replay";
    case RecoveryOriginKind::PriorEpochRecovery:
        return "prior_epoch_recovery";
    case RecoveryOriginKind::CutoverReconciliation:
        return "cutover_reconciliation";
    case RecoveryOriginKind::ArtifactIdentityEffect:
        return "artifact_identity_effect";
    }
    return {};
}

template <typename Enum, std::size_t Size>
Enum parse_closed_value(std::string_view wire,
                        const std::array<std::pair<std::string_view, Enum>, Size> &values,
                        std::string_view label) {
    for (const auto &[known_wire, value] : values) {
        if (wire == known_wire) {
            return value;
        }
    }
    reject(JournalStatus::UnknownValue, std::string(label) + " is unknown");
}

ClaimFamily parse_claim_family(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, ClaimFamily>, 4> values{{
        {"consumable_capacity", ClaimFamily::ConsumableCapacity},
        {"safety_floor", ClaimFamily::SafetyFloor},
        {"cardinality_pool", ClaimFamily::CardinalityPool},
        {"compatibility_exclusivity", ClaimFamily::CompatibilityExclusivity},
    }};
    return parse_closed_value(wire, values, "claim family");
}

ClaimCompleteness parse_completeness(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, ClaimCompleteness>, 4> values{{
        {"not_applicable", ClaimCompleteness::NotApplicable},
        {"known_zero", ClaimCompleteness::KnownZero},
        {"bounded", ClaimCompleteness::Bounded},
        {"unknown", ClaimCompleteness::Unknown},
    }};
    return parse_closed_value(wire, values, "claim completeness");
}

ClaimUnit parse_claim_unit(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, ClaimUnit>, 2> values{{
        {"bytes", ClaimUnit::Bytes},
        {"count", ClaimUnit::Count},
    }};
    return parse_closed_value(wire, values, "claim unit");
}

ResidentState parse_resident_state(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, ResidentState>, 6> values{{
        {"prepared", ResidentState::Prepared},
        {"provisional", ResidentState::Provisional},
        {"active", ResidentState::Active},
        {"suspended", ResidentState::Suspended},
        {"quarantined", ResidentState::Quarantined},
        {"released", ResidentState::Released},
    }};
    return parse_closed_value(wire, values, "resident state");
}

RecoveryDisposition parse_disposition(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, RecoveryDisposition>, 3> values{{
        {"verified_intact", RecoveryDisposition::VerifiedIntact},
        {"verified_released", RecoveryDisposition::VerifiedReleased},
        {"quarantined", RecoveryDisposition::Quarantined},
    }};
    return parse_closed_value(wire, values, "recovery disposition");
}

RecoveryOriginKind parse_origin_kind(std::string_view wire) {
    static constexpr std::array<std::pair<std::string_view, RecoveryOriginKind>, 7> values{{
        {"prepared_launch", RecoveryOriginKind::PreparedLaunch},
        {"runtime_realization", RecoveryOriginKind::RuntimeRealization},
        {"lifecycle_effect", RecoveryOriginKind::LifecycleEffect},
        {"journal_replay", RecoveryOriginKind::JournalReplay},
        {"prior_epoch_recovery", RecoveryOriginKind::PriorEpochRecovery},
        {"cutover_reconciliation", RecoveryOriginKind::CutoverReconciliation},
        {"artifact_identity_effect", RecoveryOriginKind::ArtifactIdentityEffect},
    }};
    return parse_closed_value(wire, values, "recovery origin kind");
}

OperationFamily parse_operation_family_value(std::string_view wire) {
    const auto decoded = decode_operation_family(wire);
    if (!decoded.is_known()) {
        reject(JournalStatus::UnknownValue, "operation family is unknown");
    }
    return *decoded.known_value();
}

OperationKind parse_operation_kind_value(std::string_view wire) {
    const auto decoded = decode_operation_kind(wire);
    if (!decoded.is_known()) {
        reject(JournalStatus::UnknownValue, "operation kind is unknown");
    }
    return *decoded.known_value();
}

std::string sha256_hex(std::string_view domain, std::string_view payload) {
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        reject(JournalStatus::DigestUnavailable, "SHA-256 is unavailable");
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> digest{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 || mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(&context, reinterpret_cast<const unsigned char *>(domain.data()),
                          domain.size()) != 0 ||
        mbedtls_md_update(&context, reinterpret_cast<const unsigned char *>(payload.data()),
                          payload.size()) != 0 ||
        mbedtls_md_finish(&context, digest.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) {
        reject(JournalStatus::DigestUnavailable, "SHA-256 failed");
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

void require_exact_keys(const json &object, std::initializer_list<std::string_view> expected,
                        std::string_view label) {
    if (!object.is_object()) {
        reject(JournalStatus::InvalidValue, std::string(label) + " must be an object");
    }
    std::set<std::string> expected_keys;
    for (const auto key : expected) {
        expected_keys.emplace(key);
        if (!object.contains(std::string(key))) {
            reject(JournalStatus::InvalidValue, std::string(label) + " is incomplete");
        }
    }
    for (const auto &[key, value] : object.items()) {
        static_cast<void>(value);
        if (expected_keys.find(key) == expected_keys.end()) {
            reject(JournalStatus::UnknownField, std::string(label) + " has an unknown field");
        }
    }
}

const json &required(const json &object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        reject(JournalStatus::InvalidValue, "required field is missing");
    }
    return *found;
}

std::string require_string(const json &value, std::string_view label) {
    if (!value.is_string()) {
        reject(JournalStatus::InvalidValue, std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

std::uint64_t require_u64(const json &value, std::string_view label) {
    if (!value.is_number_unsigned()) {
        reject(JournalStatus::InvalidValue, std::string(label) + " must be an unsigned integer");
    }
    return value.get<std::uint64_t>();
}

SchemaVersion parse_schema(const json &value) {
    require_exact_keys(value, {"major", "minor"}, "schema");
    const auto major = require_u64(required(value, "major"), "schema major");
    const auto minor = require_u64(required(value, "minor"), "schema minor");
    if (major > std::numeric_limits<std::uint32_t>::max() ||
        minor > std::numeric_limits<std::uint32_t>::max()) {
        reject(JournalStatus::UnsupportedSchema, "journal schema is unsupported");
    }
    const SchemaVersion schema{static_cast<std::uint32_t>(major),
                               static_cast<std::uint32_t>(minor)};
    if (!schema_is_supported(schema)) {
        reject(JournalStatus::UnsupportedSchema, "journal schema is unsupported");
    }
    return schema;
}

json schema_document(SchemaVersion schema) {
    return json{{"major", schema.major}, {"minor", schema.minor}};
}

json parse_json(std::string_view bytes) {
    if (bytes.size() > max_journal_input_bytes) {
        reject(JournalStatus::InputTooLarge, "candidate document exceeds the input limit");
    }
    if (bytes.find('\0') != std::string_view::npos) {
        reject(JournalStatus::MalformedJson, "candidate document contains a NUL byte");
    }

    std::map<int, std::set<std::string>> object_keys;
    const auto callback = [&object_keys](int depth, json::parse_event_t event, json &parsed) {
        if (event == json::parse_event_t::object_start) {
            object_keys[depth + 1].clear();
        } else if (event == json::parse_event_t::key) {
            auto &keys = object_keys[depth];
            const auto key = parsed.get<std::string>();
            if (!keys.insert(key).second) {
                reject(JournalStatus::Duplicate, "candidate document has a duplicate key");
            }
        } else if (event == json::parse_event_t::object_end) {
            object_keys.erase(depth + 1);
        }
        return true;
    };

    try {
        return json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const JournalFailure &) {
        throw;
    } catch (const json::exception &) {
        reject(JournalStatus::MalformedJson, "candidate document is malformed JSON");
    }
}

template <typename T> void sort_unique_identifiers(std::vector<T> &values, std::string_view label) {
    if (values.size() > max_journal_array_entries) {
        reject(JournalStatus::LimitExceeded, std::string(label) + " exceeds the array limit");
    }
    for (const auto &value : values) {
        require_identifier(value, label);
    }
    std::sort(values.begin(), values.end());
    if (std::adjacent_find(values.begin(), values.end()) != values.end()) {
        reject(JournalStatus::Duplicate, std::string(label) + " contains duplicates");
    }
}

void normalize_claim_closure(std::vector<ClaimFamilyClosure> &closure) {
    if (closure.size() != 4) {
        reject(JournalStatus::InvalidClaimClosure, "claim closure must contain every family");
    }
    for (auto &family : closure) {
        if (claim_family_wire(family.family).empty() ||
            completeness_wire(family.completeness).empty()) {
            reject(JournalStatus::UnknownValue, "claim closure has an unknown closed value");
        }
        if (family.entries.size() > max_journal_array_entries) {
            reject(JournalStatus::LimitExceeded, "claim family exceeds the array limit");
        }
        if ((family.completeness == ClaimCompleteness::Bounded) != !family.entries.empty()) {
            reject(JournalStatus::InvalidClaimClosure, "claim-family completeness is inconsistent");
        }
        for (const auto &entry : family.entries) {
            require_identifier(entry.constraint_id, "constraint ID");
            if (entry.amount == 0 || claim_unit_wire(entry.unit).empty()) {
                reject(JournalStatus::InvalidClaimClosure, "bounded claim amount is invalid");
            }
            const bool byte_family = family.family == ClaimFamily::ConsumableCapacity ||
                                     family.family == ClaimFamily::SafetyFloor;
            const auto expected_unit = byte_family ? ClaimUnit::Bytes : ClaimUnit::Count;
            if (entry.unit != expected_unit ||
                (family.family == ClaimFamily::CompatibilityExclusivity && entry.amount != 1)) {
                reject(JournalStatus::InvalidClaimClosure,
                       "bounded claim unit or amount is invalid");
            }
        }
        std::sort(family.entries.begin(), family.entries.end(),
                  [](const ClaimAmount &left, const ClaimAmount &right) {
                      return left.constraint_id < right.constraint_id;
                  });
        if (std::adjacent_find(family.entries.begin(), family.entries.end(),
                               [](const ClaimAmount &left, const ClaimAmount &right) {
                                   return left.constraint_id == right.constraint_id;
                               }) != family.entries.end()) {
            reject(JournalStatus::Duplicate, "claim closure has a duplicate identity tuple");
        }
    }

    std::sort(closure.begin(), closure.end(),
              [](const ClaimFamilyClosure &left, const ClaimFamilyClosure &right) {
                  return static_cast<int>(left.family) < static_cast<int>(right.family);
              });
    for (std::size_t index = 0; index < closure.size(); ++index) {
        if (static_cast<std::size_t>(closure[index].family) != index) {
            reject(JournalStatus::Duplicate, "claim closure has duplicate or missing families");
        }
    }
}

void validate_operation(const OperationIdentity &operation) {
    require_identifier(operation.operation_id, "operation ID");
    if (operation.plan_id.has_value()) {
        require_identifier(*operation.plan_id, "plan ID");
    }
    if (wire_name(operation.family).empty() || wire_name(operation.kind).empty()) {
        reject(JournalStatus::UnknownValue, "operation identity has an unknown closed value");
    }
}

void validate_lifecycle(const JournalRecordDraft &draft) {
    if (resident_state_wire(draft.resident_state).empty()) {
        reject(JournalStatus::UnknownValue, "resident state is unknown");
    }
    std::optional<RecoveryDisposition> expected;
    switch (draft.resident_state) {
    case ResidentState::Prepared:
    case ResidentState::Provisional:
        break;
    case ResidentState::Active:
    case ResidentState::Suspended:
        expected = RecoveryDisposition::VerifiedIntact;
        break;
    case ResidentState::Quarantined:
        expected = RecoveryDisposition::Quarantined;
        break;
    case ResidentState::Released:
        expected = RecoveryDisposition::VerifiedReleased;
        break;
    }
    if (draft.recovery_disposition != expected ||
        (draft.recovery_disposition.has_value() &&
         disposition_wire(*draft.recovery_disposition).empty())) {
        reject(JournalStatus::InvalidLifecycle, "resident state and disposition are inconsistent");
    }

    const bool quarantined = draft.resident_state == ResidentState::Quarantined;
    if (quarantined != draft.quarantine_origin.has_value()) {
        reject(JournalStatus::InvalidLifecycle,
               "resident state and quarantine origin are inconsistent");
    }
    if (!quarantined) {
        return;
    }
    if (origin_kind_wire(draft.quarantine_origin->kind).empty()) {
        reject(JournalStatus::UnknownValue, "recovery origin kind is unknown");
    }
    if (!is_lower_hex_digest(draft.quarantine_origin->evidence_sha256)) {
        reject(JournalStatus::InvalidValue, "recovery origin digest is invalid");
    }
}

void verify_recovery_origin(std::string_view journal_id, std::string_view resident_id,
                            std::string_view daemon_epoch,
                            const std::optional<RecoveryOrigin> &origin,
                            const RecoveryOriginVerifier *verifier) {
    if (!origin.has_value()) {
        return;
    }
    if (verifier == nullptr || !verifier->verify(RecoveryOriginVerification{
                                   journal_id,
                                   resident_id,
                                   daemon_epoch,
                                   *origin,
                               })) {
        reject(JournalStatus::UnverifiedRecoveryOrigin, "recovery origin is not verified");
    }
}

void normalize_draft(JournalRecordDraft &draft) {
    if (!schema_is_supported(draft.schema)) {
        reject(JournalStatus::UnsupportedSchema, "journal schema is unsupported");
    }
    require_identifier(draft.journal_id, "journal ID");
    require_identifier(draft.resident_id, "resident ID");
    require_identifier(draft.daemon_epoch, "daemon epoch");
    validate_operation(draft.operation);
    normalize_claim_closure(draft.claim_closure);
    sort_unique_identifiers(draft.action_lease_claim_ids, "action-lease claim IDs");
    sort_unique_identifiers(draft.ownership_claim_ids, "ownership claim IDs");
    sort_unique_identifiers(draft.recovery_claim_ids, "recovery claim IDs");
    validate_lifecycle(draft);
}

json claim_closure_document(const std::vector<ClaimFamilyClosure> &closure) {
    json result = json::array();
    for (const auto &family : closure) {
        json entries = json::array();
        for (const auto &entry : family.entries) {
            entries.push_back(json{
                {"amount", entry.amount},
                {"constraint_id", entry.constraint_id},
                {"unit", claim_unit_wire(entry.unit)},
            });
        }
        result.push_back(json{
            {"completeness", completeness_wire(family.completeness)},
            {"entries", std::move(entries)},
            {"family", claim_family_wire(family.family)},
        });
    }
    return result;
}

json record_payload(const JournalRecordDraft &draft, std::uint64_t sequence,
                    const std::optional<std::string> &predecessor_checksum) {
    json operation{
        {"family", wire_name(draft.operation.family)},
        {"kind", wire_name(draft.operation.kind)},
        {"operation_id", draft.operation.operation_id},
        {"plan_id",
         draft.operation.plan_id.has_value() ? json(*draft.operation.plan_id) : json(nullptr)},
    };
    json origin = nullptr;
    if (draft.quarantine_origin.has_value()) {
        origin = json{
            {"evidence_sha256", draft.quarantine_origin->evidence_sha256},
            {"kind", origin_kind_wire(draft.quarantine_origin->kind)},
        };
    }
    return json{
        {"action_lease_claim_ids", draft.action_lease_claim_ids},
        {"claim_closure", claim_closure_document(draft.claim_closure)},
        {"daemon_epoch", draft.daemon_epoch},
        {"journal_id", draft.journal_id},
        {"operation", std::move(operation)},
        {"ownership_claim_ids", draft.ownership_claim_ids},
        {"predecessor_checksum_sha256",
         predecessor_checksum.has_value() ? json(*predecessor_checksum) : json(nullptr)},
        {"quarantine_origin", std::move(origin)},
        {"recovery_claim_ids", draft.recovery_claim_ids},
        {"recovery_disposition", draft.recovery_disposition.has_value()
                                     ? json(disposition_wire(*draft.recovery_disposition))
                                     : json(nullptr)},
        {"resident_id", draft.resident_id},
        {"resident_state", resident_state_wire(draft.resident_state)},
        {"schema", schema_document(draft.schema)},
        {"sequence", sequence},
    };
}

std::string canonical_document(json payload, std::string_view checksum) {
    payload["checksum_sha256"] = checksum;
    return payload.dump();
}

struct SealedRecordData {
    JournalRecordDraft draft;
    std::uint64_t sequence;
    std::optional<std::string> predecessor_checksum;
    std::string checksum;
    std::string canonical;
};

SealedRecordData seal_record_data(JournalRecordDraft draft, std::uint64_t sequence,
                                  std::optional<std::string> predecessor_checksum) {
    const auto payload = record_payload(draft, sequence, predecessor_checksum);
    const auto payload_bytes = payload.dump();
    const auto checksum = sha256_hex(
        std::string_view(journal_record_domain, sizeof(journal_record_domain) - 1), payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    if (canonical.size() > max_journal_input_bytes) {
        reject(JournalStatus::LimitExceeded, "sealed journal record exceeds the input limit");
    }
    return SealedRecordData{std::move(draft), sequence, std::move(predecessor_checksum), checksum,
                            std::move(canonical)};
}

bool same_epoch_transition_is_valid(ResidentState previous, ResidentState next) noexcept {
    if (previous == next && previous != ResidentState::Released) {
        return true;
    }
    switch (previous) {
    case ResidentState::Prepared:
        return next == ResidentState::Provisional || next == ResidentState::Quarantined ||
               next == ResidentState::Released;
    case ResidentState::Provisional:
        return next == ResidentState::Active || next == ResidentState::Quarantined ||
               next == ResidentState::Released;
    case ResidentState::Active:
        return next == ResidentState::Suspended || next == ResidentState::Quarantined ||
               next == ResidentState::Released;
    case ResidentState::Suspended:
        return next == ResidentState::Active || next == ResidentState::Quarantined ||
               next == ResidentState::Released;
    case ResidentState::Quarantined:
        return next == ResidentState::Released;
    case ResidentState::Released:
        return false;
    }
    return false;
}

template <typename HistoryState>
void validate_history(const HistoryState &history, const ResidentHead *latest_resident,
                      std::string_view candidate_journal_id,
                      std::string_view candidate_daemon_epoch, ResidentState candidate_state,
                      const std::optional<RecoveryOrigin> &candidate_origin,
                      std::uint64_t candidate_sequence,
                      const std::optional<std::string> &candidate_predecessor) {
    if (candidate_journal_id != history.journal_id ||
        history.tip_sequence == std::numeric_limits<std::uint64_t>::max() ||
        candidate_sequence != history.tip_sequence + 1 ||
        candidate_predecessor != std::optional<std::string>{history.tip_checksum_sha256}) {
        reject(JournalStatus::InvalidHistory, "global journal predecessor is invalid");
    }

    if (history.stale_epoch_live_resident_count > history.live_resident_count) {
        reject(JournalStatus::InvalidHistory, "journal history counters are inconsistent");
    }
    const bool barrier_active = history.stale_epoch_live_resident_count != 0;
    const bool any_live_resident = history.live_resident_count != 0;
    const auto is_prior_epoch_quarantine = [&]() {
        return latest_resident != nullptr && latest_resident->state != ResidentState::Released &&
               latest_resident->daemon_epoch != candidate_daemon_epoch &&
               candidate_state == ResidentState::Quarantined && candidate_origin.has_value() &&
               candidate_origin->kind == RecoveryOriginKind::PriorEpochRecovery;
    };

    if (barrier_active) {
        if (candidate_daemon_epoch != history.daemon_epoch || latest_resident == nullptr ||
            latest_resident->daemon_epoch == history.daemon_epoch || !is_prior_epoch_quarantine()) {
            reject(JournalStatus::InvalidHistory,
                   "epoch barrier requires the next outstanding resident");
        }
        return;
    }

    if (candidate_daemon_epoch != history.daemon_epoch && any_live_resident) {
        if (!is_prior_epoch_quarantine()) {
            reject(JournalStatus::InvalidHistory,
                   "epoch change must begin with a prior-epoch quarantine");
        }
        return;
    }

    if (latest_resident == nullptr) {
        if (candidate_state != ResidentState::Prepared &&
            candidate_state != ResidentState::Quarantined) {
            reject(JournalStatus::InvalidHistory, "resident genesis state is invalid");
        }
        return;
    }

    if (latest_resident->state == ResidentState::Released) {
        reject(JournalStatus::InvalidHistory, "released residency is terminal");
    }
    if (latest_resident->daemon_epoch != candidate_daemon_epoch) {
        if (candidate_state != ResidentState::Quarantined || !candidate_origin.has_value() ||
            candidate_origin->kind != RecoveryOriginKind::PriorEpochRecovery) {
            reject(JournalStatus::InvalidHistory,
                   "epoch transition must enter recovery quarantine");
        }
        return;
    }
    if (!same_epoch_transition_is_valid(latest_resident->state, candidate_state)) {
        reject(JournalStatus::InvalidHistory, "resident lifecycle transition is invalid");
    }
}

std::vector<std::string> parse_identifier_array(const json &value, std::string_view label) {
    if (!value.is_array()) {
        reject(JournalStatus::InvalidValue, std::string(label) + " must be an array");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const auto &item : value) {
        result.push_back(require_string(item, label));
    }
    return result;
}

std::vector<ClaimFamilyClosure> parse_claim_closure(const json &value) {
    if (!value.is_array()) {
        reject(JournalStatus::InvalidClaimClosure, "claim closure must be an array");
    }
    std::vector<ClaimFamilyClosure> closure;
    closure.reserve(value.size());
    for (const auto &family_value : value) {
        require_exact_keys(family_value, {"completeness", "entries", "family"}, "claim family");
        ClaimFamilyClosure family;
        family.family =
            parse_claim_family(require_string(required(family_value, "family"), "claim family"));
        family.completeness = parse_completeness(
            require_string(required(family_value, "completeness"), "claim completeness"));
        const auto &entries = required(family_value, "entries");
        if (!entries.is_array()) {
            reject(JournalStatus::InvalidClaimClosure, "claim entries must be an array");
        }
        for (const auto &entry_value : entries) {
            require_exact_keys(entry_value, {"amount", "constraint_id", "unit"}, "claim amount");
            family.entries.push_back(ClaimAmount{
                require_string(required(entry_value, "constraint_id"), "constraint ID"),
                parse_claim_unit(require_string(required(entry_value, "unit"), "claim unit")),
                require_u64(required(entry_value, "amount"), "claim amount"),
            });
        }
        closure.push_back(std::move(family));
    }
    return closure;
}

OperationIdentity parse_operation(const json &value) {
    require_exact_keys(value, {"family", "kind", "operation_id", "plan_id"}, "operation identity");
    std::optional<std::string> plan_id;
    const auto &plan_value = required(value, "plan_id");
    if (!plan_value.is_null()) {
        plan_id = require_string(plan_value, "plan ID");
    }
    return OperationIdentity{
        require_string(required(value, "operation_id"), "operation ID"),
        std::move(plan_id),
        parse_operation_family_value(require_string(required(value, "family"), "operation family")),
        parse_operation_kind_value(require_string(required(value, "kind"), "operation kind")),
    };
}

std::optional<RecoveryOrigin> parse_origin(const json &value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    require_exact_keys(value, {"evidence_sha256", "kind"}, "quarantine origin");
    return RecoveryOrigin{
        parse_origin_kind(require_string(required(value, "kind"), "recovery origin kind")),
        require_string(required(value, "evidence_sha256"), "recovery evidence digest"),
    };
}

std::optional<RecoveryDisposition> parse_optional_disposition(const json &value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    return parse_disposition(require_string(value, "recovery disposition"));
}

ParsedJournalRecordResult rejected_record(const JournalFailure &failure) {
    return ParsedJournalRecordResult{failure.status(), bounded_diagnostic(failure.what()),
                                     std::nullopt};
}

AuthorityRootCandidateResult rejected_root_candidate(const JournalFailure &failure) {
    return AuthorityRootCandidateResult{failure.status(), bounded_diagnostic(failure.what()),
                                        std::nullopt};
}

JournalHistoryResult rejected_history(const JournalFailure &failure) {
    return JournalHistoryResult{
        failure.status(),
        bounded_diagnostic(failure.what()),
        std::nullopt,
    };
}

json root_candidate_payload(SchemaVersion schema, std::string_view journal_id,
                            std::string_view daemon_epoch, std::uint64_t generation,
                            std::uint64_t tip_sequence, std::string_view tip_checksum) {
    return json{
        {"daemon_epoch", daemon_epoch},
        {"generation", generation},
        {"journal_id", journal_id},
        {"schema", schema_document(schema)},
        {"tip_checksum_sha256", tip_checksum},
        {"tip_sequence", tip_sequence},
    };
}

struct SealedRootCandidateData {
    SchemaVersion schema;
    std::string journal_id;
    std::string daemon_epoch;
    std::uint64_t generation;
    std::uint64_t tip_sequence;
    std::string tip_checksum;
    std::string checksum;
    std::string canonical;
};

SealedRootCandidateData seal_root_candidate_data(SchemaVersion schema, std::string journal_id,
                                                 std::string daemon_epoch, std::uint64_t generation,
                                                 std::uint64_t tip_sequence,
                                                 std::string tip_checksum) {
    const auto payload = root_candidate_payload(schema, journal_id, daemon_epoch, generation,
                                                tip_sequence, tip_checksum);
    const auto payload_bytes = payload.dump();
    const auto checksum = sha256_hex(
        std::string_view(authority_root_domain, sizeof(authority_root_domain) - 1), payload_bytes);
    auto canonical = canonical_document(payload, checksum);
    return SealedRootCandidateData{schema,     std::move(journal_id), std::move(daemon_epoch),
                                   generation, tip_sequence,          std::move(tip_checksum),
                                   checksum,   std::move(canonical)};
}

bool root_candidate_is_self_consistent(const AuthorityRootCandidate &candidate) {
    if (!schema_is_supported(candidate.schema()) || !identifier_is_valid(candidate.journal_id()) ||
        !identifier_is_valid(candidate.daemon_epoch()) || candidate.generation() == 0 ||
        candidate.tip_sequence() == 0 || !is_lower_hex_digest(candidate.tip_checksum_sha256()) ||
        !is_lower_hex_digest(candidate.checksum_sha256())) {
        return false;
    }
    const auto payload = root_candidate_payload(
        candidate.schema(), candidate.journal_id(), candidate.daemon_epoch(),
        candidate.generation(), candidate.tip_sequence(), candidate.tip_checksum_sha256());
    if (canonical_document(payload, candidate.checksum_sha256()) != candidate.canonical_bytes()) {
        return false;
    }
    return candidate.checksum_sha256() ==
           sha256_hex(std::string_view(authority_root_domain, sizeof(authority_root_domain) - 1),
                      payload.dump());
}

template <typename HistoryState>
void validate_root_candidate_history(const HistoryState &history,
                                     const AuthorityRootCandidate *previous_root) {
    if (previous_root == nullptr) {
        if (history.tip_sequence != 1 || history.tip_predecessor_checksum_sha256.has_value()) {
            reject(JournalStatus::InvalidHistory, "genesis root-candidate tip is invalid");
        }
        return;
    }
    if (!root_candidate_is_self_consistent(*previous_root) ||
        previous_root->journal_id() != history.journal_id ||
        previous_root->generation() == std::numeric_limits<std::uint64_t>::max() ||
        previous_root->tip_sequence() == std::numeric_limits<std::uint64_t>::max() ||
        history.tip_sequence != previous_root->tip_sequence() + 1 ||
        history.tip_predecessor_checksum_sha256 !=
            std::optional<std::string>{std::string(previous_root->tip_checksum_sha256())}) {
        reject(JournalStatus::InvalidHistory,
               "authority-root candidate does not advance one stream record");
    }
}

} // namespace

ParsedJournalRecord::ParsedJournalRecord(JournalRecordDraft draft, std::uint64_t sequence,
                                         std::optional<std::string> predecessor_checksum_sha256,
                                         std::string checksum_sha256, std::string canonical_bytes)
    : draft_(std::move(draft)), sequence_(sequence),
      predecessor_checksum_sha256_(std::move(predecessor_checksum_sha256)),
      checksum_sha256_(std::move(checksum_sha256)), canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion ParsedJournalRecord::schema() const noexcept { return draft_.schema; }
std::string_view ParsedJournalRecord::journal_id() const noexcept { return draft_.journal_id; }
std::string_view ParsedJournalRecord::resident_id() const noexcept { return draft_.resident_id; }
std::string_view ParsedJournalRecord::daemon_epoch() const noexcept { return draft_.daemon_epoch; }
std::uint64_t ParsedJournalRecord::sequence() const noexcept { return sequence_; }
const std::optional<std::string> &
ParsedJournalRecord::predecessor_checksum_sha256() const noexcept {
    return predecessor_checksum_sha256_;
}
const OperationIdentity &ParsedJournalRecord::operation() const noexcept {
    return draft_.operation;
}
const std::vector<ClaimFamilyClosure> &ParsedJournalRecord::claim_closure() const noexcept {
    return draft_.claim_closure;
}
ResidentState ParsedJournalRecord::resident_state() const noexcept { return draft_.resident_state; }
const std::optional<RecoveryDisposition> &
ParsedJournalRecord::recovery_disposition() const noexcept {
    return draft_.recovery_disposition;
}
const std::optional<RecoveryOrigin> &ParsedJournalRecord::quarantine_origin() const noexcept {
    return draft_.quarantine_origin;
}
const std::vector<std::string> &ParsedJournalRecord::action_lease_claim_ids() const noexcept {
    return draft_.action_lease_claim_ids;
}
const std::vector<std::string> &ParsedJournalRecord::ownership_claim_ids() const noexcept {
    return draft_.ownership_claim_ids;
}
const std::vector<std::string> &ParsedJournalRecord::recovery_claim_ids() const noexcept {
    return draft_.recovery_claim_ids;
}
std::string_view ParsedJournalRecord::checksum_sha256() const noexcept { return checksum_sha256_; }
std::string_view ParsedJournalRecord::canonical_bytes() const noexcept { return canonical_bytes_; }

bool ParsedJournalRecordResult::accepted() const noexcept {
    return status == JournalStatus::Accepted && candidate.has_value();
}

JournalHistory::JournalHistory(std::unique_ptr<State> state) : state_(std::move(state)) {}

JournalHistory::~JournalHistory() = default;
JournalHistory::JournalHistory(JournalHistory &&) noexcept = default;
JournalHistory &JournalHistory::operator=(JournalHistory &&) noexcept = default;

bool JournalHistory::available() const noexcept { return state_ != nullptr; }
std::string_view JournalHistory::journal_id() const noexcept {
    return state_ ? std::string_view(state_->journal_id) : std::string_view{};
}
std::string_view JournalHistory::daemon_epoch() const noexcept {
    return state_ ? std::string_view(state_->daemon_epoch) : std::string_view{};
}
std::uint64_t JournalHistory::tip_sequence() const noexcept {
    return state_ ? state_->tip_sequence : 0;
}
std::string_view JournalHistory::tip_checksum_sha256() const noexcept {
    return state_ ? std::string_view(state_->tip_checksum_sha256) : std::string_view{};
}

bool JournalHistoryResult::accepted() const noexcept {
    return status == JournalStatus::Accepted && history.has_value() && history->available();
}

AuthorityRootCandidate::AuthorityRootCandidate(SchemaVersion schema, std::string journal_id,
                                               std::string daemon_epoch, std::uint64_t generation,
                                               std::uint64_t tip_sequence,
                                               std::string tip_checksum_sha256,
                                               std::string checksum_sha256,
                                               std::string canonical_bytes)
    : schema_(schema), journal_id_(std::move(journal_id)), daemon_epoch_(std::move(daemon_epoch)),
      generation_(generation), tip_sequence_(tip_sequence),
      tip_checksum_sha256_(std::move(tip_checksum_sha256)),
      checksum_sha256_(std::move(checksum_sha256)), canonical_bytes_(std::move(canonical_bytes)) {}

SchemaVersion AuthorityRootCandidate::schema() const noexcept { return schema_; }
std::string_view AuthorityRootCandidate::journal_id() const noexcept { return journal_id_; }
std::string_view AuthorityRootCandidate::daemon_epoch() const noexcept { return daemon_epoch_; }
std::uint64_t AuthorityRootCandidate::generation() const noexcept { return generation_; }
std::uint64_t AuthorityRootCandidate::tip_sequence() const noexcept { return tip_sequence_; }
std::string_view AuthorityRootCandidate::tip_checksum_sha256() const noexcept {
    return tip_checksum_sha256_;
}
std::string_view AuthorityRootCandidate::checksum_sha256() const noexcept {
    return checksum_sha256_;
}
std::string_view AuthorityRootCandidate::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

bool AuthorityRootCandidateResult::accepted() const noexcept {
    return status == JournalStatus::Accepted && candidate.has_value();
}

ParsedJournalRecordResult seal_genesis(JournalRecordDraft draft,
                                       const RecoveryOriginVerifier *verifier) {
    try {
        normalize_draft(draft);
        verify_recovery_origin(draft.journal_id, draft.resident_id, draft.daemon_epoch,
                               draft.quarantine_origin, verifier);
        if (draft.resident_state != ResidentState::Prepared &&
            draft.resident_state != ResidentState::Quarantined) {
            reject(JournalStatus::InvalidLifecycle, "global genesis state is invalid");
        }
        auto sealed = seal_record_data(std::move(draft), 1, std::nullopt);
        return ParsedJournalRecordResult{
            JournalStatus::Accepted,
            {},
            ParsedJournalRecord(std::move(sealed.draft), sealed.sequence,
                                std::move(sealed.predecessor_checksum), std::move(sealed.checksum),
                                std::move(sealed.canonical)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_record(failure);
    }
}

ParsedJournalRecordResult seal_successor(const JournalHistory &history, JournalRecordDraft draft,
                                         const RecoveryOriginVerifier *verifier) {
    try {
        if (!history.state_) {
            reject(JournalStatus::InvalidHistory, "journal history is unavailable");
        }
        normalize_draft(draft);
        if (history.state_->tip_sequence == std::numeric_limits<std::uint64_t>::max()) {
            reject(JournalStatus::InvalidHistory, "global journal sequence overflowed");
        }
        const auto sequence = history.state_->tip_sequence + 1;
        const std::optional<std::string> predecessor{history.state_->tip_checksum_sha256};
        const auto resident = history.state_->resident_heads.find(draft.resident_id);
        const auto *latest_resident =
            resident == history.state_->resident_heads.end() ? nullptr : &resident->second;
        validate_history(*history.state_, latest_resident, draft.journal_id, draft.daemon_epoch,
                         draft.resident_state, draft.quarantine_origin, sequence, predecessor);
        verify_recovery_origin(draft.journal_id, draft.resident_id, draft.daemon_epoch,
                               draft.quarantine_origin, verifier);
        auto sealed = seal_record_data(std::move(draft), sequence, predecessor);
        return ParsedJournalRecordResult{
            JournalStatus::Accepted,
            {},
            ParsedJournalRecord(std::move(sealed.draft), sealed.sequence,
                                std::move(sealed.predecessor_checksum), std::move(sealed.checksum),
                                std::move(sealed.canonical)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_record(failure);
    }
}

ParsedJournalRecordResult parse_record_candidate(std::string_view bytes) {
    try {
        const auto document = parse_json(bytes);
        require_exact_keys(document,
                           {
                               "action_lease_claim_ids",
                               "checksum_sha256",
                               "claim_closure",
                               "daemon_epoch",
                               "journal_id",
                               "operation",
                               "ownership_claim_ids",
                               "predecessor_checksum_sha256",
                               "quarantine_origin",
                               "recovery_claim_ids",
                               "recovery_disposition",
                               "resident_id",
                               "resident_state",
                               "schema",
                               "sequence",
                           },
                           "journal record");

        const auto checksum = require_string(required(document, "checksum_sha256"), "checksum");
        if (!is_lower_hex_digest(checksum)) {
            reject(JournalStatus::InvalidValue, "journal checksum is invalid");
        }
        JournalRecordDraft draft;
        draft.schema = parse_schema(required(document, "schema"));
        draft.journal_id = require_string(required(document, "journal_id"), "journal ID");
        draft.resident_id = require_string(required(document, "resident_id"), "resident ID");
        draft.daemon_epoch = require_string(required(document, "daemon_epoch"), "daemon epoch");
        draft.operation = parse_operation(required(document, "operation"));
        draft.claim_closure = parse_claim_closure(required(document, "claim_closure"));
        draft.resident_state = parse_resident_state(
            require_string(required(document, "resident_state"), "resident state"));
        draft.recovery_disposition =
            parse_optional_disposition(required(document, "recovery_disposition"));
        draft.quarantine_origin = parse_origin(required(document, "quarantine_origin"));
        draft.action_lease_claim_ids = parse_identifier_array(
            required(document, "action_lease_claim_ids"), "action-lease claim IDs");
        draft.ownership_claim_ids = parse_identifier_array(
            required(document, "ownership_claim_ids"), "ownership claim IDs");
        draft.recovery_claim_ids =
            parse_identifier_array(required(document, "recovery_claim_ids"), "recovery claim IDs");
        const auto sequence = require_u64(required(document, "sequence"), "sequence");
        std::optional<std::string> predecessor;
        const auto &predecessor_value = required(document, "predecessor_checksum_sha256");
        if (!predecessor_value.is_null()) {
            predecessor = require_string(predecessor_value, "predecessor checksum");
            if (!is_lower_hex_digest(*predecessor)) {
                reject(JournalStatus::InvalidValue, "predecessor checksum is invalid");
            }
        }
        if (sequence == 0 || ((sequence == 1) != !predecessor.has_value())) {
            reject(JournalStatus::InvalidValue, "sequence and predecessor are inconsistent");
        }

        normalize_draft(draft);
        const auto payload = record_payload(draft, sequence, predecessor);
        if (canonical_document(payload, checksum) != bytes) {
            reject(JournalStatus::NonCanonical, "journal record is not canonical");
        }
        const auto expected_checksum =
            sha256_hex(std::string_view(journal_record_domain, sizeof(journal_record_domain) - 1),
                       payload.dump());
        if (checksum != expected_checksum) {
            reject(JournalStatus::ChecksumMismatch, "journal checksum does not match");
        }
        return ParsedJournalRecordResult{
            JournalStatus::Accepted,
            {},
            ParsedJournalRecord(std::move(draft), sequence, std::move(predecessor), checksum,
                                std::string(bytes)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_record(failure);
    }
}

namespace {

bool same_operation(const OperationIdentity &left, const OperationIdentity &right) {
    return left.operation_id == right.operation_id && left.plan_id == right.plan_id &&
           left.family == right.family && left.kind == right.kind;
}

bool same_claim_closure(const std::vector<ClaimFamilyClosure> &left,
                        const std::vector<ClaimFamilyClosure> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t family_index = 0; family_index < left.size(); ++family_index) {
        const auto &left_family = left[family_index];
        const auto &right_family = right[family_index];
        if (left_family.family != right_family.family ||
            left_family.completeness != right_family.completeness ||
            left_family.entries.size() != right_family.entries.size()) {
            return false;
        }
        for (std::size_t entry_index = 0; entry_index < left_family.entries.size(); ++entry_index) {
            const auto &left_entry = left_family.entries[entry_index];
            const auto &right_entry = right_family.entries[entry_index];
            if (left_entry.constraint_id != right_entry.constraint_id ||
                left_entry.unit != right_entry.unit || left_entry.amount != right_entry.amount) {
                return false;
            }
        }
    }
    return true;
}

bool same_origin(const std::optional<RecoveryOrigin> &left,
                 const std::optional<RecoveryOrigin> &right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind && left->evidence_sha256 == right->evidence_sha256);
}

void require_self_consistent_candidate(const ParsedJournalRecord &candidate) {
    const auto reparsed = parse_record_candidate(candidate.canonical_bytes());
    if (!reparsed.accepted()) {
        reject(JournalStatus::InvalidHistory, "journal candidate is unavailable");
    }
    const auto &trusted = *reparsed.candidate;
    const auto candidate_schema = candidate.schema();
    const auto trusted_schema = trusted.schema();
    if (candidate_schema.major != trusted_schema.major ||
        candidate_schema.minor != trusted_schema.minor ||
        candidate.journal_id() != trusted.journal_id() ||
        candidate.resident_id() != trusted.resident_id() ||
        candidate.daemon_epoch() != trusted.daemon_epoch() ||
        candidate.sequence() != trusted.sequence() ||
        candidate.predecessor_checksum_sha256() != trusted.predecessor_checksum_sha256() ||
        !same_operation(candidate.operation(), trusted.operation()) ||
        !same_claim_closure(candidate.claim_closure(), trusted.claim_closure()) ||
        candidate.resident_state() != trusted.resident_state() ||
        candidate.recovery_disposition() != trusted.recovery_disposition() ||
        !same_origin(candidate.quarantine_origin(), trusted.quarantine_origin()) ||
        candidate.action_lease_claim_ids() != trusted.action_lease_claim_ids() ||
        candidate.ownership_claim_ids() != trusted.ownership_claim_ids() ||
        candidate.recovery_claim_ids() != trusted.recovery_claim_ids() ||
        candidate.checksum_sha256() != trusted.checksum_sha256()) {
        reject(JournalStatus::InvalidHistory, "journal candidate identity is inconsistent");
    }
}

} // namespace

JournalHistoryResult begin_history(const ParsedJournalRecord &genesis,
                                   const RecoveryOriginVerifier *verifier) {
    try {
        require_self_consistent_candidate(genesis);
        if (genesis.sequence() != 1 || genesis.predecessor_checksum_sha256().has_value() ||
            (genesis.resident_state() != ResidentState::Prepared &&
             genesis.resident_state() != ResidentState::Quarantined)) {
            reject(JournalStatus::InvalidHistory, "journal genesis is invalid");
        }
        verify_recovery_origin(genesis.journal_id(), genesis.resident_id(), genesis.daemon_epoch(),
                               genesis.quarantine_origin(), verifier);
        auto state = std::make_unique<JournalHistory::State>();
        state->journal_id = genesis.journal_id();
        state->daemon_epoch = genesis.daemon_epoch();
        state->tip_sequence = genesis.sequence();
        state->tip_predecessor_checksum_sha256 = genesis.predecessor_checksum_sha256();
        state->tip_checksum_sha256 = genesis.checksum_sha256();
        state->resident_heads.emplace(std::string(genesis.resident_id()),
                                      ResidentHead{
                                          std::string(genesis.daemon_epoch()),
                                          genesis.resident_state(),
                                      });
        state->live_resident_count = 1;
        state->stale_epoch_live_resident_count = 0;
        return JournalHistoryResult{
            JournalStatus::Accepted,
            {},
            JournalHistory(std::move(state)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_history(failure);
    }
}

JournalHistoryResult advance_history(JournalHistory &&history, const ParsedJournalRecord &candidate,
                                     const RecoveryOriginVerifier *verifier) {
    try {
        if (!history.state_) {
            reject(JournalStatus::InvalidHistory, "journal history is unavailable");
        }
        require_self_consistent_candidate(candidate);
        const std::string resident_id(candidate.resident_id());
        const auto resident = history.state_->resident_heads.find(resident_id);
        const auto *latest_resident =
            resident == history.state_->resident_heads.end() ? nullptr : &resident->second;
        validate_history(*history.state_, latest_resident, candidate.journal_id(),
                         candidate.daemon_epoch(), candidate.resident_state(),
                         candidate.quarantine_origin(), candidate.sequence(),
                         candidate.predecessor_checksum_sha256());
        verify_recovery_origin(candidate.journal_id(), candidate.resident_id(),
                               candidate.daemon_epoch(), candidate.quarantine_origin(), verifier);
        const bool epoch_changed = candidate.daemon_epoch() != history.state_->daemon_epoch;
        const bool new_resident = resident == history.state_->resident_heads.end();
        const bool latest_was_stale = !new_resident &&
                                      resident->second.state != ResidentState::Released &&
                                      resident->second.daemon_epoch != history.state_->daemon_epoch;
        std::string next_daemon_epoch(candidate.daemon_epoch());
        std::string next_tip_checksum(candidate.checksum_sha256());
        auto next_predecessor = candidate.predecessor_checksum_sha256();
        ResidentHead next_head{
            std::string(candidate.daemon_epoch()),
            candidate.resident_state(),
        };

        if (new_resident) {
            history.state_->resident_heads.emplace(resident_id, std::move(next_head));
        } else {
            resident->second = std::move(next_head);
        }
        if (epoch_changed) {
            if (history.state_->live_resident_count == 0) {
                history.state_->live_resident_count = 1;
                history.state_->stale_epoch_live_resident_count = 0;
            } else {
                history.state_->stale_epoch_live_resident_count =
                    history.state_->live_resident_count - 1;
            }
        } else {
            if (new_resident) {
                ++history.state_->live_resident_count;
            } else if (latest_was_stale) {
                --history.state_->stale_epoch_live_resident_count;
            }
            if (candidate.resident_state() == ResidentState::Released) {
                --history.state_->live_resident_count;
            }
        }
        history.state_->daemon_epoch = std::move(next_daemon_epoch);
        history.state_->tip_sequence = candidate.sequence();
        history.state_->tip_predecessor_checksum_sha256 = std::move(next_predecessor);
        history.state_->tip_checksum_sha256 = std::move(next_tip_checksum);
        return JournalHistoryResult{
            JournalStatus::Accepted,
            {},
            std::move(history),
        };
    } catch (const JournalFailure &failure) {
        return rejected_history(failure);
    }
}

AuthorityRootCandidateResult
seal_authority_root_candidate(const JournalHistory &history,
                              const AuthorityRootCandidate *previous_root) {
    try {
        if (!history.state_) {
            reject(JournalStatus::InvalidHistory, "journal history is unavailable");
        }
        validate_root_candidate_history(*history.state_, previous_root);
        const auto generation = previous_root == nullptr ? 1 : previous_root->generation() + 1;
        auto sealed = seal_root_candidate_data(
            supported_journal_schema, history.state_->journal_id, history.state_->daemon_epoch,
            generation, history.state_->tip_sequence, history.state_->tip_checksum_sha256);
        return AuthorityRootCandidateResult{
            JournalStatus::Accepted,
            {},
            AuthorityRootCandidate(sealed.schema, std::move(sealed.journal_id),
                                   std::move(sealed.daemon_epoch), sealed.generation,
                                   sealed.tip_sequence, std::move(sealed.tip_checksum),
                                   std::move(sealed.checksum), std::move(sealed.canonical)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_root_candidate(failure);
    }
}

AuthorityRootCandidateResult
parse_authority_root_candidate(std::string_view bytes, const JournalHistory &history,
                               const AuthorityRootCandidate *previous_root) {
    try {
        const auto document = parse_json(bytes);
        require_exact_keys(document,
                           {
                               "checksum_sha256",
                               "daemon_epoch",
                               "generation",
                               "journal_id",
                               "schema",
                               "tip_checksum_sha256",
                               "tip_sequence",
                           },
                           "authority-root candidate");
        const auto schema = parse_schema(required(document, "schema"));
        const auto journal_id = require_string(required(document, "journal_id"), "journal ID");
        const auto daemon_epoch =
            require_string(required(document, "daemon_epoch"), "daemon epoch");
        require_identifier(journal_id, "journal ID");
        require_identifier(daemon_epoch, "daemon epoch");
        const auto generation =
            require_u64(required(document, "generation"), "root-candidate generation");
        const auto tip_sequence = require_u64(required(document, "tip_sequence"), "tip sequence");
        const auto tip_checksum =
            require_string(required(document, "tip_checksum_sha256"), "tip checksum");
        const auto checksum =
            require_string(required(document, "checksum_sha256"), "root-candidate checksum");
        if (generation == 0 || tip_sequence == 0 || !is_lower_hex_digest(tip_checksum) ||
            !is_lower_hex_digest(checksum)) {
            reject(JournalStatus::InvalidValue, "authority-root-candidate identity is invalid");
        }

        const auto payload = root_candidate_payload(schema, journal_id, daemon_epoch, generation,
                                                    tip_sequence, tip_checksum);
        if (canonical_document(payload, checksum) != bytes) {
            reject(JournalStatus::NonCanonical, "authority-root candidate is not canonical");
        }
        const auto expected_checksum =
            sha256_hex(std::string_view(authority_root_domain, sizeof(authority_root_domain) - 1),
                       payload.dump());
        if (checksum != expected_checksum) {
            reject(JournalStatus::ChecksumMismatch,
                   "authority-root-candidate checksum does not match");
        }

        const auto expected = seal_authority_root_candidate(history, previous_root);
        if (!expected.accepted() || expected.candidate->journal_id() != journal_id ||
            expected.candidate->daemon_epoch() != daemon_epoch ||
            expected.candidate->generation() != generation ||
            expected.candidate->tip_sequence() != tip_sequence ||
            expected.candidate->tip_checksum_sha256() != tip_checksum ||
            expected.candidate->checksum_sha256() != checksum) {
            reject(JournalStatus::InvalidHistory,
                   "authority-root candidate does not match its history tip");
        }
        return AuthorityRootCandidateResult{
            JournalStatus::Accepted,
            {},
            AuthorityRootCandidate(schema, journal_id, daemon_epoch, generation, tip_sequence,
                                   tip_checksum, checksum, std::string(bytes)),
        };
    } catch (const JournalFailure &failure) {
        return rejected_root_candidate(failure);
    }
}

} // namespace lemon::residency
