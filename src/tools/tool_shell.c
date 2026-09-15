#include "mask/tool_gateway.h"
#include "mask/sandbox.h"
#include "mask/common.h"

#include <string.h>
#include <stdio.h>

#include "cJSON.h"

#define MASK_SHELL_MAX_ARGS 16
#define MASK_SHELL_ARG_MAX 256

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

    cJSON_Delete(parsed);

    struct mask_sandbox_limits limits;
    mask_sandbox_default_limits(&limits);

    int exit_code = mask_sandbox_exec(argv, &limits, output, output_size);
    MASK_LOGI("tool_run_shell: '%s' exited with code %d", argv[0], exit_code);
    return (exit_code == 0) ? MASK_OK : MASK_ERR;
}
