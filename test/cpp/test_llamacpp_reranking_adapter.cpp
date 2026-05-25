// Standalone test for Lemonade's llama.cpp reranking adapter.
// Compile with:
//   g++ -std=c++17 -I src/cpp/include \
//       test/cpp/test_llamacpp_reranking_adapter.cpp \
//       src/cpp/server/backends/llamacpp_reranking_adapter.cpp \
//       src/cpp/server/recipe_options.cpp \
//       -o /tmp/test_llamacpp_reranking_adapter

#include "lemon/backends/llamacpp_reranking_adapter.h"
#include "lemon/system_info.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace lemon {

SystemInfo::SupportedBackendsResult SystemInfo::get_supported_backends(const std::string&) {
    return {};
}

} // namespace lemon

using lemon::RecipeOptions;
using lemon::json;
using lemon::ModelType;
using lemon::backends::build_zeroentropy_logit_score_completion_request;
using lemon::backends::format_zeroentropy_logit_score_prompt;
using lemon::backends::rerank_with_zeroentropy_logit_score_adapter;
using lemon::backends::should_launch_native_llamacpp_reranking;

int main() {
    int failures = 0;
    auto expect_eq = [&failures](const std::string& actual,
                                 const std::string& expected,
                                 const char* name) {
        const bool ok = actual == expected;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) {
            std::printf("  got:\n%s\n want:\n%s\n", actual.c_str(), expected.c_str());
            ++failures;
        }
    };
    auto expect_true = [&failures](bool condition, const char* name) {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    auto expect_close = [&failures](double actual, double expected, const char* name) {
        const bool ok = std::fabs(actual - expected) < 1e-9;
        std::printf("[%s] %s  (got=%.12f, want=%.12f)\n",
                    ok ? "PASS" : "FAIL", name, actual, expected);
        if (!ok) ++failures;
    };

    expect_eq(
        format_zeroentropy_logit_score_prompt("capital of France", "Paris is the capital of France."),
        "<|im_start|>system\n"
        "capital of France<|im_end|>\n"
        "<|im_start|>user\n"
        "Paris is the capital of France.<|im_end|>\n"
        "<|im_start|>assistant\n",
        "formats ZeroEntropy reranking prompt");

    {
        RecipeOptions options("llamacpp", {
            {"llamacpp_reranking_adapter", "zeroentropy-logit-score"},
            {"llamacpp_reranking_true_token_id", 9454},
            {"llamacpp_reranking_logit_scale", 5.0}
        });
        json preserved = options.to_json();
        expect_true(preserved.contains("llamacpp_reranking_adapter"),
                    "preserves adapter option for llamacpp recipes");
        expect_true(preserved.contains("llamacpp_reranking_true_token_id"),
                    "preserves selected token id option for llamacpp recipes");
        expect_true(preserved.contains("llamacpp_reranking_logit_scale"),
                    "preserves logit scale option for llamacpp recipes");
        expect_true(!should_launch_native_llamacpp_reranking(ModelType::RERANKING, options),
                    "adapter-backed rerankers do not launch native llama.cpp reranking");
    }

    {
        RecipeOptions options("llamacpp", {});
        expect_true(should_launch_native_llamacpp_reranking(ModelType::RERANKING, options),
                    "plain llama.cpp rerankers still launch native reranking");
    }

    {
        json completion_request = build_zeroentropy_logit_score_completion_request(
            "capital of France", "Paris is the capital of France.", 9454);
        expect_true(completion_request["prompt"].is_string(), "completion request has prompt");
        expect_true(completion_request["n_predict"] == 1, "completion request predicts one token");
        expect_true(completion_request["temperature"] == 0, "completion request is deterministic");
        expect_true(completion_request["token_logits"] == json::array({9454}),
                    "completion request asks for selected token logit");
    }

    {
        json request = {
            {"model", "zerank"},
            {"query", "capital of France"},
            {"documents", json::array({
                "Berlin is the capital of Germany.",
                "Paris is the capital of France.",
                "A baguette is bread."
            })}
        };
        std::vector<double> logits = {2.0, 17.0, -1.0};
        size_t calls = 0;
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, 9454, 5.0, [&logits, &calls](const json&) {
                if (calls >= logits.size()) {
                    return json{{"error", {{"message", "unexpected completion call count"}}}};
                }
                return json{{"token_logits", json::array({
                    {{"id", 9454}, {"token", "Yes"}, {"logit", logits[calls++]}}
                })}};
            });
        expect_true(!response.contains("error"), "mocked adapter reranking succeeds");
        const bool has_valid_results =
            response.contains("results") &&
            response["results"].is_array() &&
            response["results"].size() == 3;
        expect_true(has_valid_results, "returns one result per document");
        if (has_valid_results) {
            expect_true(response["results"][0]["index"] == 1,
                        "sorts Paris sanity document first by raw logit");
            expect_true(response["results"][1]["index"] == 0,
                        "sorts second document by raw logit");
            expect_true(response["results"][2]["index"] == 2,
                        "sorts last document by raw logit");
            expect_close(response["results"][0]["relevance_score"].get<double>(),
                         1.0 / (1.0 + std::exp(-(17.0 / 5.0))),
                         "normalizes public score with sigmoid(logit / scale)");
        }
    }

    {
        json request = {
            {"query", "capital of France"},
            {"documents", json::array({"Paris is the capital of France."})}
        };
        bool completion_called = false;
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, -1, 5.0, [&completion_called](const json&) {
                completion_called = true;
                return json::object();
            });
        expect_true(response.contains("error"), "negative selected token id returns error");
        expect_true(!completion_called, "negative selected token id does not call backend");
    }

    {
        json request = {
            {"query", "capital of France"},
            {"documents", json::array({"Paris is the capital of France."})}
        };
        bool completion_called = false;
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, 9454, 0.0, [&completion_called](const json&) {
                completion_called = true;
                return json::object();
            });
        expect_true(response.contains("error"), "nonpositive logit scale returns error");
        expect_true(!completion_called, "nonpositive logit scale does not call backend");
    }

    {
        json request = {
            {"query", "capital of France"},
            {"documents", json::array({"Paris is the capital of France."})}
        };
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, 9454, 5.0, [](const json&) {
                return json{{"token_logits", json::array({{{"id", 1}, {"logit", 0.0}}})}};
            });
        expect_true(response.contains("error"), "missing selected token returns error");
    }

    {
        json request = {
            {"query", "capital of France"},
            {"documents", json::array({"Paris is the capital of France."})}
        };
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, 9454, 5.0, [](const json&) {
                return json{{"token_logits", "not an array"}};
            });
        expect_true(response.contains("error"), "malformed token_logits returns error");
    }

    {
        json request = {
            {"query", "capital of France"},
            {"documents", json::array({"Paris is the capital of France."})}
        };
        json response = rerank_with_zeroentropy_logit_score_adapter(
            request, 9454, 5.0, [](const json&) {
                return json{{"error", {{"message", "backend failed"}}}};
            });
        expect_true(response.contains("error"), "backend error returns Lemonade error");
    }

    return failures == 0 ? 0 : 1;
}
