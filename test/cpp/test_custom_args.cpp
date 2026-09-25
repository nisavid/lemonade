// Standalone test for lemon::utils custom arg parsing helpers.
// Build with: cmake --build --preset default --target test_custom_args
// Run with: ctest --test-dir build -R '^CustomArgsTest$' --output-on-failure

#include "lemon/utils/custom_args.h"
#include "lemon/utils/recipe_arg_resolver.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using lemon::utils::CustomArgsRequestState;
using lemon::utils::build_custom_args_map;
using lemon::utils::custom_args_has_flag;
using lemon::utils::map_to_args_string;
using lemon::utils::merge_args_maps;
using lemon::utils::parse_custom_args;
using lemon::utils::resolve_scoped_custom_args;

using ArgMap = lemon::utils::CustomArgsMap;

static std::string dump(const ArgMap& m) {
    std::string s = "{";
    for (const auto& [flag, occurrences] : m) {
        s += " " + flag + ":";
        for (const auto& vals : occurrences) {
            s += " [";
            for (size_t i = 0; i < vals.size(); ++i) {
                if (i) s += ", ";
                s += vals[i];
            }
            s += "]";
        }
    }
    return s + " }";
}

static bool expect_map(const char* name, const std::string& input,
                       const ArgMap& expected) {
    ArgMap actual = build_custom_args_map(parse_custom_args(input));
    bool ok = (actual == expected);
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got:  %s\n  want: %s\n",
                    dump(actual).c_str(), dump(expected).c_str());
    }
    return ok;
}

static bool expect_merge(const char* name, const std::string& target,
                         const std::string& incoming,
                         const std::string& expected) {
    ArgMap target_map = build_custom_args_map(parse_custom_args(target));
    ArgMap incoming_map = build_custom_args_map(parse_custom_args(incoming));
    std::string actual = map_to_args_string(merge_args_maps(target_map, incoming_map));
    bool ok = (actual == expected);
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got:  %s\n  want: %s\n",
                    actual.c_str(), expected.c_str());
    }
    return ok;
}

using Argv = std::vector<std::string>;

// The production *_args layer merge, high-precedence layer first.
static std::string merge_layers(const std::string& high, const std::string& low) {
    if (high.empty()) return low;
    if (low.empty()) return high;
    return map_to_args_string(merge_args_maps(build_custom_args_map(parse_custom_args(high, true)),
                                              build_custom_args_map(parse_custom_args(low, true))));
}

static std::string dump(const Argv& argv) {
    std::string s = "[";
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) s += ", ";
        s += "<" + argv[i] + ">";
    }
    return s + "]";
}

static bool expect_merged_argv(const char* name, const std::string& merged,
                               const Argv& expected) {
    Argv actual = parse_custom_args(merged);
    bool ok = (actual == expected);
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  args: %s\n  got:  %s\n  want: %s\n", merged.c_str(),
                    dump(actual).c_str(), dump(expected).c_str());
    }
    return ok;
}

static bool expect_merged_argv_one_of(const char* name, const std::string& merged,
                                      const std::vector<Argv>& accepted) {
    Argv actual = parse_custom_args(merged);
    bool ok = false;
    for (const auto& expected : accepted) {
        ok = ok || (actual == expected);
    }
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  args: %s\n  got:  %s\n", merged.c_str(), dump(actual).c_str());
        for (const auto& expected : accepted) {
            std::printf("  want: %s\n", dump(expected).c_str());
        }
    }
    return ok;
}

static bool expect_same_args(const char* name, const std::string& actual,
                             const std::string& expected) {
    bool ok = (actual == expected);
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got:  %s\n  want: %s\n", actual.c_str(), expected.c_str());
    }
    return ok;
}

static bool expect_has_flag(const char* name, const std::string& input,
                            const std::string& flag, bool expected) {
    bool actual = custom_args_has_flag(parse_custom_args(input), flag);
    bool ok = (actual == expected);
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got:  %d\n  want: %d\n", actual, expected);
    }
    return ok;
}

