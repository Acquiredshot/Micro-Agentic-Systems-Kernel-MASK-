#include "mask/tool_gateway.h"
#include "mask/sandbox.h"
#include "mask/common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

/*
 * net_connections — active TCP/UDP connections via ss.
 * Arguments: optional {"proto": "tcp"} or {"proto": "udp"}; defaults to both.
 * Output: JSON array of connection objects:
 *   [{"proto":"tcp","state":"ESTAB","src":"192.168.1.5:43210",
 *     "dst":"93.184.216.34:443","pid":1234,"process":"nginx"},
 *    ...]
 */
int tool_net_connections(const char *args_json, char *output, size_t output_size) {
    const char *proto_filter = NULL;
    if (args_json) {
        cJSON *parsed = cJSON_Parse(args_json);
        if (parsed) {
            cJSON *pf = cJSON_GetObjectItemCaseSensitive(parsed, "proto");
            if (cJSON_IsString(pf) && pf->valuestring) {
                proto_filter = pf->valuestring;
            }
            cJSON_Delete(parsed);
        }
    }

    /* Build argv for ss. Each flag must be its own argv entry for execvp. */
    char *argv[8];
    int argc = 0;
    argv[argc++] = "ss";
    if (proto_filter && strcmp(proto_filter, "udp") == 0) {
        argv[argc++] = "-u";
    } else if (proto_filter && strcmp(proto_filter, "tcp") == 0) {
        argv[argc++] = "-t";
    } else {
        argv[argc++] = "-t";
        argv[argc++] = "-u";
    }
    argv[argc++] = "-a";
    argv[argc++] = "-n";
    argv[argc++] = "-p";
    argv[argc] = NULL;

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);
    limits.wall_timeout_sec = 10;

    char raw[8192];
    int rc = mask_sandbox_exec(argv, &limits, raw, sizeof(raw));
    if (rc != 0) {
        snprintf(output, output_size,
                 "{\"error\":\"ss exited with code %d\",\"raw_output\":\"%s\"}",
                 rc, raw);
        return MASK_ERR;
    }

    /* Parse ss output into JSON.
     * ss -tunapl outputs lines like:
     *   Netid  State   Recv-Q  Send-Q  Local Address:Port   Peer Address:Port  Process
     *   tcp    ESTAB   0       0       192.168.1.5:43210    93.184.216.34:443  users:(("nginx",pid=1234,fd=5))
     */
    cJSON *arr = cJSON_CreateArray();
    char *line = strtok(raw, "\n");
    while (line) {
        /* Skip header lines from ss output */
        if (line[0] == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }
        if (line[0] == 'N') {
            line = strtok(NULL, "\n");
            continue;
        }
        if (line[0] == 'S' && line[1] == 't') {
            line = strtok(NULL, "\n");
            continue;
        }

        /* Tokenize by whitespace */
        char *tokens[8];
        int n = 0;
        char *tok = strtok(line, " \t");
        while (tok && n < 8) {
            tokens[n++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (n < 5) {
            line = strtok(NULL, "\n");
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "proto", tokens[0]);
        cJSON_AddStringToObject(obj, "state", tokens[1]);
        cJSON_AddStringToObject(obj, "local", tokens[4]);
        cJSON_AddStringToObject(obj, "peer", tokens[5]);

        /* Extract PID and process name from the users:((...)) field if present */
        char pid_str[32] = "";
        char proc_str[64] = "";
        for (int i = 5; i < n; i++) {
            if (strstr(tokens[i], "pid=")) {
                char *p = strstr(tokens[i], "pid=");
                if (p) {
                    p += 4;
                    int j = 0;
                    while (*p && *p >= '0' && *p <= '9' && j < 31) {
                        pid_str[j++] = *p++;
                    }
                    pid_str[j] = '\0';
                }
            }
            if (strstr(tokens[i], "\"") && !proc_str[0]) {
                /* First quoted string is the process name */
                char *q1 = strchr(tokens[i], '"');
                if (q1) {
                    q1++;
                    char *q2 = strchr(q1, '"');
                    if (q2) {
                        size_t len = q2 - q1;
                        if (len > 63) len = 63;
                        memcpy(proc_str, q1, len);
                        proc_str[len] = '\0';
                    }
                }
            }
        }
        if (pid_str[0]) cJSON_AddStringToObject(obj, "pid", pid_str);
        if (proc_str[0]) cJSON_AddStringToObject(obj, "process", proc_str);

        cJSON_AddItemToArray(arr, obj);
        line = strtok(NULL, "\n");
    }

    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    snprintf(output, output_size, "%s", out ? out : "[]");
    free(out);
    return MASK_OK;
}

/*
 * listening_sockets — TCP/UDP sockets in LISTEN state via ss.
 * Arguments: none.
 * Output: JSON array of listening socket objects:
 *   [{"proto":"tcp","local":"0.0.0.0:22","peer":"*:*",
 *     "pid":890,"process":"sshd"},
 *    ...]
 */
int tool_listening_sockets(const char *args_json, char *output, size_t output_size) {
    (void)args_json;

    char *argv[8];
    int argc = 0;
    argv[argc++] = "ss";
    argv[argc++] = "-t";
    argv[argc++] = "-u";
    argv[argc++] = "-l";
    argv[argc++] = "-n";
    argv[argc++] = "-p";
    argv[argc] = NULL;

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);
    limits.wall_timeout_sec = 10;

    char raw[8192];
    int rc = mask_sandbox_exec(argv, &limits, raw, sizeof(raw));
    if (rc != 0) {
        snprintf(output, output_size,
                 "{\"error\":\"ss exited with code %d\",\"raw_output\":\"%s\"}",
                 rc, raw);
        return MASK_ERR;
    }

    cJSON *arr = cJSON_CreateArray();
    char *line = strtok(raw, "\n");
    while (line) {
        if (line[0] == '\0') {
            line = strtok(NULL, "\n");
            continue;
        }
        if (line[0] == 'N') {
            line = strtok(NULL, "\n");
            continue;
        }
        if (line[0] == 'S' && line[1] == 't') {
            line = strtok(NULL, "\n");
            continue;
        }

        char *tokens[8];
        int n = 0;
        char *tok = strtok(line, " \t");
        while (tok && n < 8) {
            tokens[n++] = tok;
            tok = strtok(NULL, " \t");
        }
        if (n < 5) {
            line = strtok(NULL, "\n");
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "proto", tokens[0]);
        cJSON_AddStringToObject(obj, "local", tokens[4]);
        cJSON_AddStringToObject(obj, "peer", tokens[5]);

        char pid_str[32] = "";
        char proc_str[64] = "";
        for (int i = 5; i < n; i++) {
            if (strstr(tokens[i], "pid=")) {
                char *p = strstr(tokens[i], "pid=");
                if (p) {
                    p += 4;
                    int j = 0;
                    while (*p && *p >= '0' && *p <= '9' && j < 31) {
                        pid_str[j++] = *p++;
                    }
                    pid_str[j] = '\0';
                }
            }
            if (strstr(tokens[i], "\"") && !proc_str[0]) {
                char *q1 = strchr(tokens[i], '"');
                if (q1) {
                    q1++;
                    char *q2 = strchr(q1, '"');
                    if (q2) {
                        size_t len = q2 - q1;
                        if (len > 63) len = 63;
                        memcpy(proc_str, q1, len);
                        proc_str[len] = '\0';
                    }
                }
            }
        }
        if (pid_str[0]) cJSON_AddStringToObject(obj, "pid", pid_str);
        if (proc_str[0]) cJSON_AddStringToObject(obj, "process", proc_str);

        cJSON_AddItemToArray(arr, obj);
        line = strtok(NULL, "\n");
    }

    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    snprintf(output, output_size, "%s", out ? out : "[]");
    free(out);
    return MASK_OK;
}
