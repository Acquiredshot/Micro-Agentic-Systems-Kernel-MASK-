#include "mask/llm_client.h"
#include "mask/common.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <curl/curl.h>
#include "cJSON.h"

/* Bounded growable buffer for the HTTP response body. Capped so a
 * misbehaving endpoint can't exhaust memory. */
#define MASK_LLM_MAX_RESPONSE_BYTES (2 * 1024 * 1024)

struct curl_buf {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    struct curl_buf *buf = (struct curl_buf *)userp;
    size_t add = size * nmemb;

    if (buf->len + add > MASK_LLM_MAX_RESPONSE_BYTES) {
        return 0; /* signals error to curl, aborts transfer */
    }

    if (buf->len + add + 1 > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap;
        while (new_cap < buf->len + add + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(buf->data, new_cap);
        if (!grown) {
            return 0;
        }
        buf->data = grown;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, contents, add);
    buf->len += add;
    buf->data[buf->len] = '\0';
    return add;
}

int mask_llm_client_global_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        MASK_LOGE("curl_global_init failed");
        return MASK_ERR;
    }
    return MASK_OK;
}

void mask_llm_client_global_cleanup(void) {
    curl_global_cleanup();
}

/* Robust JSON extraction: finds the first complete JSON object between
 * top-level braces, handling markdown fences, conversational prose, and
 * unescaped control characters. Returns a newly allocated string containing
 * just the JSON payload, or NULL if no valid JSON could be extracted. */
static char *extract_json_payload(const char *response) {
    if (!response || !*response) {
        return NULL;
    }

    const char *start = response;
    const char *json_start = NULL;
    const char *json_end = NULL;
    int brace_depth = 0;
    int in_string = 0;
    int escape_next = 0;

    /* Skip markdown code fences: ```json or ``` */
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (strncmp(start, "```json", 7) == 0 || strncmp(start, "```", 3) == 0) {
        const char *fence_end = strchr(start, '\n');
        if (fence_end) {
            start = fence_end + 1;
        }
    }

    /* Find the opening brace of the JSON object */
    for (const char *p = start; *p; p++) {
        if (escape_next) {
            escape_next = 0;
            continue;
        }

        if (*p == '\\' && in_string) {
            escape_next = 1;
            continue;
        }

        if (*p == '"') {
            in_string = !in_string;
            continue;
        }

        if (!in_string) {
            if (*p == '{') {
                if (brace_depth == 0) {
                    json_start = p;
                }
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
                if (brace_depth == 0 && json_start) {
                    json_end = p + 1;
                    break;
                }
            }
        }
    }

    if (!json_start || !json_end) {
        return NULL;
    }

    /* Extract the JSON substring */
    size_t json_len = json_end - json_start;
    char *json = malloc(json_len + 1);
    if (!json) {
        return NULL;
    }

    memcpy(json, json_start, json_len);
    json[json_len] = '\0';

    /* Validate with cJSON to ensure it's actually valid JSON */
    cJSON *test = cJSON_Parse(json);
    if (!test) {
        free(json);
        return NULL;
    }
    cJSON_Delete(test);

    return json;
}

/* Validates tool arguments against a JSON schema. Currently performs
 * basic validation: ensures args is a JSON object if present, and checks
 * for required fields based on tool name. Returns MASK_OK if valid,
 * MASK_ERR otherwise. */
static int validate_tool_args(const char *tool_name, cJSON *args) {
    if (!args) {
        /* No args provided - allow for tools that don't require them */
        return MASK_OK;
    }

    if (!cJSON_IsObject(args)) {
        MASK_LOGE("tool '%s': args must be a JSON object", tool_name);
        return MASK_ERR;
    }

    /* Tool-specific validation rules */
    if (strcmp(tool_name, "run_shell") == 0) {
        cJSON *argv = cJSON_GetObjectItemCaseSensitive(args, "argv");
        if (!argv || !cJSON_IsArray(argv)) {
            MASK_LOGE("tool 'run_shell': missing or invalid 'argv' array");
            return MASK_ERR;
        }
        if (cJSON_GetArraySize(argv) == 0) {
            MASK_LOGE("tool 'run_shell': 'argv' array cannot be empty");
            return MASK_ERR;
        }
    }

    return MASK_OK;
}

