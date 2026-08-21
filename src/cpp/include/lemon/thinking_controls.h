// Cross-backend "disable thinking" normalization for OpenAI-style chat
// requests. Lemonade's public convention is backend-neutral: clients express
// intent via `enable_thinking: false` (or the OpenAI-compat `thinking` field).
//
// The server translates that intent into native OpenAI-compatible reasoning
// controls before backend dispatch. A `/no_think` prefix is retained as a
// compatibility fallback for backends/templates that do not consume those
// controls yet. Client-facing thinking fields are stripped after translation.
//
// Shared by the HTTP request path (server.cpp) and by internal
// classifier/router invocations (routing_classifier_services.cpp), so a
// constrained classifier call is normalized exactly like a normal request.

#pragma once

#include <nlohmann/json.hpp>

namespace lemon {

using json = nlohmann::json;

bool should_disable_thinking(const json& request_json);

bool prepend_no_think_to_last_user_message(json& request_json);

bool strip_handled_thinking_fields(json& request_json);

bool normalize_thinking_controls(json& request_json);

} // namespace lemon
