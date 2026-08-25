#include <lemon/utils/process_manager.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;
using lemon::utils::PreparedProcessContainment;
using lemon::utils::ProcessContainmentIdentity;
using lemon::utils::ProcessContainmentRequest;
using lemon::utils::ProcessManager;

std::string unique_nonce() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16)
           << static_cast<std::uint64_t>(ticks) << std::setw(16)
           << static_cast<std::uint64_t>(::getpid());
    const std::string seed = stream.str();
    return seed + seed;
}

int fail(const std::string &message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

lemon::utils::ProcessContainmentOperationControl operation_control() {
    return {std::chrono::steady_clock::now() + 5s, {}};
}

std::optional<std::filesystem::path> find_prepared_leaf(
    const std::filesystem::path &root,
    const ProcessContainmentIdentity &identity) {
    std::error_code error;
    std::optional<std::filesystem::path> match;
    for (std::filesystem::directory_iterator entry(root, error), end;
         !error && entry != end; entry.increment(error)) {
        struct stat status {};
        if (::lstat(entry->path().c_str(), &status) != 0 ||
            !S_ISDIR(status.st_mode) ||
            static_cast<std::uint64_t>(status.st_dev) != identity.device ||
            static_cast<std::uint64_t>(status.st_ino) != identity.inode) {
            continue;
        }
        if (match) {
            return std::nullopt;
        }
        match = entry->path();
    }
    return error ? std::nullopt : match;
}

} // namespace

int main() {
    const char *root_value =
        std::getenv("LEMONADE_PROCESS_CONTAINMENT_TEST_ROOT");
    if (root_value == nullptr || *root_value == '\0') {
        std::cout << "SKIP: LEMONADE_PROCESS_CONTAINMENT_TEST_ROOT is unset\n";
        return 77;
    }

    const std::filesystem::path root(root_value);
    const std::string nonce = unique_nonce();
    auto prepared_result = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root, "integration/direct-child", nonce},
        operation_control());
    if (!prepared_result.succeeded()) {
        return fail("cannot prepare delegated containment: " +
                    prepared_result.diagnostic);
    }

    PreparedProcessContainment &containment = *prepared_result.containment;
    auto cleanup = [&] {
        if (!containment.active()) {
            return;
        }
        (void)ProcessManager::kill_process_containment(containment, 3s);
        (void)ProcessManager::release_process_containment(
            containment, operation_control());
    };

    const auto initial = ProcessManager::snapshot_process_containment(
        containment, operation_control());
    if (!initial.succeeded() || !initial.snapshot->members.empty()) {
        cleanup();
        return fail("prepared containment was not initially empty");
    }
    const auto leaf = find_prepared_leaf(root, initial.snapshot->identity);
    if (!leaf) {
        cleanup();
        return fail("prepared containment leaf identity is not visible");
    }
    std::ifstream maximum_depth(*leaf / "cgroup.max.depth");
    std::string maximum_depth_value;
    maximum_depth >> maximum_depth_value;
    maximum_depth.close();
    if (maximum_depth_value != "0") {
        cleanup();
        return fail("prepared containment did not bind a zero maximum depth");
    }

    std::error_code descendant_error;
    const std::filesystem::path descendant =
        *leaf / "unexpected-descendant";
    const bool created_descendant =
        std::filesystem::create_directory(descendant, descendant_error);
    if (created_descendant) {
        (void)std::filesystem::remove(descendant, descendant_error);
        cleanup();
        return fail("prepared containment did not enforce a flat process scope");
    }
    if (descendant_error !=
        std::errc::resource_unavailable_try_again) {
        cleanup();
        return fail("flat process scope failed with an unexpected error: " +
                    descendant_error.message());
    }

    auto started = ProcessManager::start_process_contained(
        containment, "/bin/sleep", {"60"},
        {std::chrono::steady_clock::now() + 5s, {}});
    if (!started.succeeded()) {
        cleanup();
        return fail("contained child did not start: " + started.diagnostic);
    }

    auto populated = ProcessManager::snapshot_process_containment(
        containment, operation_control());
    if (!populated.succeeded() || populated.snapshot->members.size() != 1U ||
        populated.snapshot->members.front() != *started.direct_child_identity) {
        cleanup();
        (void)ProcessManager::reap_process(*started.process);
        return fail("snapshot did not bind the exact direct child");
    }

    const auto killed =
        ProcessManager::kill_process_containment(containment, 3s);
    if (!killed.succeeded()) {
        cleanup();
        (void)ProcessManager::reap_process(*started.process);
        return fail("contained tree did not terminate: " + killed.diagnostic);
    }
    (void)ProcessManager::reap_process(*started.process);

    const auto empty = ProcessManager::snapshot_process_containment(
        containment, operation_control());
    if (!empty.succeeded() || !empty.snapshot->members.empty()) {
        cleanup();
        return fail("terminated containment did not become observably empty");
    }

    const auto released =
        ProcessManager::release_process_containment(containment,
                                                    operation_control());
    if (!released.succeeded() || containment.active() ||
        std::filesystem::exists(*leaf)) {
        cleanup();
        return fail("empty containment did not release cleanly");
    }

    std::cout << "Linux process containment integration test passed\n";
    return 0;
}
