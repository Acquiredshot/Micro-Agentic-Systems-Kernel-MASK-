#include "mask/tool_gateway.h"
#include "mask/common.h"

#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

void mask_tool_gateway_init(struct mask_tool_gateway *gw) {
    memset(gw, 0, sizeof(*gw));
}

int mask_tool_gateway_register(struct mask_tool_gateway *gw, const char *name,
                                const char *description, mask_tool_fn fn) {
    if (gw->count >= MASK_MAX_TOOLS) {
        MASK_LOGE("tool gateway full, cannot register '%s'", name);
        return MASK_ERR;
    }

    struct mask_tool *t = &gw->tools[gw->count++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->description, sizeof(t->description), "%s", description);
    t->fn = fn;
    return MASK_OK;
}

int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size) {
    for (size_t i = 0; i < gw->count; i++) {
        if (strcmp(gw->tools[i].name, name) == 0) {
            return gw->tools[i].fn(args_json, output, output_size);
        }
    }
    MASK_LOGW("unknown tool requested: '%s'", name);
    return MASK_ERR;
}

char *mask_tool_gateway_manifest_json(struct mask_tool_gateway *gw) {
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < gw->count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", gw->tools[i].name);
        cJSON_AddStringToObject(item, "description", gw->tools[i].description);
        cJSON_AddItemToArray(arr, item);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
