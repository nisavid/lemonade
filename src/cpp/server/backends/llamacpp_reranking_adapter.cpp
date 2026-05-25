#include "lemon/backends/llamacpp_reranking_adapter.h"

#include "lemon/error_types.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace lemon {
namespace backends {

namespace {

struct ScoredDocument {
    int index;
    double raw_logit;
    double relevance_score;
};

double sigmoid(double value) {
    return 1.0 / (1.0 + std::exp(-value));
}

json invalid_request(const std::string& message) {
    return ErrorResponse::create(message, ErrorType::INVALID_REQUEST);
}

bool parse_selected_token_logit(const json& response,
                                int true_token_id,
                                double& raw_logit,
                                std::string& error_message) {
    if (response.contains("error")) {
        error_message = "llama.cpp selected-token-logit request failed";
        return false;
    }

    if (!response.contains("token_logits") || !response["token_logits"].is_array()) {
        error_message = "llama.cpp response is missing token_logits array";
        return false;
    }

    for (const auto& item : response["token_logits"]) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
            !item.contains("logit") || !item["logit"].is_number()) {
            error_message = "llama.cpp response has malformed token_logits entries";
            return false;
        }

        if (item["id"].get<int>() == true_token_id) {
            raw_logit = item["logit"].get<double>();
            if (!std::isfinite(raw_logit)) {
                error_message = "llama.cpp response returned a non-finite selected-token logit";
                return false;
            }
            return true;
        }
    }

    error_message = "llama.cpp response did not include the configured selected token id";
    return false;
}

} // namespace

bool is_zeroentropy_logit_score_adapter(const RecipeOptions& options) {
    json adapter = options.get_option("llamacpp_reranking_adapter");
    return adapter.is_string() && adapter.get<std::string>() == ZEROENTROPY_LOGIT_SCORE_ADAPTER;
}

bool should_launch_native_llamacpp_reranking(ModelType model_type,
                                             const RecipeOptions& options) {
    return model_type == ModelType::RERANKING && !is_zeroentropy_logit_score_adapter(options);
}

std::string format_zeroentropy_logit_score_prompt(const std::string& query,
                                                  const std::string& document) {
    return "<|im_start|>system\n" + query + "<|im_end|>\n" +
           "<|im_start|>user\n" + document + "<|im_end|>\n" +
           "<|im_start|>assistant\n";
}

json build_zeroentropy_logit_score_completion_request(const std::string& query,
                                                      const std::string& document,
                                                      int true_token_id) {
    return {
        {"prompt", format_zeroentropy_logit_score_prompt(query, document)},
        {"n_predict", 1},
        {"temperature", 0},
        {"token_logits", json::array({true_token_id})}
    };
}

json rerank_with_zeroentropy_logit_score_adapter(
    const json& request,
    int true_token_id,
    double logit_scale,
    const std::function<json(const json&)>& completion_request) {
    if (true_token_id < 0) {
        return invalid_request("ZeroEntropy reranking adapter requires llamacpp_reranking_true_token_id");
    }
    if (logit_scale <= 0.0 || !std::isfinite(logit_scale)) {
        return invalid_request("ZeroEntropy reranking adapter requires a positive finite logit scale");
    }
    if (!request.contains("query") || !request["query"].is_string()) {
        return invalid_request("Reranking request must include a string query");
    }
    if (!request.contains("documents") || !request["documents"].is_array()) {
        return invalid_request("Reranking request must include a documents array");
    }

    const std::string query = request["query"].get<std::string>();
    std::vector<ScoredDocument> scored_documents;
    scored_documents.reserve(request["documents"].size());

    for (size_t i = 0; i < request["documents"].size(); ++i) {
        const auto& document_json = request["documents"][i];
        if (!document_json.is_string()) {
            return invalid_request("Reranking documents must be strings");
        }

        const std::string document = document_json.get<std::string>();
        json completion_response = completion_request(
            build_zeroentropy_logit_score_completion_request(query, document, true_token_id));

        double raw_logit = 0.0;
        std::string error_message;
        if (!parse_selected_token_logit(completion_response, true_token_id, raw_logit, error_message)) {
            json details = {{"document_index", i}};
            if (completion_response.contains("error")) {
                details["backend_error"] = completion_response["error"];
            }
            return ErrorResponse::create(error_message, ErrorType::BACKEND_ERROR, details);
        }

        scored_documents.push_back({
            static_cast<int>(i),
            raw_logit,
            sigmoid(raw_logit / logit_scale)
        });
    }

    std::stable_sort(scored_documents.begin(), scored_documents.end(),
        [](const ScoredDocument& lhs, const ScoredDocument& rhs) {
            return lhs.raw_logit > rhs.raw_logit;
        });

    json results = json::array();
    for (const auto& scored : scored_documents) {
        results.push_back({
            {"index", scored.index},
            {"relevance_score", scored.relevance_score}
        });
    }

    json response = {{"results", results}};
    if (request.contains("model")) {
        response["model"] = request["model"];
    }
    return response;
}

} // namespace backends
} // namespace lemon
