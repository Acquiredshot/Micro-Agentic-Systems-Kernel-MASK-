#include "mask/tool_gateway.h"
#include "mask/sandbox.h"
#include "mask/common.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/*
 * tool_action — structured response actions for the WOLF-PAK security platform.
 *
 * Accepts JSON of the form {"action":"<name>","args":{...}} and maps each
 * action to a pre-approved command executed inside the sandbox. This is the
 * Action Gateway layer: unlike run_shell (which takes arbitrary argv), this
 * tool only permits a fixed set of response actions with typed parameters.
 *
 * Known actions:
 *   kill_process      {"pid": <int>}              — kill -9 <pid>
 *   disable_user      {"username": "<name>"}     — passwd -l <username> (lock account)
 *   isolate_network   {}                         — iptables -A OUTPUT -j DROP (all outbound)
 *   rotate_credential {"username": "<name>"}     — passwd -e <username> (force expiry)
 */

static int run_action(const char *action_name, char *const argv[], char *output, size_t output_size) {
    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);
    limits.wall_timeout_sec = 30;

    char raw[4096];
    int rc = mask_sandbox_exec(argv, &limits, raw, sizeof(raw));
    if (rc != 0) {
        snprintf(output, output_size,
                 "{\"action\":\"%s\",\"status\":\"error\",\"exit_code\":%d,\"stderr\":\"%s\"}",
                 action_name, rc, raw);
        return MASK_ERR;
    }
    snprintf(output, output_size,
             "{\"action\":\"%s\",\"status\":\"ok\",\"exit_code\":%d,\"stdout\":\"%s\"}",
             action_name, rc, raw);
    return MASK_OK;
}

int tool_action(const char *args_json, char *output, size_t output_size) {
    cJSON *parsed = cJSON_Parse(args_json);
    if (!parsed) {
        snprintf(output, output_size, "{\"status\":\"error\",\"message\":\"invalid JSON\"}");
        return MASK_ERR;
    }

    cJSON *action_obj = cJSON_GetObjectItemCaseSensitive(parsed, "action");
    if (!cJSON_IsString(action_obj) || !action_obj->valuestring) {
        snprintf(output, output_size, "{\"status\":\"error\",\"message\":\"missing 'action' field\"}");
        cJSON_Delete(parsed);
        return MASK_ERR;
    }

    const char *action = action_obj->valuestring;

    if (strcmp(action, "kill_process") == 0) {
        cJSON *pid_obj = cJSON_GetObjectItemCaseSensitive(parsed, "pid");
        if (!cJSON_IsNumber(pid_obj)) {
            snprintf(output, output_size, "{\"status\":\"error\",\"message\":\"kill_process requires numeric 'pid'\"}");
            cJSON_Delete(parsed);
            return MASK_ERR;
        }
        unsigned long pid = (unsigned long)pid_obj->valuedouble;
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%lu", pid);

        char *const argv[] = { "kill", "-9", pid_str, NULL };
        cJSON_Delete(parsed);
        return run_action(action, argv, output, output_size);
    }

    if (strcmp(action, "disable_user") == 0) {
        cJSON *user_obj = cJSON_GetObjectItemCaseSensitive(parsed, "username");
        if (!cJSON_IsString(user_obj) || !user_obj->valuestring) {
            snprintf(output, output_size, "{\"status\":\"error\",\"message\":\"disable_user requires 'username'\"}");
            cJSON_Delete(parsed);
            return MASK_ERR;
        }
        static char user_buf[256];
        snprintf(user_buf, sizeof(user_buf), "%s", user_obj->valuestring);

        char *const argv[] = { "passwd", "-l", user_buf, NULL };
        cJSON_Delete(parsed);
        return run_action(action, argv, output, output_size);
    }

    if (strcmp(action, "isolate_network") == 0) {
        /* Block all outbound traffic via iptables. Requires root. */
        char *const argv[] = { "iptables", "-A", "OUTPUT", "-j", "DROP", NULL };
        cJSON_Delete(parsed);
        return run_action(action, argv, output, output_size);
    }

    if (strcmp(action, "rotate_credential") == 0) {
        cJSON *user_obj = cJSON_GetObjectItemCaseSensitive(parsed, "username");
        if (!cJSON_IsString(user_obj) || !user_obj->valuestring) {
            snprintf(output, output_size, "{\"status\":\"error\",\"message\":\"rotate_credential requires 'username'\"}");
            cJSON_Delete(parsed);
            return MASK_ERR;
        }
        static char user_buf[256];
        snprintf(user_buf, sizeof(user_buf), "%s", user_obj->valuestring);

        char *const argv[] = { "passwd", "-e", user_buf, NULL };
        cJSON_Delete(parsed);
        return run_action(action, argv, output, output_size);
    }

    /* Unknown action — list known actions. */
    snprintf(output, output_size,
             "{\"status\":\"error\",\"message\":\"unknown action '%s'. "
             "Known actions: kill_process, disable_user, isolate_network, rotate_credential\"}",
             action);
    cJSON_Delete(parsed);
    return MASK_ERR;
}
