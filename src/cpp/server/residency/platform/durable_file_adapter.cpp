#include "platform/durable_file_adapter.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace lemon::residency::detail {

namespace {

DurableFileResult result(DurableFileStatus status) {
    return {status, {}};
}

DurableFileResult combine_transaction_results(
    const DurableFileResult &operation_result,
    const DurableFileResult &flush_result,
    const DurableFileResult &close_result) {
    if (operation_result.effect_may_have_occurred()) {
        return result(DurableFileStatus::EffectMayHaveOccurred);
    }
    if (!operation_result.succeeded()) {
        return operation_result;
    }
    if (flush_result.succeeded() && close_result.succeeded()) {
        return result(DurableFileStatus::Succeeded);
    }
    return result(DurableFileStatus::EffectMayHaveOccurred);
}

DurableFileResult combine_read_results(const DurableFileResult &read_result,
                                       const DurableFileResult &close_result,
                                       bool observed_bytes) {
    if (read_result.effect_may_have_occurred() ||
        close_result.effect_may_have_occurred() ||
        (observed_bytes && !close_result.succeeded())) {
        return result(DurableFileStatus::EffectMayHaveOccurred);
    }
    if (!read_result.succeeded()) {
        return read_result;
    }
    return close_result;
}

} // namespace

bool DurableFileResult::succeeded() const noexcept {
    return status == DurableFileStatus::Succeeded;
}

bool DurableFileResult::effect_may_have_occurred() const noexcept {
    return status == DurableFileStatus::EffectMayHaveOccurred;
}

DurableFileResult retry_interrupted(DurableInterruptibleCall &call) {
    while (true) {
        auto call_result = call.attempt();
        if (call_result.status != DurableFileStatus::Interrupted) {
            return call_result;
        }
    }
}

DurableFileResult write_all(DurableFileChannel &channel,
                            std::string_view bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto write_result = channel.write_some(bytes.substr(written));
        if (write_result.result.status == DurableFileStatus::Interrupted) {
            continue;
        }
        if (!write_result.result.succeeded()) {
            if (written != 0 &&
                !write_result.result.effect_may_have_occurred()) {
                return result(DurableFileStatus::EffectMayHaveOccurred);
            }
            return write_result.result;
        }
        const auto remaining = bytes.size() - written;
        if (write_result.bytes_written == 0 ||
            write_result.bytes_written > remaining) {
            return result(written == 0 ? DurableFileStatus::FailedBeforeEffect
                                       : DurableFileStatus::EffectMayHaveOccurred);
        }
        written += write_result.bytes_written;
    }
    return result(DurableFileStatus::Succeeded);
}

DurableFileResult flush_retrying_interrupts(DurableFileChannel &channel) {
    while (true) {
        auto flush_result = channel.flush();
        if (flush_result.status != DurableFileStatus::Interrupted) {
            return flush_result;
        }
    }
}

DurableFileResult truncate_retrying_interrupts(DurableFileChannel &channel,
                                               std::size_t bytes) {
    while (true) {
        auto truncate_result = channel.truncate(bytes);
        if (truncate_result.status != DurableFileStatus::Interrupted) {
            return truncate_result;
        }
    }
}

DurableFileResult close_once(DurableFileChannel &channel) {
    return channel.close();
}

DurableFileResult close_read_once(DurableReadChannel &channel) {
    return channel.close();
}

DurableFileResult write_flush_close(DurableFileChannel &channel,
                                    std::string_view bytes) {
    const auto write_result = write_all(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    if (write_result.effect_may_have_occurred()) {
        return result(DurableFileStatus::EffectMayHaveOccurred);
    }
    return combine_transaction_results(write_result, flush_result, close_result);
}

DurableFileResult truncate_flush_close(DurableFileChannel &channel,
                                       std::size_t bytes) {
    const auto truncate_result = truncate_retrying_interrupts(channel, bytes);
    const auto flush_result = flush_retrying_interrupts(channel);
    const auto close_result = close_once(channel);
    if (truncate_result.effect_may_have_occurred()) {
        return result(DurableFileStatus::EffectMayHaveOccurred);
    }
    return combine_transaction_results(truncate_result, flush_result,
                                       close_result);
}

DurableReadResult read_bounded_close(DurableReadChannel &channel,
                                     std::size_t max_bytes) {
    std::string retained;
    bool truncated = false;
    auto read_result = result(DurableFileStatus::Succeeded);
    while (true) {
        const auto requested = std::min(
            (max_bytes == std::numeric_limits<std::size_t>::max()
                 ? max_bytes
                 : max_bytes + 1) -
                retained.size(),
            durable_read_chunk_bytes);
        const auto chunk_result = channel.read_some(requested);
        if (chunk_result.result.status == DurableFileStatus::Interrupted) {
            continue;
        }
        if (chunk_result.bytes.size() > requested ||
            (chunk_result.result.succeeded() && chunk_result.bytes.empty() &&
             !chunk_result.end_of_file)) {
            read_result = result(retained.empty()
                                     ? DurableFileStatus::FailedBeforeEffect
                                     : DurableFileStatus::EffectMayHaveOccurred);
            break;
        }
        if (!chunk_result.bytes.empty()) {
            retained.append(chunk_result.bytes);
        }
        if (!chunk_result.result.succeeded()) {
            read_result = chunk_result.result;
            if (!retained.empty() &&
                !read_result.effect_may_have_occurred()) {
                read_result = result(DurableFileStatus::EffectMayHaveOccurred);
            }
            break;
        }
        if (retained.size() > max_bytes) {
            retained.resize(max_bytes);
            truncated = true;
            break;
        }
        if (chunk_result.end_of_file) {
            break;
        }
    }
    const auto close_result = close_read_once(channel);
    return {combine_read_results(read_result, close_result, !retained.empty()),
            std::move(retained), truncated};
}

std::optional<std::string>
durable_immutable_object_filename(std::string_view sha256) {
    if (sha256.size() != 64 ||
        !std::all_of(sha256.begin(), sha256.end(), [](char value) {
            return (value >= '0' && value <= '9') ||
                   (value >= 'a' && value <= 'f');
        })) {
        return std::nullopt;
    }
    std::string filename = "object-sha256-";
    filename.append(sha256);
    filename.append(".json");
    return filename;
}

std::optional<std::string>
durable_immutable_object_stage_filename(std::string_view sha256) {
    const auto object_name = durable_immutable_object_filename(sha256);
    if (!object_name.has_value()) {
        return std::nullopt;
    }
    return "." + *object_name + ".stage";
}

bool durable_fixed_namespace_name_is_valid(
    std::string_view name) noexcept {
    if (name.empty() || name.size() > 63 || name.front() == '-' ||
        name.back() == '-') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char value) {
        return (value >= 'a' && value <= 'z') ||
               (value >= '0' && value <= '9') || value == '-';
    });
}

} // namespace lemon::residency::detail
