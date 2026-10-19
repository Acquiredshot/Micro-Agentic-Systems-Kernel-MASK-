#include "mask/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "cJSON.h"

/* Tool: net_check - Perform lightweight HTTP status and latency checks */
int tool_net_check(const char *args_json, char *output, size_t output_size) {
    if (!args_json || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(output, output_size, "error: invalid JSON arguments");
        return MASK_ERR;
    }

    cJSON *url_obj = cJSON_GetObjectItemCaseSensitive(args, "url");
    if (!cJSON_IsString(url_obj) || !url_obj->valuestring) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: missing 'url' argument");
        return MASK_ERR;
    }

    const char *url = url_obj->valuestring;

    /* Validate URL starts with http:// or https:// */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: URL must start with http:// or https://");
        return MASK_ERR;
    }

    /* Use curl to check the URL with timeout */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s -o /dev/null -w '%%{http_code} %%{time_total}' --max-time 5 '%s' 2>&1", url);

    FILE *f = popen(cmd, "r");
    if (!f) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot execute curl: %s", strerror(errno));
        return MASK_ERR;
    }

    char buf[128];
    if (fgets(buf, sizeof(buf), f)) {
        /* Remove trailing newline */
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }
        snprintf(output, output_size, "{\"status\":\"%s\"}", buf);
    } else {
        snprintf(output, output_size, "error: no response from curl");
    }
    pclose(f);

    cJSON_Delete(args);
    return MASK_OK;
}
