#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <lemon/backends/backend_utils.h>
#include <lemon/runtime_config.h>
#include <lemon/utils/json_utils.h>
#include <lemon/utils/path_utils.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

using lemon::backends::BackendUtils;

namespace {

int failures = 0;

unsigned long current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

void check(bool condition, const std::string& message) {
    if (condition) {
        std::cout << "[PASS] " << message << '\n';
        return;
    }
    std::cerr << "[FAIL] " << message << '\n';
    ++failures;
}

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

std::string pinned_therock_version() {
    const auto config = lemon::utils::JsonUtils::load_from_file(
        lemon::utils::get_resource_path("resources/backend_versions.json"));
    return config.at("therock").at("version").get<std::string>();
}

void test_every_supported_architecture_has_an_archive_variant() {
    const auto config = lemon::utils::JsonUtils::load_from_file(
        lemon::utils::get_resource_path("resources/backend_versions.json"));
    for (const auto& arch_value : config.at("therock").at("architectures")) {
        const std::string arch = arch_value.get<std::string>();
        const auto variant = BackendUtils::therock_archive_variant(arch);
        check(variant && !variant->empty(),
              "supported TheRock architecture " + arch + " has an archive variant");
    }
    check(!BackendUtils::therock_archive_variant("gfx9999"),
          "an unlisted TheRock architecture has no archive variant");
}

void test_wheel_liveness_requires_every_recorded_runtime_dir(const fs::path& root) {
    const std::string arch = "gfx1151";
    const std::string version = "7.14.0";
    const fs::path wheel_dir = BackendUtils::get_therock_wheel_dir(arch, version);
    const fs::path core_dir = root / "runtime-core";
    const fs::path libraries_dir = root / "runtime-libraries";
    fs::create_directories(core_dir);
    fs::create_directories(libraries_dir);
    write_text(wheel_dir / "runtime_paths.txt",
               core_dir.string() + "\n\n" + libraries_dir.string() + "\n");

    check(BackendUtils::therock_wheel_runtime_alive(arch, version),
          "wheel runtime is live when every recorded directory exists");

    fs::remove_all(libraries_dir);
    check(!BackendUtils::therock_wheel_runtime_alive(arch, version),
          "wheel runtime is not live when one recorded directory is missing");

    write_text(wheel_dir / "runtime_paths.txt", "\n\r\n");
    check(!BackendUtils::therock_wheel_runtime_alive(arch, version),
          "wheel runtime is not live without a nonblank directory");
}

void test_cleanup_preserves_every_architecture_at_the_pinned_version() {
    const std::string version = pinned_therock_version();
    const std::string stale_version = "0.0.0";

    const fs::path current_first = BackendUtils::get_therock_wheel_dir("gfx1151", version);
    const fs::path current_second = BackendUtils::get_therock_wheel_dir("gfx1152", version);
    const fs::path stale = BackendUtils::get_therock_wheel_dir("gfx1151", stale_version);
    fs::create_directories(current_first);
    fs::create_directories(current_second);
    fs::create_directories(stale);

    BackendUtils::cleanup_old_therock_versions();

    check(fs::is_directory(current_first),
          "cleanup preserves the first architecture at the pinned version");
    check(fs::is_directory(current_second),
          "cleanup preserves another architecture at the pinned version");
    check(!fs::exists(stale), "cleanup removes a stale wheel runtime version");
}

void test_explicit_wheel_selection_does_not_use_a_tarball_runtime() {
    const std::string arch = "gfx1150";
    const std::string version = pinned_therock_version();
    fs::create_directories(fs::path(BackendUtils::get_therock_install_dir(arch, version)) / "lib");

    lemon::RuntimeConfig config(lemon::json{{"rocm_install_method", "wheel"}});
    lemon::RuntimeConfig::set_global(&config);
    const auto paths = BackendUtils::get_therock_lib_paths(arch);
    lemon::RuntimeConfig::set_global(nullptr);

    check(paths.empty(), "explicit wheel selection does not fall back to a tarball runtime");
}

void test_runtime_installation_check_honors_the_requested_method(const fs::path& root) {
    const std::string arch = "gfx1150";
    const std::string version = pinned_therock_version();
    const fs::path tarball_dir = BackendUtils::get_therock_install_dir(arch, version);
    write_text(tarball_dir / "version.txt", version);

    check(!BackendUtils::therock_runtime_installed(arch, version, "wheel"),
          "a tarball without method metadata does not satisfy a wheel request");
    check(BackendUtils::therock_runtime_installed(arch, version, "tarball"),
          "a matching tarball satisfies a tarball request");
    check(BackendUtils::therock_runtime_installed(arch, version, "auto"),
          "a matching tarball satisfies automatic selection");

    const fs::path wheel_dir = BackendUtils::get_therock_wheel_dir(arch, version);
    const fs::path core_dir = root / "installed-wheel-core";
    const fs::path libraries_dir = root / "installed-wheel-libraries";
    fs::create_directories(core_dir);
    fs::create_directories(libraries_dir);
    write_text(wheel_dir / "runtime_paths.txt",
               core_dir.string() + "\n" + libraries_dir.string() + "\n");
    write_text(wheel_dir / "version.txt", version);

    check(BackendUtils::therock_runtime_installed(arch, version, "wheel"),
          "a complete matching wheel runtime satisfies a wheel request");
}

void test_runtime_repair_removes_both_installation_trees() {
    const std::string arch = "gfx1153";
    const std::string version = pinned_therock_version();
    const fs::path tarball_dir = BackendUtils::get_therock_install_dir(arch, version);
    const fs::path wheel_dir = BackendUtils::get_therock_wheel_dir(arch, version);
    fs::create_directories(tarball_dir);
    fs::create_directories(wheel_dir);

    BackendUtils::remove_therock_runtime(arch, version);

    check(!fs::exists(tarball_dir), "runtime repair removes the tarball tree");
    check(!fs::exists(wheel_dir), "runtime repair removes the wheel tree");
}

void test_runtime_file_search_checks_every_directory(const fs::path& root) {
    const fs::path first = root / "runtime-search-first";
    const fs::path second = root / "runtime-search-second";
    fs::create_directories(first);
    write_text(second / "amdhip64_7.dll", "runtime");

    const auto found = BackendUtils::find_runtime_file(
        {first.string(), second.string()}, "amdhip64_7.dll");
    check(found && *found == second / "amdhip64_7.dll",
          "runtime file search finds a DLL outside the first directory");
}

}  // namespace

int main() {
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() /
        ("lemonade-therock-runtime-test-" + std::to_string(current_process_id()) + "-" +
         std::to_string(tick));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    lemon::utils::set_cache_dir(root.string());

    test_wheel_liveness_requires_every_recorded_runtime_dir(root);
    test_cleanup_preserves_every_architecture_at_the_pinned_version();
    test_explicit_wheel_selection_does_not_use_a_tarball_runtime();
    test_runtime_installation_check_honors_the_requested_method(root);
    test_runtime_repair_removes_both_installation_trees();
    test_runtime_file_search_checks_every_directory(root);
    test_every_supported_architecture_has_an_archive_variant();

    fs::remove_all(root, ec);
    return failures == 0 ? 0 : 1;
}
