#pragma once

#ifndef LEMONADE_RESIDENCY_DURABLE_TESTING
#error "durable journal test support is confined to the test target"
#endif

#include "lemon/residency/durable_journal.h"
#include "lemon/residency/durable_local_overlay.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lemon::residency::testing {

enum class PlatformContract { Linux, Windows, MacOS };

enum class FaultPosition { Before, After };

enum class FaultAction {
    Error,
    Crash,
    EffectThenError,
    ShortWrite,
    InterruptedOnce,
    ZeroProgress,
    OverreportedWrite,
};

enum class FaultOperation {
    PreflightProbe,
    PreflightFlushCreate,
    PreflightFlushWrite,
    PreflightFlushFileFlush,
    PreflightFlushClose,
    PreflightTruncate,
    PreflightTruncateFlush,
    PreflightTruncateClose,
    PreflightReplaceStageCreate,
    PreflightReplaceStageWrite,
    PreflightReplaceStageFlush,
    PreflightReplaceStageClose,
    PreflightReplace,
    PreflightNamespaceDurability,
    PreflightCleanup,
    FixedNamespaceInspect,
    AuthorityLockOpen,
    AuthorityLockAcquire,
    AuthorityIdentityRead,
    RootOpen,
    RootRead,
    RootClose,
    JournalOpen,
    JournalRead,
    JournalReadClose,
    ObjectOpen,
    ObjectRead,
    ObjectReadClose,
    ObjectStageCreate,
    ObjectWrite,
    ObjectFlush,
    ObjectClose,
    ObjectPublish,
    ObjectNamespaceDurability,
    ObjectStageCleanup,
    JournalAppendOpen,
    InitialJournalCreate,
    JournalWrite,
    JournalFlush,
    JournalClose,
    RootStageCreate,
    RootStageWrite,
    RootStageFlush,
    RootStageClose,
    RootReplace,
    NamespaceDurability,
    AuthorityUnlock,
    AuthorityLockClose,
    TailRepairOpen,
    TailRepairTruncate,
    TailRepairFlush,
    TailRepairClose,
    CompactionStageCreate,
    CompactionWrite,
    CompactionFlush,
    CompactionClose,
    CompactionReplace,
    CompactionNamespaceDurability,
};

enum class DurabilityCapability {
    RegularFileFlush,
    TailTruncateAndFlush,
    SameDirectoryReplace,
    NamespaceDurability,
};

enum class FixedAuthorityChild { Journal, Root, Lock, JournalStage, RootStage };

enum class UnsupportedEntryKind { Symlink, NonRegular, ReparsePoint };

struct OperationObservation {
    FaultOperation operation;
    bool lock_held;
    bool mutation_attempted;
    std::uint64_t lock_identity;
    std::size_t requested_bytes;
    std::size_t transferred_bytes;
};

struct NamespaceSnapshot {
    std::string journal_bytes;
    std::string authority_root_bytes;
    std::set<std::string> relative_paths;
    std::set<std::string> durable_paths;
    std::set<std::string> durable_content_available;
    std::map<std::string, std::string> entry_kinds;
    std::map<std::string, std::string> live_file_bytes;
    std::map<std::string, std::string> durable_file_bytes;
    std::set<std::string> open_handles;
    std::vector<OperationObservation> observations;
    bool namespace_durable;
    bool journal_durable;
    bool authority_root_durable;
    bool journal_closed;
    bool stage_files_absent;
    bool durable_stage_files_absent;
    bool all_authority_io_under_lock;
    bool lock_file_present;
    bool lock_file_stable;
    bool root_read_before_journal_read;
    bool authority_mutation_attempted;
};

enum class HelperResultKind {
    Succeeded,
    Interrupted,
    FailedBeforeEffect,
    EffectMayHaveOccurred,
};

struct HelperProbeResult {
    HelperResultKind result;
    std::string bytes;
    std::size_t attempts;
};

struct HelperOrchestrationProbeResult {
    HelperResultKind result;
    std::string bytes;
    std::size_t write_attempts;
    std::size_t flush_attempts;
    std::size_t truncate_attempts;
    std::size_t close_attempts;
};

struct ReadHelperProbeResult {
    HelperResultKind result;
    std::string bytes;
    bool truncated;
    std::vector<std::size_t> requested_bytes;
    std::vector<std::size_t> returned_bytes;
    std::size_t read_attempts;
    std::size_t close_attempts;
};

struct NativeLockPathProbeResult {
    bool replacement_prevented_while_held;
    bool stale_identity_rejected;
    bool nonblocking_probe_rejected;
    bool first_unlock_succeeded;
    bool contender_succeeded;
    bool contender_rebound;
    bool replacement_after_release_rebound;
};

