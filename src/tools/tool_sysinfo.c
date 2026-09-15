#include "mask/tool_gateway.h"
#include "mask/common.h"

#include <stdio.h>
#include <string.h>

/* Reads a handful of cheap, always-present /proc metrics and returns them
 * as a small JSON object. Ignores args_json (no parameters needed). */
int tool_sysinfo(const char *args_json, char *output, size_t output_size) {
    (void)args_json;

    double load1 = 0, load5 = 0, load15 = 0;
    FILE *f = fopen("/proc/loadavg", "r");
    if (f) {
        if (fscanf(f, "%lf %lf %lf", &load1, &load5, &load15) != 3) {
            load1 = load5 = load15 = -1;
        }
        fclose(f);
    }

    long mem_total_kb = -1, mem_available_kb = -1;
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[64];
        long value;
        char unit[16];
        while (fscanf(f, "%63s %ld %15s", key, &value, unit) == 3) {
            if (strcmp(key, "MemTotal:") == 0) {
                mem_total_kb = value;
            } else if (strcmp(key, "MemAvailable:") == 0) {
                mem_available_kb = value;
            }
        }
        fclose(f);
    }

    snprintf(output, output_size,
             "{\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f,"
             "\"mem_total_kb\":%ld,\"mem_available_kb\":%ld}",
             load1, load5, load15, mem_total_kb, mem_available_kb);

    return MASK_OK;
}
