#pragma once

#include "../model_types.h"
#include "../recipe_options.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace lemon {
namespace backends {

constexpr const char* ZEROENTROPY_LOGIT_SCORE_ADAPTER = "zeroentropy-logit-score";
constexpr int ZEROENTROPY_TRUE_TOKEN_ID = 9454;
constexpr double ZEROENTROPY_LOGIT_SCALE = 5.0;

bool is_zeroentropy_logit_score_adapter(const RecipeOptions& options);
bool should_launch_native_llamacpp_reranking(ModelType model_type,
                                             const RecipeOptions& options);

std::string format_zeroentropy_logit_score_prompt(const std::string& query,
                                                  const std::string& document);

json build_zeroentropy_logit_score_completion_request(const std::string& query,
                                                      const std::string& document,
                                                      int true_token_id);

json rerank_with_zeroentropy_logit_score_adapter(
    const json& request,
    int true_token_id,
    double logit_scale,
    const std::function<json(const json&)>& completion_request);

} // namespace backends
} // namespace lemon
