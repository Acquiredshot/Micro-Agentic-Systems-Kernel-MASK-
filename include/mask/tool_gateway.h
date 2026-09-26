#ifndef MASK_TOOL_GATEWAY_H
#define MASK_TOOL_GATEWAY_H

#include <stddef.h>

/**
 * @file tool_gateway.h
 * @brief Fixed-capacity tool registry and JSON manifest generator for MASK.
 */

#define MASK_MAX_TOOLS 32
#define MASK_TOOL_NAME_MAX 64
#define MASK_TOOL_DESC_MAX 256

/**
 * @brief Function signature used by all registered system tools.
 *
 * @param args_json JSON argument payload supplied by the LLM or caller.
 * @param output Output buffer written by the tool. Must be NUL-terminated.
 * @param output_size Capacity of @p output in bytes.
 * @return 0 on success, non-zero on failure.
 */
typedef int (*mask_tool_fn)(const char *args_json, char *output, size_t output_size);

/* Policy phases. Tools declare the minimum phase they require; the gateway
 * won't dispatch a tool if the daemon's current phase is below that. */
#define MASK_POLICY_PHASE_OBSERVE    "observe"
#define MASK_POLICY_PHASE_INVESTIGATE "investigate"
#define MASK_POLICY_PHASE_RESPOND    "respond"

/* Minimum phase required to call the tool. The gateway refuses dispatch
 * when the daemon's active phase is below this value. Read-only tools set
 * OBSERVE; run_shell sets INVESTIGATE; the action tool sets RESPOND. */
#define MASK_TOOL_PHASE_OBSERVE    0
#define MASK_TOOL_PHASE_INVESTIGATE 1
#define MASK_TOOL_PHASE_RESPOND    2

struct mask_tool_phase {
    int min_phase;   /* MASK_TOOL_PHASE_* */
};

/** 
 * @brief Descriptor for one named tool in the fixed registry.
 */
struct mask_tool {
    char name[MASK_TOOL_NAME_MAX];
    char description[MASK_TOOL_DESC_MAX];
    mask_tool_fn fn;
    struct mask_tool_phase phase;
};

/**
 * @brief Registry containing a bounded array of callable tools.
 */
struct mask_tool_gateway {
    struct mask_tool tools[MASK_MAX_TOOLS];
    size_t count;
    int active_phase;  /* MASK_TOOL_PHASE_* — enforced on every dispatch */
};

/**
 * @brief Initializes a tool gateway to an empty registry.
 *
 * @param gw Pointer to the gateway to initialize.
 */
void mask_tool_gateway_init(struct mask_tool_gateway *gw);

/**
 * @brief Registers a tool function in the gateway.
 *
 * @param gw Gateway to update.
 * @param name Unique tool name used by the LLM and dispatch layer.
 * @param description Human-readable description included in the tool manifest.
 * @param fn Function pointer to invoke when the tool is selected.
 * @param min_phase Minimum policy phase (MASK_TOOL_PHASE_*) required to call
 *                  this tool. The gateway refuses dispatch when the daemon's
 *                  active phase is below this value.
 * @return MASK_OK on success, MASK_ERR if the registry is full.
 */
int mask_tool_gateway_register(struct mask_tool_gateway *gw, const char *name,
                                const char *description, mask_tool_fn fn,
                                int min_phase);

/**
 * @brief Looks up and invokes a registered tool by name, enforcing the
 *        current policy phase. Tools whose min_phase exceeds current_phase
 *        are refused with an error message in @p output.
 *
 * @param gw Registry to search.
 * @param name Tool name to dispatch.
 * @param args_json JSON payload for the tool.
 * @param output Fixed buffer that receives the tool result.
 * @param output_size Capacity of @p output in bytes.
 * @param current_phase Current policy phase (MASK_TOOL_PHASE_*).
 * @return MASK_ERR if the tool is not found or not permitted in the current
 *         phase; otherwise the tool's own return code.
 */
int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size,
                                int current_phase);

/**
 * @brief Sets the active policy phase on the gateway. Affects all future
 *        dispatch calls until changed again.
 */
void mask_tool_gateway_set_phase(struct mask_tool_gateway *gw, int phase);

/**
 * @brief Returns the current active policy phase.
 */
int mask_tool_gateway_get_phase(const struct mask_tool_gateway *gw);

/**
 * @brief Builds a JSON array of registered tool metadata including phase.
 * @param gw Gateway whose tools will be serialized.
 * @return Newly allocated JSON string; caller must free() it.
 */
char *mask_tool_gateway_manifest_json(struct mask_tool_gateway *gw);

#endif /* MASK_TOOL_GATEWAY_H */
