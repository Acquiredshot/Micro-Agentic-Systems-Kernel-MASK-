#ifndef MASK_RAG_H
#define MASK_RAG_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file rag.h
 * @brief Agentic RAG with multi-tier guardrails for MASK.
 *
 * Provides intent routing, input sanitization, context bounding,
 * similarity thresholding, loop breaking, and output redaction.
 */

#define MASK_RAG_MAX_INPUT_LEN 4096
#define MASK_RAG_MAX_OUTPUT_LEN 8192
#define MASK_RAG_MIN_SIMILARITY 0.75
#define MASK_RAG_MAX_STEPS 10
#define MASK_RAG_TIMEOUT_MS 30000

/**
 * @brief Intent classification for user inputs.
 */
typedef enum {
    MASK_RAG_INTENT_UNKNOWN = 0,
    MASK_RAG_INTENT_QUERY,
    MASK_RAG_INTENT_ACTION,
    MASK_RAG_INTENT_DIAGNOSTIC
} mask_rag_intent_t;

/**
 * @brief RAG context with guardrails configuration.
 */
struct mask_rag_context {
    int enabled;
    double min_similarity_threshold;
    int max_steps;
    uint64_t timeout_ms;
    int redaction_enabled;
};

/**
 * @brief Initializes RAG context with default guardrails.
 *
 * @param ctx Context to initialize.
 */
void mask_rag_init(struct mask_rag_context *ctx);

/**
 * @brief Sanitizes user input by removing prompt injection tokens.
 *
 * @param input Raw user input.
 * @param output Sanitized output buffer.
 * @param output_size Capacity of output buffer.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_rag_sanitize_input(const char *input, char *output, size_t output_size);

/**
 * @brief Classifies the intent of a user input.
 *
 * @param input Sanitized user input.
 * @return Intent classification.
 */
mask_rag_intent_t mask_rag_classify_intent(const char *input);

/**
 * @brief Checks if retrieval is needed for a given intent.
 *
 * @param intent Classified intent.
 * @return 1 if retrieval needed, 0 otherwise.
 */
int mask_rag_needs_retrieval(mask_rag_intent_t intent);

/**
 * @brief Bounds context chunks to deterministic token sizes.
 *
 * @param context Raw context text.
 * @param max_tokens Maximum tokens to include.
 * @param output Bounded output buffer.
 * @param output_size Capacity of output buffer.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_rag_bound_context(const char *context, int max_tokens,
                            char *output, size_t output_size);

/**
 * @brief Redacts sensitive data from output (IPs, tokens, paths, PII).
 *
 * @param output Output to redact (modified in place).
 * @param output_size Capacity of output buffer.
 */
void mask_rag_redact_output(char *output, size_t output_size);

/**
 * @brief Checks if execution should stop based on step count and timeout.
 *
 * @param ctx RAG context.
 * @param step_count Current step count.
 * @param start_ms Start timestamp in milliseconds.
 * @return 1 if should stop, 0 if should continue.
 */
int mask_rag_should_stop(const struct mask_rag_context *ctx,
                         int step_count,
                         uint64_t start_ms);

/**
 * @brief Checks if a chunk meets the minimum similarity threshold.
 *
 * @param ctx RAG context.
 * @param similarity_score Calculated similarity score (0.0 to 1.0).
 * @return 1 if chunk is relevant, 0 if should be rejected.
 */
int mask_rag_is_chunk_relevant(const struct mask_rag_context *ctx,
                                double similarity_score);

#endif /* MASK_RAG_H */
