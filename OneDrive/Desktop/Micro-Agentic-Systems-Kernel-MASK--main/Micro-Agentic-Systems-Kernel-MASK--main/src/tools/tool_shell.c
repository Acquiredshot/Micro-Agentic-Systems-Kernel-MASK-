#include "mask/tool_gateway.h"
#include "mask/sandbox.h"
#include "mask/common.h"
#include "mask/config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"

#define MASK_SHELL_MAX_ARGS 16
#define MASK_SHELL_ARG_MAX 256

/* Allowlist enforcement for run_shell. When the daemon's
 * run_shell_allowlist config is set, only commands whose basename
 * appears in the comma-separated list are permitted. This is the
 * first step towards a capability policy that makes investigation
 * safe in production (gap 2.3 / roadmap §7.2).
 *
 * The allowlist pointer is set by main.c from the live config at
 * dispatch time, so it picks up runtime changes made via set_config
 * without needing a restart. */
static const char *g_shell_allowlist = NULL;

void mask_shell_set_allowlist(const char *allowlist) {
    g_shell_allowlist = allowlist;
}

/* Returns 1 if the given command's basename is in the allowlist, 0
 * otherwise. When the allowlist is NULL or empty, all commands are
 * permitted (the original behaviour). */
static int cmd_allowed(const char *cmd) {
    if (!g_shell_allowlist || !g_shell_allowlist[0]) return 1;

    /* Extract basename: last component after the final '/' */
    const char *base = strrchr(cmd, '/');
    base = base ? base + 1 : cmd;

    /* Parse comma-separated allowlist */
    char *list = strdup(g_shell_allowlist);
    if (!list) return 0;

    int allowed = 0;
    char *tok = strtok(list, ",");
    while (tok) {
        /* Trim whitespace */
        while (*tok == ' ' || *tok == '\t') tok++;
        char *end = tok + strlen(tok) - 1;
        while (end > tok && (*end == ' ' || *end == '\t' || *end == '\n')) *end-- = '\0';

        if (strcmp(base, tok) == 0) {
            allowed = 1;
            break;
        }
        tok = strtok(NULL, ",");
    }
    free(list);
    return allowed;
}

/* Expects args_json of the form {"argv": ["echo", "hello"]}. Deliberately
 * takes an argv array rather than a raw command string so there is no shell
 * involved and therefore no shell-metacharacter injection surface. */
int tool_run_shell(const char *args_json, char *output, size_t output_size) {
    cJSON *parsed = cJSON_Parse(args_json);
    if (!parsed) {
        snprintf(output, output_size, "error: invalid JSON args");
        return MASK_ERR;
    }

    cJSON *argv_json = cJSON_GetObjectItemCaseSensitive(parsed, "argv");
    if (!cJSON_IsArray(argv_json) || cJSON_GetArraySize(argv_json) == 0) {
        snprintf(output, output_size, "error: 'argv' must be a non-empty array of strings");
        cJSON_Delete(parsed);
        return MASK_ERR;
    }

    int n = cJSON_GetArraySize(argv_json);
    if (n > MASK_SHELL_MAX_ARGS - 1) {
        n = MASK_SHELL_MAX_ARGS - 1;
    }

    /* Fixed arena: no per-arg heap allocation. Static storage assumes the
     * tool gateway serializes calls into this tool (true today: main.c only
     * ever has one LLM worker thread active at a time). If tools are ever
     * dispatched concurrently, move this onto the stack or add a lock. */
    static char arg_storage[MASK_SHELL_MAX_ARGS][MASK_SHELL_ARG_MAX];
    char *argv[MASK_SHELL_MAX_ARGS];

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(argv_json, i);
        if (!cJSON_IsString(item)) {
            snprintf(output, output_size, "error: argv[%d] is not a string", i);
            cJSON_Delete(parsed);
            return MASK_ERR;
        }
        snprintf(arg_storage[i], MASK_SHELL_ARG_MAX, "%s", item->valuestring);
        argv[i] = arg_storage[i];
    }
    argv[n] = NULL;

    /* Allowlist check: if configured, verify the command is permitted. */
    if (!cmd_allowed(argv[0])) {
        cJSON_Delete(parsed);
        snprintf(output, output_size, "error: command '%s' not in allowlist", argv[0]);
        MASK_LOGW("tool_run_shell: '%s' denied by allowlist", argv[0]);
        return MASK_ERR;
    }

    cJSON_Delete(parsed);

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);

    int exit_code = mask_sandbox_exec(argv, &limits, output, output_size);
    MASK_LOGI("tool_run_shell: '%s' exited with code %d", argv[0], exit_code);
    return (exit_code == 0) ? MASK_OK : MASK_ERR;
}
