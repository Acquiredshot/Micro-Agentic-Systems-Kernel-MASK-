#include "mask/tool_gateway.h"
#include "mask/sandbox.h"
#include "mask/common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

/*
 * process_list — snapshot of all running processes via ps.
 * Arguments: optional {"extra": "cmdline"} to include the full command line
 *            (may be very long); defaults to just pid, user, stat, cpu, mem, vsz, rss, name.
 * Output: JSON array of process objects.
 */
int tool_process_list(const char *args_json, char *output, size_t output_size) {
    int want_cmdline = 0;
    if (args_json) {
        cJSON *parsed = cJSON_Parse(args_json);
        if (parsed) {
            cJSON *ex = cJSON_GetObjectItemCaseSensitive(parsed, "extra");
            if (cJSON_IsString(ex) && ex->valuestring &&
                strcmp(ex->valuestring, "cmdline") == 0) {
                want_cmdline = 1;
            }
            cJSON_Delete(parsed);
        }
    }

    /* Build argv for ps. Each flag must be its own argv entry. */
    char *argv[16];
    int argc = 0;
    argv[argc++] = "ps";
    if (want_cmdline) {
        argv[argc++] = "-eo";
        argv[argc++] = "pid,user,stat,pcpu,pmem,vsz,rss,args";
    } else {
        argv[argc++] = "-eo";
        argv[argc++] = "pid,user,stat,pcpu,pmem,vsz,rss,comm";
    }
    argv[argc++] = "--no-headers";
    argv[argc] = NULL;

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);
    limits.wall_timeout_sec = 10;

    char raw[16384];
    int rc = mask_sandbox_exec(argv, &limits, raw, sizeof(raw));
    if (rc != 0) {
        snprintf(output, output_size,
                 "{\"error\":\"ps exited with code %d\",\"raw_output\":\"%s\"}",
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

        char *tokens[8];
        int n = 0;
        char *tok = strtok(line, " \t");
        while (tok && n < 8) {
            tokens[n++] = tok;
            tok = strtok(NULL, " \t");
        }

        if (n < 3) {
            line = strtok(NULL, "\n");
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "pid", tokens[0]);
        cJSON_AddStringToObject(obj, "user", tokens[1]);
        cJSON_AddStringToObject(obj, "stat", tokens[2]);
        if (n > 3) cJSON_AddStringToObject(obj, "cpu", tokens[3]);
        if (n > 4) cJSON_AddStringToObject(obj, "mem", tokens[4]);
        if (n > 5) cJSON_AddStringToObject(obj, "vsz", tokens[5]);
        if (n > 6) cJSON_AddStringToObject(obj, "rss", tokens[6]);
        if (want_cmdline && n > 7) {
            /* Rebuild the cmdline from remaining tokens */
            char cmdline[1024] = "";
            size_t pos = 0;
            for (int i = 7; i < n && pos < (int)sizeof(cmdline) - 1; i++) {
                size_t len = strlen(tokens[i]);
                if (pos + len + 1 <= sizeof(cmdline)) {
                    if (pos > 0) {
                        cmdline[pos++] = ' ';
                    }
                    memcpy(cmdline + pos, tokens[i], len);
                    pos += len;
                }
            }
            cmdline[pos] = '\0';
            cJSON_AddStringToObject(obj, "cmdline", cmdline);
        } else if (!want_cmdline && n > 7) {
            cJSON_AddStringToObject(obj, "comm", tokens[7]);
        }

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
 * user_sessions — currently logged-in users via 'who'.
 * Arguments: none.
 * Output: JSON array of session objects:
 *   [{"user":"alice","tty":"pts/0","host":"192.168.1.10",
 *     "login":"2026-09-26 10:00"},
 *    ...]
 */
int tool_user_sessions(const char *args_json, char *output, size_t output_size) {
    (void)args_json;

    char *const argv[] = {
        "who", NULL
    };

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);
    limits.wall_timeout_sec = 10;

    char raw[4096];
    int rc = mask_sandbox_exec(argv, &limits, raw, sizeof(raw));
    if (rc != 0) {
        snprintf(output, output_size,
                 "{\"error\":\"who exited with code %d\",\"raw_output\":\"%s\"}",
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

        /* who outputs: user  tty       date time (and optionally host)
         * Example: alice  pts/0     2026-09-26 10:00 (192.168.1.10)
         */
        char user[64] = "", tty[32] = "", date[64] = "", time_str[32] = "", host[64] = "";
        int n = sscanf(line, "%63s %31s %63s %31s %63s", user, tty, date, time_str, host);
        if (n < 4) {
            line = strtok(NULL, "\n");
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "user", user);
        cJSON_AddStringToObject(obj, "tty", tty);
        cJSON_AddStringToObject(obj, "login", n >= 4 ? "" : "");
        char login_buf[128] = "";
        if (n >= 4) {
            snprintf(login_buf, sizeof(login_buf), "%s %s", date, time_str);
        }
        cJSON_AddStringToObject(obj, "login", login_buf);
        if (n >= 5 && host[0]) {
            cJSON_AddStringToObject(obj, "host", host);
        }

        cJSON_AddItemToArray(arr, obj);
        line = strtok(NULL, "\n");
    }

    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    snprintf(output, output_size, "%s", out ? out : "[]");
    free(out);
    return MASK_OK;
}
