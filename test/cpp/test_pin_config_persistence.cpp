#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lemon/config_file.h>
#include <lemon/runtime_config.h>
#include <lemon/utils/path_utils.h>

namespace fs = std::filesystem;
using json = nlohmann::json;
using lemon::ConfigFile;
using lemon::RuntimeConfig;

static int passed = 0;
static int failures = 0;

static void check(bool cond, const char* desc) {
    if (cond) {
        std::printf("[PASS] %s\n", desc);
        ++passed;
    } else {
        std::printf("[FAIL] %s\n", desc);
        ++failures;
    }
    std::fflush(stdout);
}

class ScopedTempRoot {
public:
    ScopedTempRoot() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("lemon_test_pin_persist_" + std::to_string(unique));
        fs::create_directories(path_);
    }
    ~ScopedTempRoot() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    fs::path cache_dir() const { return path_ / "cache"; }
    fs::path config_dir() const { return path_ / "config"; }

private:
    fs::path path_;
};

static std::string utf8(const fs::path& p) {
    return lemon::utils::path_to_utf8(p);
}

static std::vector<std::string> reload_pins(const fs::path& cache_dir,
                                            const fs::path& config_dir) {
    RuntimeConfig reloaded(ConfigFile::load(utf8(cache_dir), utf8(config_dir)));
    return reloaded.pinned_models();
}

static void test_pin_write_is_sparse_and_survives_reload() {
    std::puts("--- test_pin_write_is_sparse_and_survives_reload ---");
    ScopedTempRoot root;
    fs::create_directories(root.cache_dir());

    ConfigFile::save_overrides(utf8(root.config_dir()),
                               {{"pinned_models", json::array({"Qwen3-0.6B-GGUF"})}});

    check(!fs::exists(root.cache_dir() / "config.json"),
          "pin write does not create config.json in the cache dir");
    const json disk = ConfigFile::load_raw(utf8(root.config_dir()));
    check(disk == json{{"pinned_models", json::array({"Qwen3-0.6B-GGUF"})}},
          "config_dir config.json holds only the pinned_models override");
    check(reload_pins(root.cache_dir(), root.config_dir()) ==
              std::vector<std::string>{"Qwen3-0.6B-GGUF"},
          "pinned_models survives a reload from config_dir");
}

static void test_pin_write_preserves_other_overrides() {
    std::puts("--- test_pin_write_preserves_other_overrides ---");
    ScopedTempRoot root;
    ConfigFile::save(utf8(root.config_dir()), {{"port", 9000}});

    ConfigFile::save_overrides(utf8(root.config_dir()),
                               {{"pinned_models", json::array({"a", "b"})}});

    const json disk = ConfigFile::load_raw(utf8(root.config_dir()));
    check(disk == json{{"port", 9000}, {"pinned_models", json::array({"a", "b"})}},
          "pin write keeps the existing sparse port override");
}

static void test_unpin_all_prunes_the_key() {
    std::puts("--- test_unpin_all_prunes_the_key ---");
    ScopedTempRoot root;
    ConfigFile::save_overrides(utf8(root.config_dir()),
                               {{"pinned_models", json::array({"a"})}});

    ConfigFile::save_overrides(utf8(root.config_dir()),
                               {{"pinned_models", json::array()}});

    const json disk = ConfigFile::load_raw(utf8(root.config_dir()));
    check(!disk.contains("pinned_models"),
          "removing the last pin prunes pinned_models from config_dir");
    check(reload_pins(root.cache_dir(), root.config_dir()).empty(),
          "reload after removing the last pin has no pins");
}

static void test_legacy_cache_snapshot_pins_migrate_to_config_dir() {
    std::puts("--- test_legacy_cache_snapshot_pins_migrate_to_config_dir ---");
    ScopedTempRoot root;
    fs::create_directories(root.cache_dir());
    json legacy_snapshot = ConfigFile::get_defaults();
    legacy_snapshot["pinned_models"] = json::array({"legacy-model"});
    {
        std::ofstream out(root.cache_dir() / "config.json");
        out << legacy_snapshot.dump(2);
    }

    lemon::utils::migrate_legacy_json_files_to_config_dir(utf8(root.cache_dir()),
                                                          utf8(root.config_dir()));

    check(!fs::exists(root.cache_dir() / "config.json"),
          "legacy cache-dir config.json moved out of the cache dir");
    check(reload_pins(root.cache_dir(), root.config_dir()) ==
              std::vector<std::string>{"legacy-model"},
          "pins from the legacy full snapshot load from config_dir");

    ConfigFile::save_overrides(utf8(root.config_dir()),
                               {{"pinned_models", json::array({"legacy-model", "new-model"})}});

    const json disk = ConfigFile::load_raw(utf8(root.config_dir()));
    check(disk.contains("pinned_models") &&
              disk["pinned_models"] == json::array({"legacy-model", "new-model"}),
          "next pin write keeps legacy and new pins");
    check(!disk.contains("host") && !disk.contains("llamacpp"),
          "next pin write prunes default-valued keys from the legacy snapshot");
    check(reload_pins(root.cache_dir(), root.config_dir()) ==
              std::vector<std::string>{"legacy-model", "new-model"},
          "both pins survive a reload");
}

int main() {
    std::puts("=== Pin Config Persistence Tests ===");

    test_pin_write_is_sparse_and_survives_reload();
    test_pin_write_preserves_other_overrides();
    test_unpin_all_prunes_the_key();
    test_legacy_cache_snapshot_pins_migrate_to_config_dir();

    std::printf("================================================\n");
    if (failures > 0) {
        std::printf("Tests finished: %d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("All pin config persistence tests PASSED (%d passed).\n", passed);
    return 0;
}
