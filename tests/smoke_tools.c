/* Standalone smoke test for the tool gateway + sandbox exec path.
 * Not part of the daemon build; compiled and run manually. */
#include "mask/tool_gateway.h"
#include "mask/ring_buffer.h"
#include "mask/common.h"
#include "mask/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int tool_sysinfo(const char *args_json, char *output, size_t output_size);
int tool_run_shell(const char *args_json, char *output, size_t output_size);
void mask_shell_set_allowlist(const char *allowlist);

int main(void) {
    /* Test 1: Without allowlist, all commands are permitted. */
    {
        struct mask_tool_gateway gw;
        mask_tool_gateway_init(&gw);
        mask_shell_set_allowlist(NULL);
        assert(mask_tool_gateway_register(&gw, "sysinfo", "sys info", tool_sysinfo, MASK_TOOL_PHASE_OBSERVE) == MASK_OK);
        assert(mask_tool_gateway_register(&gw, "run_shell", "run shell", tool_run_shell, MASK_TOOL_PHASE_INVESTIGATE) == MASK_OK);

        char *manifest = mask_tool_gateway_manifest_json(&gw);
        printf("manifest: %s\n", manifest);
        assert(strstr(manifest, "sysinfo") != NULL);
        assert(strstr(manifest, "run_shell") != NULL);
        free(manifest);

        char out[512];
        int rc = mask_tool_gateway_dispatch(&gw, "sysinfo", "{}", out, sizeof(out), MASK_TOOL_PHASE_OBSERVE);
        printf("sysinfo -> rc=%d out=%s\n", rc, out);
        assert(rc == MASK_OK);
        assert(strstr(out, "load1") != NULL);

        rc = mask_tool_gateway_dispatch(&gw, "run_shell",
            "{\"argv\":[\"echo\",\"hello from sandbox\"]}", out, sizeof(out), MASK_TOOL_PHASE_INVESTIGATE);
        printf("run_shell echo -> rc=%d out=%s\n", rc, out);
        assert(rc == MASK_OK);
        assert(strstr(out, "hello from sandbox") != NULL);

        /* Non-existent binary must fail cleanly, not crash. */
        rc = mask_tool_gateway_dispatch(&gw, "run_shell",
            "{\"argv\":[\"/no/such/binary\"]}", out, sizeof(out), MASK_TOOL_PHASE_INVESTIGATE);
        printf("run_shell bad binary -> rc=%d out=%s\n", rc, out);
        assert(rc != MASK_OK);

        /* CPU limit enforcement: a busy loop should get killed by RLIMIT_CPU. */
        rc = mask_tool_gateway_dispatch(&gw, "run_shell",
            "{\"argv\":[\"sh\",\"-c\",\"while true; do :; done\"]}", out, sizeof(out), MASK_TOOL_PHASE_INVESTIGATE);
        printf("run_shell busy-loop -> rc=%d out=%s\n", rc, out);
        assert(rc != MASK_OK);

        /* Unknown tool name. */
        rc = mask_tool_gateway_dispatch(&gw, "does_not_exist", "{}", out, sizeof(out), MASK_TOOL_PHASE_OBSERVE);
        assert(rc == MASK_ERR);

        /* Ring buffer must strip ANSI escape sequences and other control
         * characters (e.g. from a sandboxed `clear` call) before storing text,
         * so every consumer gets plain printable text. */
        struct mask_ring_buffer rb;
        assert(mask_ring_buffer_init(&rb, 4) == MASK_OK);
        mask_ring_buffer_push(&rb, "tool", "\x1b[H\x1b[2J\x1b[3Jhello\x1b[0m world\x07\x01");
        struct mask_memory_entry entries[4];
        size_t n = mask_ring_buffer_snapshot(&rb, entries, 4);
        printf("sanitized ring buffer text: '%s'\n", entries[0].text);
        assert(n == 1);
        assert(strcmp(entries[0].text, "hello world") == 0);
        mask_ring_buffer_push(&rb, "tool", "line one\nindented\ttab end");
        n = mask_ring_buffer_snapshot(&rb, entries, 4);
        assert(strcmp(entries[1].text, "line one\nindented\ttab end") == 0);
        mask_ring_buffer_destroy(&rb);

        printf("ALL SMOKE TESTS PASSED\n");
    }

    /* Test 2: With allowlist, only permitted commands run. */
    {
        struct mask_tool_gateway gw;
        mask_tool_gateway_init(&gw);
        mask_shell_set_allowlist("echo,cat,ls");
        assert(mask_tool_gateway_register(&gw, "sysinfo", "sys info", tool_sysinfo, MASK_TOOL_PHASE_OBSERVE) == MASK_OK);
        assert(mask_tool_gateway_register(&gw, "run_shell", "run shell", tool_run_shell, MASK_TOOL_PHASE_INVESTIGATE) == MASK_OK);

        char out[512];

        /* Allowed command. */
        int rc = mask_tool_gateway_dispatch(&gw, "run_shell",
            "{\"argv\":[\"echo\",\"hello\"]}", out, sizeof(out), MASK_TOOL_PHASE_INVESTIGATE);
        printf("run_shell echo (allowed) -> rc=%d out=%s\n", rc, out);
        assert(rc == MASK_OK);
        assert(strstr(out, "hello") != NULL);

        /* Disallowed command. */
        rc = mask_tool_gateway_dispatch(&gw, "run_shell",
            "{\"argv\":[\"rm\",\"-rf\",\"/\"]}", out, sizeof(out), MASK_TOOL_PHASE_INVESTIGATE);
        printf("run_shell rm (denied) -> rc=%d out=%s\n", rc, out);
        assert(rc != MASK_OK);
        assert(strstr(out, "not in allowlist") != NULL);

        printf("ALLOWLIST SMOKE TESTS PASSED\n");
    }

    return 0;
}