struct ImmutableObjectLinkedPublishProbeResult {
    HelperResultKind creation_result;
    bool linked_names_survived_restart;
    bool read_succeeded;
    bool read_was_unsupported;
    bool authority_released;
};

bool operator==(const NamespaceSnapshot &left, const NamespaceSnapshot &right);
bool same_persistent_state(const NamespaceSnapshot &left,
                           const NamespaceSnapshot &right);
HelperProbeResult probe_write_all(std::string bytes,
                                  std::vector<FaultAction> script);
HelperProbeResult probe_flush_retry(std::vector<FaultAction> script);
HelperProbeResult probe_truncate_retry(std::string bytes,
                                       std::size_t truncate_to,
                                       std::vector<FaultAction> script);
HelperProbeResult probe_close_once(std::vector<FaultAction> script);
HelperOrchestrationProbeResult
probe_write_flush_close(std::string bytes,
                        std::vector<FaultAction> write_script,
                        std::vector<FaultAction> flush_script,
                        std::vector<FaultAction> close_script);
HelperOrchestrationProbeResult
probe_truncate_flush_close(std::string bytes, std::size_t truncate_to,
                           std::vector<FaultAction> truncate_script,
                           std::vector<FaultAction> flush_script,
                           std::vector<FaultAction> close_script);
ReadHelperProbeResult
probe_read_bounded_close(std::string bytes, std::size_t max_bytes,
                         std::vector<FaultAction> read_script,
                         std::vector<FaultAction> close_script);
NativeLockPathProbeResult
probe_native_lock_path_revalidation(std::string directory);

class JournalTestStorage {
public:
    static JournalTestStorage fresh();
    static JournalTestStorage fresh(PlatformContract platform);
    static JournalTestStorage
    seeded(std::string journal_bytes,
           std::optional<std::string> authority_root_bytes);
    static JournalTestStorage seeded(std::string journal_bytes,
                                     std::string authority_root_bytes);
    static JournalTestStorage seeded(std::string journal_bytes,
                                     std::string authority_root_bytes,
                                     PlatformContract platform);
    static JournalTestStorage
    missing_authority_directory(PlatformContract platform);
    static ImmutableObjectLinkedPublishProbeResult
    probe_immutable_object_linked_publish_read(PlatformContract platform);

    JournalTestStorage(const JournalTestStorage &);
    JournalTestStorage &operator=(const JournalTestStorage &);
    JournalTestStorage(JournalTestStorage &&) noexcept;
    JournalTestStorage &operator=(JournalTestStorage &&) noexcept;
    ~JournalTestStorage();

    JournalTestStorage clone() const;
    DurableJournal make_journal(JournalLimits limits);
    LocalOverlayStore make_overlay_store(LocalOverlayStoreLimits limits);
    NamespaceSnapshot snapshot() const;
    void seed_untrusted_file(std::string name, std::string bytes);
    void overwrite_immutable_object(std::string sha256, std::string bytes);
    void remove_immutable_object(std::string sha256);
    void overwrite_fixed_child_bytes(FixedAuthorityChild child,
                                     std::string bytes);
    void replace_fixed_child_file(FixedAuthorityChild child,
                                  std::string bytes);
    void remove_fixed_child_file(FixedAuthorityChild child);
    void rename_and_replace_authority_directory();

    void arm_fault(FaultOperation operation, FaultPosition position,
                   FaultAction action);
    void arm_fault_sequence(FaultOperation operation, FaultPosition position,
                            std::vector<FaultAction> actions);
    void restart();
    void reset_observations();
    std::size_t attempt_count(FaultOperation operation) const;

    void make_capability_unavailable(DurabilityCapability capability);
    void poison_fixed_child(FixedAuthorityChild child,
                            UnsupportedEntryKind kind);

    void pause_after(FaultOperation operation);
    void pause_after_nth(FaultOperation operation, std::size_t occurrence);
    bool destroy_adapter_with_open_lock(bool simulated_crash);
    bool wait_until_paused(std::uint64_t timeout_milliseconds);
    bool wait_until_lock_waiter(std::uint64_t timeout_milliseconds) const;
    void release_pause();
    bool try_unlink_lock_file();
    bool try_recreate_lock_file();
    bool try_replace_lock_with_reparse_point();
    void alias_lock_identity_from(const JournalTestStorage &other);
    std::uint64_t lock_identity() const;

private:
    struct State;
    class Adapter;
    explicit JournalTestStorage(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

} // namespace lemon::residency::testing