int main() {
    int failures = 0;

    // Negative numbers attach to their flag and never become phantom keys.
    failures += !expect_map(
        "negative integer value", "--cache-ram -1", {{"--cache-ram", {{"-1"}}}});
    failures += !expect_map(
        "negative float value", "--temp -0.5", {{"--temp", {{"-0.5"}}}});
    failures += !expect_map(
        "negative leading-dot float", "--temp -.5", {{"--temp", {{"-.5"}}}});

    // Scientific notation.
    failures += !expect_map(
        "scientific notation", "--threshold -1e5", {{"--threshold", {{"-1e5"}}}});
    failures += !expect_map(
        "scientific notation with negative exponent",
        "--threshold -1.5e-3", {{"--threshold", {{"-1.5e-3"}}}});
    failures += !expect_map(
        "scientific notation capital E",
        "--val -1E10", {{"--val", {{"-1E10"}}}});

    // Identical negatives under two flags must both survive (std::map keys
    // are unique; treating -1 as a flag would dedupe it, dropping values).
    failures += !expect_map(
        "duplicate negative under two flags (no phantom key)",
        "--cache-ram -1 --reasoning-budget -1",
        {{"--cache-ram", {{"-1"}}}, {"--reasoning-budget", {{"-1"}}}});

    // Sanity: short and long flags still parse as flags with their values.
    failures += !expect_map(
        "short flag with value", "-ngl 99", {{"-ngl", {{"99"}}}});
    failures += !expect_map(
        "long flag with value", "--threads 8", {{"--threads", {{"8"}}}});

    // Counterexamples: incomplete numerics are flags, not values.
    failures += !expect_map(
        "-1foo is a flag, not a value",
        "--foo -1foo", {{"--foo", {{}}}, {"-1foo", {{}}}});
    failures += !expect_map(
        "-.future is a flag, not a value",
        "--foo -.future", {{"--foo", {{}}}, {"-.future", {{}}}});

    failures += !expect_map(
        "repeatable flag occurrences",
        "--override-kv a=bool:false --override-kv b=bool:false",
        {{"--override-kv", {{"a=bool:false"}, {"b=bool:false"}}}});

    failures += !expect_merge(
        "target repeatable flag takes precedence",
        "--override-kv a=bool:false --override-kv b=bool:false --threads 8",
        "--override-kv default=bool:true --threads 4 --cache-type-k q8_0",
        "--cache-type-k q8_0 --override-kv a=bool:false --override-kv b=bool:false --threads 8");
    failures += !expect_merge(
        "incoming repeatable flag is preserved",
        "--threads 8",
        "--override-kv a=bool:false --override-kv b=bool:false",
        "--override-kv a=bool:false --override-kv b=bool:false --threads 8");
    failures += !expect_merge(
        "binary negation precedence is preserved",
        "--no-mmap",
        "--mmap --override-kv a=bool:false --override-kv b=bool:false",
        "--no-mmap --override-kv a=bool:false --override-kv b=bool:false");

    // Overridable-arg detection must compare complete flag tokens, not
    // substrings, so a flag name appearing only inside a value or file path
    // does not suppress a Lemonade default (regression for llama.cpp arg
    // handling, e.g. "--load-mode none" and the -lm / --mmap aliases).
    failures += !expect_has_flag(
        "real long alias token matches",
        "--no-mmap", "--no-mmap", true);
    failures += !expect_has_flag(
        "real short alias token matches",
        "-lm", "-lm", true);
    failures += !expect_has_flag(
        "key with equals-value form matches",
        "--load-mode=auto", "--load-mode", true);
    failures += !expect_has_flag(
        "equals-value alias matches",
        "--mmap=auto", "--mmap", true);
    failures += !expect_has_flag(
        "alias inside path does not match",
        "--lora /models/alma-lm-adapter.gguf", "-lm", false);
    failures += !expect_has_flag(
        "long alias inside value does not match",
        "--override-kv tokenizer.mmap=auto", "--mmap", false);
    failures += !expect_has_flag(
        "missing alias does not match",
        "--threads 8", "--mmap", false);

    // Merged layers must reach the backend argv exactly as one unmerged layer
    // would: no literal quote characters, however many layers were merged.
    const std::string global = "--threads 8";
    const std::string arch = "--chat-template-kwargs '{\"preserve_thinking\":true}'";
    const std::string arch_scope = merge_layers(arch, global);
    failures += !expect_merged_argv(
        "architecture JSON value merged with global args has no literal quotes", arch_scope,
        {"--chat-template-kwargs", "{\"preserve_thinking\":true}", "--threads", "8"});

    const std::string model =
        "--alias \"My Model\" --chat-template-kwargs '{\"enable_thinking\":false}'";
    const std::string model_scope = merge_layers(model, arch_scope);
    failures += !expect_merged_argv(
        "quoted user-model values merged over architecture and global args", model_scope,
        {"--alias", "My Model", "--chat-template-kwargs", "{\"enable_thinking\":false}",
         "--threads", "8"});
    failures += !expect_merged_argv(
        "request, model, architecture and global layers nest without extra quoting",
        merge_layers("--temp 0.5", model_scope),
        {"--alias", "My Model", "--chat-template-kwargs", "{\"enable_thinking\":false}",
         "--temp", "0.5", "--threads", "8"});

    failures += !expect_same_args("re-merging a merged layer is idempotent",
                                  merge_layers(arch_scope, global), arch_scope);
    failures += !expect_same_args("merging a merged layer into itself is idempotent",
                                  merge_layers(model_scope, model_scope), model_scope);

    failures += !expect_merged_argv(
        "values with spaces and escaped quotes round-trip through a merge",
        merge_layers("--system-prompt 'say \\'hi\\' now' --alias \"a \\\"b\\\" c\"", global),
        {"--alias", "a \"b\" c", "--system-prompt", "say 'hi' now", "--threads", "8"});
    failures += !expect_merged_argv(
        "single-quoted JSON with backslash-escaped inner quotes round-trips through a merge",
        merge_layers("--chat-template-kwargs '{\"greeting\":\"say \\\"hi\\\"\"}'", global),
        {"--chat-template-kwargs", "{\"greeting\":\"say \\\"hi\\\"\"}", "--threads", "8"});
    failures += !expect_merged_argv(
        "backslash-escaped and bare backslash values round-trip through a merge",
        merge_layers("--lora \"C:\\\\Models\\\\my adapter.gguf\" --log-file C:\\logs\\run.log",
                     global),
        {"--log-file", "C:\\logs\\run.log", "--lora", "C:\\Models\\my adapter.gguf",
         "--threads", "8"});
    failures += !expect_merged_argv(
        "backslash values stay stable across repeated merges",
        merge_layers(merge_layers(merge_layers("--lora \"C:\\\\Models\\\\my adapter.gguf\"",
                                               global),
                                  global),
                     arch),
        {"--chat-template-kwargs", "{\"preserve_thinking\":true}", "--lora",
         "C:\\Models\\my adapter.gguf", "--threads", "8"});

    failures += !expect_merged_argv(
        "quoted value that starts with a dash stays attached to its flag",
        merge_layers("--reverse-prompt '-User:' --threads 4", "--ctx-size 4096 --n-gpu-layers 99"),
        {"--ctx-size", "4096", "--n-gpu-layers", "99", "--reverse-prompt", "-User:",
         "--threads", "4"});
    const std::string end_prompt = merge_layers("--reverse-prompt '--END'", "--ctx-size 4096");
    failures += !expect_merged_argv(
        "quoted value that looks like a long flag stays attached to its flag", end_prompt,
        {"--ctx-size", "4096", "--reverse-prompt", "--END"});
    failures += !expect_same_args("quoted long-flag-like value is idempotent across merges",
                                  merge_layers(end_prompt, "--ctx-size 4096"), end_prompt);
    failures += !expect_merged_argv_one_of(
        "equals-form flag keeps its quoted spaced value intact",
        merge_layers("--chat-template-kwargs='{\"enable_thinking\": false}'", global),
        {{"--chat-template-kwargs={\"enable_thinking\": false}", "--threads", "8"},
         {"--chat-template-kwargs", "{\"enable_thinking\": false}", "--threads", "8"}});
    failures += !expect_merged_argv(
        "equals inside a quoted segment of a flag token does not split the flag",
        merge_layers("--prompt-prefix\"a=b c\"", global),
        {"--prompt-prefixa=b c", "--threads", "8"});
    failures += !expect_merged_argv(
        "unterminated quote keeps the rest of its layer as one value",
        merge_layers("--system-prompt \"be brief --temp 0.1", global),
        {"--system-prompt", "be brief --temp 0.1", "--threads", "8"});

    const std::string empty_value = "--reasoning-format ''";
    failures += !expect_merged_argv("empty quoted value survives a merge as it would unmerged",
                                    merge_layers(empty_value, global),
                                    {"--reasoning-format", "--threads", "8"});
    failures += !expect_same_args("empty quoted value is idempotent across merges",
                                  merge_layers(merge_layers(empty_value, global), global),
                                  merge_layers(empty_value, global));

    const std::string qwen35_arch =
        "--temp 1.0 --top-p 0.95 --top-k 20 --min-p 0.00 --repeat-penalty 1.0 "
        "--chat-template-kwargs '{\"preserve_thinking\":true}'";
    failures += !expect_merged_argv(
        "qwen35 architecture JSON reaches argv as one unquoted argument over global args",
        resolve_scoped_custom_args({global, qwen35_arch, "", "",
                                    CustomArgsRequestState::Omitted, "", true}),
        {"--chat-template-kwargs", "{\"preserve_thinking\":true}", "--min-p", "0.00",
         "--repeat-penalty", "1.0", "--temp", "1.0", "--threads", "8", "--top-k", "20",
         "--top-p", "0.95"});
    failures += !expect_merged_argv(
        "qwen35 architecture JSON stays one unquoted argument under model and global args",
        resolve_scoped_custom_args({global, qwen35_arch, "", "--ctx-size 8192",
                                    CustomArgsRequestState::Omitted, "", true}),
        {"--chat-template-kwargs", "{\"preserve_thinking\":true}", "--ctx-size", "8192",
         "--min-p", "0.00", "--repeat-penalty", "1.0", "--temp", "1.0", "--threads", "8",
         "--top-k", "20", "--top-p", "0.95"});

    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
