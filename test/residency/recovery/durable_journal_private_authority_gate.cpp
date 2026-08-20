#include "lemon/residency/durable_journal.h"
#include "platform/durable_file_adapter.h"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using lemon::residency::AuthorityRootCandidate;
using lemon::residency::DurableJournal;
using lemon::residency::ExactSchemaExportCandidate;
using lemon::residency::JournalHistory;
using lemon::residency::ParsedJournalRecord;
using lemon::residency::PublishedJournal;

struct ForgedAdapter {};

static_assert(std::is_move_constructible_v<PublishedJournal>);
static_assert(!std::is_copy_constructible_v<PublishedJournal>);
static_assert(!std::is_default_constructible_v<PublishedJournal>);
static_assert(!std::is_constructible_v<PublishedJournal, std::string>);
static_assert(!std::is_constructible_v<PublishedJournal,
                                       ExactSchemaExportCandidate>);
static_assert(!std::is_constructible_v<PublishedJournal,
                                       const ParsedJournalRecord &>);
static_assert(!std::is_constructible_v<PublishedJournal, JournalHistory &&>);
static_assert(!std::is_constructible_v<PublishedJournal,
                                       const AuthorityRootCandidate &>);
static_assert(!std::is_constructible_v<PublishedJournal,
                                       std::shared_ptr<ForgedAdapter>>);
static_assert(!std::is_default_constructible_v<DurableJournal>);
static_assert(!std::is_copy_constructible_v<DurableJournal>);

} // namespace

namespace lemon::residency {

class PublishedJournalAccess {
    template <typename T>
    static auto can_private_construct(int)
        -> decltype(T(std::unique_ptr<typename T::Impl>{}), std::true_type{});
    template <typename>
    static auto can_private_construct(...) -> std::false_type;

public:
    static constexpr bool can_forge =
        decltype(can_private_construct<PublishedJournal>(0))::value;
};

class DurableJournalTestAccess {
    template <typename T>
    static auto can_private_construct(int)
        -> decltype(T(std::declval<std::unique_ptr<::ForgedAdapter>>(),
                      std::declval<JournalLimits>()),
                    std::true_type{});
    template <typename>
    static auto can_private_construct(...) -> std::false_type;

public:
    static constexpr bool can_forge =
        decltype(can_private_construct<DurableJournal>(0))::value;
};

static_assert(!PublishedJournalAccess::can_forge);
static_assert(!DurableJournalTestAccess::can_forge);

namespace detail {

enum class DurablePreflightTestFault { MacroOffNameCollision };
inline constexpr int make_platform_durable_file_adapter_for_test = 0;
inline constexpr int make_durable_journal_for_test = 0;

class DurableJournalAccess {
    template <typename T>
    static auto can_private_construct(int)
        -> decltype(T(std::declval<std::unique_ptr<::ForgedAdapter>>(),
                      std::declval<JournalLimits>()),
                    std::true_type{});
    template <typename>
    static auto can_private_construct(...) -> std::false_type;

public:
    static constexpr bool can_forge =
        decltype(can_private_construct<DurableJournal>(0))::value;
};

class DurableJournalTestAccess {
    template <typename T>
    static auto can_private_construct(int)
        -> decltype(T(std::declval<std::unique_ptr<::ForgedAdapter>>(),
                      std::declval<JournalLimits>()),
                    std::true_type{});
    template <typename>
    static auto can_private_construct(...) -> std::false_type;

public:
    static constexpr bool can_forge =
        decltype(can_private_construct<DurableJournal>(0))::value;
};

class DurableJournalTestFactory {
    template <typename T>
    static auto can_private_construct(int)
        -> decltype(T(std::declval<std::unique_ptr<::ForgedAdapter>>(),
                      std::declval<JournalLimits>()),
                    std::true_type{});
    template <typename>
    static auto can_private_construct(...) -> std::false_type;

public:
    static constexpr bool can_forge =
        decltype(can_private_construct<DurableJournal>(0))::value;
};

static_assert(!DurableJournalAccess::can_forge);
static_assert(!DurableJournalTestAccess::can_forge);
static_assert(!DurableJournalTestFactory::can_forge);

} // namespace detail
} // namespace lemon::residency
