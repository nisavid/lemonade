#pragma once

#include "lemon/residency/durable_journal.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency::detail {

inline constexpr std::size_t durable_read_chunk_bytes = 64 * 1024;

enum class DurableFileStatus {
    Succeeded,
    NotFound,
    AlreadyExists,
    Unsupported,
    Interrupted,
    FailedBeforeEffect,
    EffectMayHaveOccurred,
};

struct DurableFileResult {
    DurableFileStatus status = DurableFileStatus::Unsupported;
    std::string diagnostic;

    bool succeeded() const noexcept;
    bool effect_may_have_occurred() const noexcept;
};

struct DurableReadResult {
    DurableFileResult result;
    std::string bytes;
    bool truncated = false;
};

struct DurableReadChunkResult {
    DurableFileResult result;
    std::string bytes;
    bool end_of_file = false;
};

struct DurableIdentityResult {
    DurableFileResult result;
    std::string identity;
};

struct DurableFixedNamespaceResult {
    DurableFileResult result;
    bool journal_present;
    bool root_present;
    bool journal_stage_present;
    bool root_stage_present;
    bool lock_present;
};

struct DurableWriteResult {
    DurableFileResult result;
    std::size_t bytes_written = 0;
};

class DurableFileChannel {
public:
    virtual ~DurableFileChannel() = default;

    virtual DurableWriteResult write_some(std::string_view bytes) = 0;
    virtual DurableFileResult flush() = 0;
    virtual DurableFileResult truncate(std::size_t bytes) = 0;
    virtual DurableFileResult close() = 0;
};

class DurableReadChannel {
public:
    virtual ~DurableReadChannel() = default;

    virtual DurableReadChunkResult read_some(std::size_t max_bytes) = 0;
    virtual DurableFileResult close() = 0;
};

class DurableInterruptibleCall {
public:
    virtual ~DurableInterruptibleCall() = default;
    virtual DurableFileResult attempt() = 0;
};

DurableFileResult retry_interrupted(DurableInterruptibleCall &call);
DurableFileResult write_all(DurableFileChannel &channel,
                            std::string_view bytes);
DurableFileResult flush_retrying_interrupts(DurableFileChannel &channel);
DurableFileResult truncate_retrying_interrupts(DurableFileChannel &channel,
                                               std::size_t bytes);
DurableFileResult close_once(DurableFileChannel &channel);
DurableFileResult close_read_once(DurableReadChannel &channel);
DurableFileResult write_flush_close(DurableFileChannel &channel,
                                    std::string_view bytes);
DurableFileResult truncate_flush_close(DurableFileChannel &channel,
                                       std::size_t bytes);
DurableReadResult read_bounded_close(DurableReadChannel &channel,
                                     std::size_t max_bytes);
std::optional<std::string>
durable_immutable_object_filename(std::string_view sha256);
std::optional<std::string>
durable_immutable_object_stage_filename(std::string_view sha256);
bool durable_fixed_namespace_name_is_valid(
    std::string_view name) noexcept;

class DurableFileAdapter {
public:
    virtual ~DurableFileAdapter() = default;

    virtual DurableFileResult lock_authority() = 0;
    virtual DurableIdentityResult authority_identity() = 0;
    virtual DurableFileResult preflight_capabilities() = 0;
    virtual DurableFixedNamespaceResult inspect_fixed_namespace() = 0;
    virtual DurableReadResult read_root(std::size_t max_bytes) = 0;
    virtual DurableReadResult read_journal(std::size_t max_bytes) = 0;
    virtual DurableReadResult
    read_immutable_object(std::string_view sha256,
                          std::size_t max_bytes) = 0;
    virtual DurableFileResult
    create_immutable_object(std::string_view sha256,
                            std::string_view bytes) = 0;
    virtual DurableFileResult create_journal(std::string_view bytes) = 0;
    virtual DurableFileResult append_journal(std::string_view bytes) = 0;
    virtual DurableFileResult truncate_journal(std::size_t bytes) = 0;
    virtual DurableFileResult replace_journal(std::string_view bytes) = 0;
    virtual DurableFileResult replace_root(std::string_view bytes) = 0;
    virtual DurableFileResult unlock_authority() = 0;
};

std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter(const std::filesystem::path &directory);
std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter_in_fixed_namespace(
    const std::filesystem::path &parent_directory,
    std::string_view child_namespace);

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
enum class DurablePreflightTestFault {
    ProbeIdentityCaptureFailureOnce,
    StageIdentityCaptureFailureOnce,
    StageCreateCollisionExhaustion,
};

std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter_for_test(
    const std::filesystem::path &directory, DurablePreflightTestFault fault);

class DurableJournalTestFactory {
public:
    static DurableJournal make(std::unique_ptr<DurableFileAdapter> adapter,
                               JournalLimits limits);
};

DurableJournal
make_durable_journal_for_test(std::unique_ptr<DurableFileAdapter> adapter,
                              JournalLimits limits);
#endif

} // namespace lemon::residency::detail
