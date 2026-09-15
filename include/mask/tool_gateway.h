#ifndef MASK_TOOL_GATEWAY_H
#define MASK_TOOL_GATEWAY_H

#include <stddef.h>

#define MASK_MAX_TOOLS 32
#define MASK_TOOL_NAME_MAX 64
#define MASK_TOOL_DESC_MAX 256

/* A tool takes its arguments as a JSON string and writes a JSON (or plain
 * text) result into output. Returns 0 on success, non-zero on failure. */
typedef int (*mask_tool_fn)(const char *args_json, char *output, size_t output_size);

struct mask_tool {
    char name[MASK_TOOL_NAME_MAX];
    char description[MASK_TOOL_DESC_MAX];
    mask_tool_fn fn;
};

struct mask_tool_gateway {
    struct mask_tool tools[MASK_MAX_TOOLS];
    size_t count;
};

void mask_tool_gateway_init(struct mask_tool_gateway *gw);

/* Returns MASK_OK, or MASK_ERR if the table is full. */
int mask_tool_gateway_register(struct mask_tool_gateway *gw, const char *name,
                                const char *description, mask_tool_fn fn);

/* Looks up `name` and invokes it. Returns MASK_ERR if not found, otherwise
 * the tool's own return value. */
int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size);

/* Builds a JSON array describing all registered tools (name + description),
 * suitable for embedding in an LLM prompt. Caller must free() the result. */
char *mask_tool_gateway_manifest_json(struct mask_tool_gateway *gw);

#endif /* MASK_TOOL_GATEWAY_H */
