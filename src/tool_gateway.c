#include "mask/tool_gateway.h"
#include "mask/common.h"

#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

void mask_tool_gateway_init(struct mask_tool_gateway *gw) {
    memset(gw, 0, sizeof(*gw));
    gw->active_phase = MASK_TOOL_PHASE_OBSERVE;
}

int mask_tool_gateway_register(struct mask_tool_gateway *gw, const char *name,
                                const char *description, mask_tool_fn fn,
                                int min_phase) {
    if (gw->count >= MASK_MAX_TOOLS) {
        MASK_LOGE("tool gateway full, cannot register '%s'", name);
        return MASK_ERR;
    }

    struct mask_tool *t = &gw->tools[gw->count++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->description, sizeof(t->description), "%s", description);
    t->fn = fn;
    t->phase.min_phase = min_phase;
    return MASK_OK;
}

int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size,
                                int current_phase) {
    for (size_t i = 0; i < gw->count; i++) {
        if (strcmp(gw->tools[i].name, name) == 0) {
            if (gw->tools[i].phase.min_phase > current_phase) {
                snprintf(output, output_size,
                         "error: tool '%s' requires policy phase %d but current phase is %d",
                         name, gw->tools[i].phase.min_phase, current_phase);
                return MASK_ERR;
            }
            return gw->tools[i].fn(args_json, output, output_size);
        }
    }
    MASK_LOGW("unknown tool requested: '%s'", name);
    return MASK_ERR;
}

void mask_tool_gateway_set_phase(struct mask_tool_gateway *gw, int phase) {
    gw->active_phase = phase;
}

int mask_tool_gateway_get_phase(const struct mask_tool_gateway *gw) {
    return gw->active_phase;
}

char *mask_tool_gateway_manifest_json(struct mask_tool_gateway *gw) {
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < gw->count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", gw->tools[i].name);
        cJSON_AddStringToObject(item, "description", gw->tools[i].description);
        const char *ph_name = "unknown";
        switch (gw->tools[i].phase.min_phase) {
            case MASK_TOOL_PHASE_OBSERVE:     ph_name = "observe"; break;
            case MASK_TOOL_PHASE_INVESTIGATE: ph_name = "investigate"; break;
            case MASK_TOOL_PHASE_RESPOND:     ph_name = "respond"; break;
        }
        cJSON_AddStringToObject(item, "phase", ph_name);
        cJSON_AddItemToArray(arr, item);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
