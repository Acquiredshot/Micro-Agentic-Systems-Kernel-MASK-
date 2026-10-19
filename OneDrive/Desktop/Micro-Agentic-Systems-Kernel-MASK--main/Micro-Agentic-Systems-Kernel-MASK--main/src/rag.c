#include "mask/rag.h"
#include "mask/common.h"

#include <string.h>
#include <ctype.h>
#include <regex.h>

/* Prompt injection patterns to strip */
static const char *INJECTION_PATTERNS[] = {
    "ignore previous instructions",
    "disregard all above",
    "forget everything",
    "new instructions:",
    "override protocol",
    "bypass safety",
    "system prompt:",
    "developer mode",
    NULL
};

/* Regex patterns for sensitive data redaction */
static const char *REDACT_PATTERNS[] = {
    "\\b[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\b",  /* IPv4 */
    "\\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}\\b",    /* Email */
    "\\b[A-Za-z0-9]{32,}\\b",                                   /* API tokens/keys */
    "\\b/[a-zA-Z0-9_/]+/[a-zA-Z0-9_/]+\\b",                    /* File paths */
    NULL
};

void mask_rag_init(struct mask_rag_context *ctx) {
    if (!ctx) {
        return;
    }

    ctx->enabled = 1;
    ctx->min_similarity_threshold = MASK_RAG_MIN_SIMILARITY;
    ctx->max_steps = MASK_RAG_MAX_STEPS;
    ctx->timeout_ms = MASK_RAG_TIMEOUT_MS;
    ctx->redaction_enabled = 1;
}

int mask_rag_sanitize_input(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    /* Copy input to output */
    size_t len = strlen(input);
    if (len >= output_size) {
        len = output_size - 1;
    }
    memcpy(output, input, len);
    output[len] = '\0';

    /* Strip injection patterns (case-insensitive) */
    for (int i = 0; INJECTION_PATTERNS[i] != NULL; i++) {
        const char *pattern = INJECTION_PATTERNS[i];
        char *pos = output;

        while ((pos = strcasestr(pos, pattern)) != NULL) {
            /* Replace with asterisks */
            size_t pat_len = strlen(pattern);
            memset(pos, '*', pat_len);
            pos += pat_len;
        }
    }

    /* Strip control characters except newlines and tabs */
    char *dst = output;
    for (size_t i = 0; output[i] && dst < output + output_size - 1; i++) {
        if (iscntrl((unsigned char)output[i]) && output[i] != '\n' && output[i] != '\t') {
            continue;
        }
        *dst++ = output[i];
    }
    *dst = '\0';

    return MASK_OK;
}

mask_rag_intent_t mask_rag_classify_intent(const char *input) {
    if (!input || !*input) {
        return MASK_RAG_INTENT_UNKNOWN;
    }

    /* Simple keyword-based classification */
    if (strstr(input, "run") || strstr(input, "execute") ||
        strstr(input, "start") || strstr(input, "kill") ||
        strstr(input, "stop") || strstr(input, "restart")) {
        return MASK_RAG_INTENT_ACTION;
    }

    if (strstr(input, "what") || strstr(input, "how") ||
        strstr(input, "why") || strstr(input, "explain") ||
        strstr(input, "show") || strstr(input, "list")) {
        return MASK_RAG_INTENT_QUERY;
    }

    if (strstr(input, "check") || strstr(input, "diagnose") ||
        strstr(input, "status") || strstr(input, "health") ||
        strstr(input, "monitor")) {
        return MASK_RAG_INTENT_DIAGNOSTIC;
    }

    return MASK_RAG_INTENT_UNKNOWN;
}

int mask_rag_needs_retrieval(mask_rag_intent_t intent) {
    /* Only retrieval-based intents need context retrieval */
    return (intent == MASK_RAG_INTENT_QUERY || intent == MASK_RAG_INTENT_DIAGNOSTIC);
}

int mask_rag_bound_context(const char *context, int max_tokens,
                            char *output, size_t output_size) {
    if (!context || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    /* Approximate token count (4 chars per token average) */
    size_t max_chars = (size_t)max_tokens * 4;
    if (max_chars >= output_size) {
        max_chars = output_size - 1;
    }

    size_t len = strlen(context);
    if (len <= max_chars) {
        snprintf(output, output_size, "%s", context);
        return MASK_OK;
    }

    /* Truncate at a word boundary */
    size_t copy_len = max_chars;
    while (copy_len > 0 && !isspace((unsigned char)context[copy_len])) {
        copy_len--;
    }

    if (copy_len == 0) {
        copy_len = max_chars;
    }

    memcpy(output, context, copy_len);
    output[copy_len] = '\0';

    return MASK_OK;
}

void mask_rag_redact_output(char *output, size_t output_size) {
    if (!output || output_size == 0) {
        return;
    }

    /* Simple redaction: replace sensitive patterns with [REDACTED] */
    const char *replacement = "[REDACTED]";
    size_t repl_len = strlen(replacement);

    for (int i = 0; REDACT_PATTERNS[i] != NULL; i++) {
        regex_t regex;
        if (regcomp(&regex, REDACT_PATTERNS[i], REG_EXTENDED | REG_ICASE) != 0) {
            continue;
        }

        regmatch_t match;
        char *pos = output;
        while (regexec(&regex, pos, 1, &match, 0) == 0) {
            size_t match_start = match.rm_so;
            size_t match_end = match.rm_eo;
            size_t match_len = match_end - match_start;

            if (match_len + pos - output < output_size - repl_len) {
                memmove(pos + match_start + repl_len, pos + match_end,
                        strlen(pos + match_end) + 1);
                memcpy(pos + match_start, replacement, repl_len);
                pos += match_start + repl_len;
            } else {
                break;
            }
        }

        regfree(&regex);
    }
}

int mask_rag_should_stop(const struct mask_rag_context *ctx,
                         int step_count,
                         uint64_t start_ms) {
    if (!ctx) {
        return 0;
    }

    /* Check step count limit */
    if (step_count >= ctx->max_steps) {
        MASK_LOGW("RAG: step count limit reached (%d)", ctx->max_steps);
        return 1;
    }

    /* Check timeout */
    uint64_t elapsed = mask_now_ms() - start_ms;
    if (elapsed >= ctx->timeout_ms) {
        MASK_LOGW("RAG: timeout reached (%llu ms)", (unsigned long long)elapsed);
        return 1;
    }

    return 0;
}

int mask_rag_is_chunk_relevant(const struct mask_rag_context *ctx,
                                double similarity_score) {
    if (!ctx) {
        return 1; /* Default to accepting if no context */
    }

    if (similarity_score < ctx->min_similarity_threshold) {
        MASK_LOGD("RAG: chunk rejected (similarity %.2f < threshold %.2f)",
                  similarity_score, ctx->min_similarity_threshold);
        return 0;
    }

    return 1;
}
