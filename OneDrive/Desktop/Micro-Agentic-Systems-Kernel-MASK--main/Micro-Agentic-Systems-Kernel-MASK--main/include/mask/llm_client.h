#ifndef MASK_LLM_CLIENT_H
#define MASK_LLM_CLIENT_H

#include <stddef.h>

/* Must be called once before any mask_llm_chat() call, and
 * mask_llm_client_global_cleanup() once at shutdown (wraps curl_global_init/
 * cleanup, which are not thread-safe to call concurrently with requests). */
int mask_llm_client_global_init(void);
void mask_llm_client_global_cleanup(void);

/* Sends a single non-streaming chat/generate request to an Ollama-compatible
 * endpoint (POST {endpoint}/api/generate) with the given system + user
 * prompt, and copies the model's textual reply into response (truncated to
 * response_size - 1, NUL-terminated). Safe to call from any thread once
 * global_init has run. Returns MASK_OK on success, MASK_ERR on transport or
 * parse failure. */
int mask_llm_chat(const char *endpoint, const char *model,
                   const char *system_prompt, const char *user_prompt,
                   char *response, size_t response_size);

/* Parses the LLM response to extract tool call information using robust
 * JSON extraction. Handles markdown fences, conversational prose, and
 * unstructured responses gracefully. Returns MASK_OK on success, MASK_ERR
 * on failure. */
int mask_llm_parse_tool_call(const char *llm_response,
                              char *tool_name, size_t tool_name_size,
                              char *args_json, size_t args_json_size,
                              char *say_text, size_t say_text_size);

#endif /* MASK_LLM_CLIENT_H */
