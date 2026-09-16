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

/**
 * @brief Descriptor for one named tool in the fixed registry.
 */
struct mask_tool {
    char name[MASK_TOOL_NAME_MAX];
    char description[MASK_TOOL_DESC_MAX];
    mask_tool_fn fn;
};

/**
 * @brief Registry containing a bounded array of callable tools.
 */
struct mask_tool_gateway {
    struct mask_tool tools[MASK_MAX_TOOLS];
    size_t count;
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
 * @return MASK_OK on success, MASK_ERR if the registry is full.
 */
int mask_tool_gateway_register(struct mask_tool_gateway *gw, const char *name,
                                const char *description, mask_tool_fn fn);

/**
 * @brief Looks up and invokes a registered tool by name.
 *
 * @param gw Registry to search.
 * @param name Tool name to dispatch.
 * @param args_json JSON payload for the tool.
 * @param output Fixed buffer that receives the tool result.
 * @param output_size Capacity of @p output in bytes.
 * @return MASK_ERR if the tool is not found; otherwise the tool's own return code.
 */
int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size);

/**
 * @brief Builds a JSON array containing the registered tool metadata.
 *
 * @param gw Gateway whose tools will be serialized.
 * @return Newly allocated JSON string; caller must free() it.
 */
char *mask_tool_gateway_manifest_json(struct mask_tool_gateway *gw);

#endif /* MASK_TOOL_GATEWAY_H */
