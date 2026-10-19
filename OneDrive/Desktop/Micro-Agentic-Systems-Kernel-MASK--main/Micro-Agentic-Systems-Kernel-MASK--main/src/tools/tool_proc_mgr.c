#include "mask/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "cJSON.h"

/* Tool: proc_info - Query process information by PID or list all processes */
int tool_proc_info(const char *args_json, char *output, size_t output_size) {
    if (!args_json || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(output, output_size, "error: invalid JSON arguments");
        return MASK_ERR;
    }

    cJSON *pid_obj = cJSON_GetObjectItemCaseSensitive(args, "pid");
    int pid = cJSON_IsNumber(pid_obj) ? (int)pid_obj->valuedouble : -1;

    char cmd[256];
    if (pid > 0) {
        snprintf(cmd, sizeof(cmd), "cat /proc/%d/status 2>/dev/null || echo 'Process not found'", pid);
    } else {
        snprintf(cmd, sizeof(cmd), "ps aux --sort=-pcpu | head -20");
    }

    FILE *f = popen(cmd, "r");
    if (!f) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot execute command: %s", strerror(errno));
        return MASK_ERR;
    }

    char buf[512];
    size_t total = 0;
    while (fgets(buf, sizeof(buf), f) && total < output_size - 1) {
        size_t len = strlen(buf);
        if (total + len < output_size - 1) {
            memcpy(output + total, buf, len);
            total += len;
        }
    }
    output[total] = '\0';
    pclose(f);

    cJSON_Delete(args);
    return MASK_OK;
}
