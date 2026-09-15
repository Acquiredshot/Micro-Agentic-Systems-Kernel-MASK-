#include "mask/llm_client.h"
#include "mask/common.h"

#include <string.h>
#include <stdlib.h>

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
