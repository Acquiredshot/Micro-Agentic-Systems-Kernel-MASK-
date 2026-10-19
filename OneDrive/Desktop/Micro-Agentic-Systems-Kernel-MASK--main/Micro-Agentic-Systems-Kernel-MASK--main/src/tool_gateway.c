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
                                int min_phase, int privilege_level) {
    if (gw->count >= MASK_MAX_TOOLS) {
        MASK_LOGE("tool gateway full, cannot register '%s'", name);
        return MASK_ERR;
    }

    struct mask_tool *t = &gw->tools[gw->count++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->description, sizeof(t->description), "%s", description);
    t->fn = fn;
    t->phase.min_phase = min_phase;
    t->privilege.privilege_level = privilege_level;
    t->privilege.approval_state = MASK_TOOL_APPROVAL_PENDING;
    return MASK_OK;
}

int mask_tool_gateway_dispatch(struct mask_tool_gateway *gw, const char *name,
                                const char *args_json, char *output, size_t output_size,
                                int current_phase) {
    for (size_t i = 0; i < gw->count; i++) {
        if (strcmp(gw->tools[i].name, name) == 0) {
            /* Check policy phase */
            if (gw->tools[i].phase.min_phase > current_phase) {
                snprintf(output, output_size,
                         "error: tool '%s' requires policy phase %d but current phase is %d",
                         name, gw->tools[i].phase.min_phase, current_phase);
                return MASK_ERR;
            }

            /* Check privilege level and approval state for RESTRICTED tools */
            if (gw->tools[i].privilege.privilege_level == MASK_TOOL_PRIVILEGE_RESTRICTED) {
                if (gw->tools[i].privilege.approval_state == MASK_TOOL_APPROVAL_PENDING) {
                    snprintf(output, output_size,
                             "error: tool '%s' is RESTRICTED and requires manual approval",
                             name);
                    return MASK_ERR;
                }
                if (gw->tools[i].privilege.approval_state == MASK_TOOL_APPROVAL_REJECTED) {
                    snprintf(output, output_size,
                             "error: tool '%s' execution was rejected",
                             name);
                    return MASK_ERR;
                }
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

        const char *priv_name = "unknown";
        switch (gw->tools[i].privilege.privilege_level) {
            case MASK_TOOL_PRIVILEGE_SAFE:      priv_name = "safe"; break;
            case MASK_TOOL_PRIVILEGE_ELEVATED:  priv_name = "elevated"; break;
            case MASK_TOOL_PRIVILEGE_RESTRICTED: priv_name = "restricted"; break;
        }
        cJSON_AddStringToObject(item, "privilege", priv_name);

        if (gw->tools[i].privilege.privilege_level == MASK_TOOL_PRIVILEGE_RESTRICTED) {
            const char *approval_name = "unknown";
            switch (gw->tools[i].privilege.approval_state) {
                case MASK_TOOL_APPROVAL_PENDING:   approval_name = "pending"; break;
                case MASK_TOOL_APPROVAL_APPROVED:  approval_name = "approved"; break;
                case MASK_TOOL_APPROVAL_REJECTED:  approval_name = "rejected"; break;
            }
            cJSON_AddStringToObject(item, "approval", approval_name);
        }

        cJSON_AddItemToArray(arr, item);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

int mask_tool_gateway_set_approval(struct mask_tool_gateway *gw,
                                    const char *tool_name,
                                    int approval_state) {
    for (size_t i = 0; i < gw->count; i++) {
        if (strcmp(gw->tools[i].name, tool_name) == 0) {
            if (gw->tools[i].privilege.privilege_level != MASK_TOOL_PRIVILEGE_RESTRICTED) {
                MASK_LOGW("tool '%s' is not RESTRICTED, approval not applicable", tool_name);
                return MASK_ERR;
            }
            if (approval_state != MASK_TOOL_APPROVAL_APPROVED &&
                approval_state != MASK_TOOL_APPROVAL_REJECTED) {
                MASK_LOGE("invalid approval state for tool '%s'", tool_name);
                return MASK_ERR;
            }
            gw->tools[i].privilege.approval_state = approval_state;
            MASK_LOGI("tool '%s' approval state set to %d", tool_name, approval_state);
            return MASK_OK;
        }
    }
    MASK_LOGW("tool not found for approval: '%s'", tool_name);
    return MASK_ERR;
}

int mask_tool_gateway_get_approval(const struct mask_tool_gateway *gw,
                                    const char *tool_name) {
    for (size_t i = 0; i < gw->count; i++) {
        if (strcmp(gw->tools[i].name, tool_name) == 0) {
            return gw->tools[i].privilege.approval_state;
        }
    }
    return -1;
}

void mask_tool_gateway_reset_approvals(struct mask_tool_gateway *gw) {
    for (size_t i = 0; i < gw->count; i++) {
        if (gw->tools[i].privilege.privilege_level == MASK_TOOL_PRIVILEGE_RESTRICTED) {
            gw->tools[i].privilege.approval_state = MASK_TOOL_APPROVAL_PENDING;
        }
    }
    MASK_LOGI("all RESTRICTED tool approvals reset to PENDING");
}