int mask_llm_chat(const char *endpoint, const char *model,
                   const char *system_prompt, const char *user_prompt,
                   char *response, size_t response_size) {
    int rc = MASK_ERR;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    char *body = NULL;
    struct curl_buf resp_buf = {0};
    char url[512];

    if (response && response_size > 0) {
        response[0] = '\0';
    }

    curl = curl_easy_init();
    if (!curl) {
        MASK_LOGE("curl_easy_init failed");
        return MASK_ERR;
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", model);
    cJSON_AddStringToObject(req, "prompt", user_prompt);
    if (system_prompt && *system_prompt) {
        cJSON_AddStringToObject(req, "system", system_prompt);
    }
    cJSON_AddBoolToObject(req, "stream", 0);
    body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) {
        MASK_LOGE("failed to serialize LLM request body");
        goto cleanup;
    }

    snprintf(url, sizeof(url), "%s/api/generate", endpoint);

    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        MASK_LOGE("LLM request failed: %s", curl_easy_strerror(res));
        goto cleanup;
    }

    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 200 || http_status >= 300) {
        MASK_LOGE("LLM endpoint returned HTTP %ld", http_status);
        goto cleanup;
    }

    if (!resp_buf.data) {
        MASK_LOGE("LLM endpoint returned empty body");
        goto cleanup;
    }

    cJSON *parsed = cJSON_Parse(resp_buf.data);
    if (!parsed) {
        MASK_LOGE("failed to parse LLM response JSON");
        goto cleanup;
    }

    cJSON *text = cJSON_GetObjectItemCaseSensitive(parsed, "response");
    if (!cJSON_IsString(text) || !text->valuestring) {
        MASK_LOGE("LLM response JSON missing 'response' string field");
        cJSON_Delete(parsed);
        goto cleanup;
    }

    if (response) {
        snprintf(response, response_size, "%s", text->valuestring);
    }
    cJSON_Delete(parsed);
    rc = MASK_OK;

cleanup:
    if (headers) curl_slist_free_all(headers);
    if (body) free(body);
    if (resp_buf.data) free(resp_buf.data);
    if (curl) curl_easy_cleanup(curl);
    return rc;
}

/* Parses the LLM response to extract tool call information using robust
 * JSON extraction. Handles markdown fences, conversational prose, and
 * unstructured responses gracefully. Returns MASK_OK on success, MASK_ERR
 * on failure. */
int mask_llm_parse_tool_call(const char *llm_response,
                              char *tool_name, size_t tool_name_size,
                              char *args_json, size_t args_json_size,
                              char *say_text, size_t say_text_size) {
    if (!llm_response || !*llm_response) {
        return MASK_ERR;
    }

    if (tool_name && tool_name_size > 0) {
        tool_name[0] = '\0';
    }
    if (args_json && args_json_size > 0) {
        args_json[0] = '\0';
    }
    if (say_text && say_text_size > 0) {
        say_text[0] = '\0';
    }

    /* Try to extract JSON payload from the response */
    char *json = extract_json_payload(llm_response);
    if (!json) {
        /* No valid JSON found - treat entire response as natural language */
        if (say_text && say_text_size > 0) {
            snprintf(say_text, say_text_size, "%s", llm_response);
        }
        MASK_LOGD("LLM response contained no valid JSON, treating as natural language");
        return MASK_OK;
    }

    cJSON *parsed = cJSON_Parse(json);
    free(json);
    if (!parsed) {
        /* JSON extraction found braces but content wasn't valid JSON */
        if (say_text && say_text_size > 0) {
            snprintf(say_text, say_text_size, "%s", llm_response);
        }
        MASK_LOGD("Extracted content was not valid JSON, treating as natural language");
        return MASK_OK;
    }

    /* Extract tool name */
    cJSON *tool = cJSON_GetObjectItemCaseSensitive(parsed, "tool");
    if (cJSON_IsString(tool) && tool->valuestring && *tool->valuestring) {
        if (tool_name && tool_name_size > 0) {
            snprintf(tool_name, tool_name_size, "%s", tool->valuestring);
        }

        /* Extract and validate tool arguments */
        cJSON *args = cJSON_GetObjectItemCaseSensitive(parsed, "args");
        if (validate_tool_args(tool->valuestring, args) == MASK_OK) {
            if (args_json && args_json_size > 0) {
                char *args_str = cJSON_PrintUnformatted(args);
                if (args_str) {
                    snprintf(args_json, args_json_size, "%s", args_str);
                    free(args_str);
                } else {
                    snprintf(args_json, args_json_size, "{}");
                }
            }
        } else {
            cJSON_Delete(parsed);
            return MASK_ERR;
        }
    } else {
        /* tool is null or missing - extract "say" field if present */
        cJSON *say = cJSON_GetObjectItemCaseSensitive(parsed, "say");
        if (cJSON_IsString(say) && say->valuestring) {
            if (say_text && say_text_size > 0) {
                snprintf(say_text, say_text_size, "%s", say->valuestring);
            }
        } else {
            /* No tool and no say - treat as natural language */
            if (say_text && say_text_size > 0) {
                snprintf(say_text, say_text_size, "%s", llm_response);
            }
        }
    }

    cJSON_Delete(parsed);
    return MASK_OK;
}
