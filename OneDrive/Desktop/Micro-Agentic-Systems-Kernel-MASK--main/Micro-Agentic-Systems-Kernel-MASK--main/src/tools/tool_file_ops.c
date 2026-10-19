#include "mask/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"

#define MAX_PATH_DEPTH 8
#define MAX_FILE_SIZE (10 * 1024 * 1024) /* 10MB limit */

/* Validates that a path is within the allowed depth and doesn't escape
 * the current working directory. */
static int validate_path(const char *path) {
    if (!path || !*path) {
        return MASK_ERR;
    }

    /* Reject absolute paths */
    if (path[0] == '/') {
        MASK_LOGE("file_ops: absolute paths not allowed");
        return MASK_ERR;
    }

    /* Reject path traversal attempts */
    if (strstr(path, "..") != NULL) {
        MASK_LOGE("file_ops: path traversal not allowed");
        return MASK_ERR;
    }

    /* Count path depth */
    int depth = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            depth++;
            if (depth > MAX_PATH_DEPTH) {
                MASK_LOGE("file_ops: path depth exceeds limit");
                return MASK_ERR;
            }
        }
    }

    return MASK_OK;
}

/* Tool: file_read - Read a file with bounded size and path validation */
int tool_file_read(const char *args_json, char *output, size_t output_size) {
    if (!args_json || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(output, output_size, "error: invalid JSON arguments");
        return MASK_ERR;
    }

    cJSON *path_obj = cJSON_GetObjectItemCaseSensitive(args, "path");
    if (!cJSON_IsString(path_obj) || !path_obj->valuestring) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: missing 'path' argument");
        return MASK_ERR;
    }

    const char *path = path_obj->valuestring;
    if (validate_path(path) != MASK_OK) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: invalid path");
        return MASK_ERR;
    }

    /* Check file size before reading */
    struct stat st;
    if (stat(path, &st) != 0) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot stat file: %s", strerror(errno));
        return MASK_ERR;
    }

    if (!S_ISREG(st.st_mode)) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: not a regular file");
        return MASK_ERR;
    }

    if (st.st_size > MAX_FILE_SIZE) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: file too large (max %d bytes)", MAX_FILE_SIZE);
        return MASK_ERR;
    }

    /* Read file */
    FILE *f = fopen(path, "r");
    if (!f) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot open file: %s", strerror(errno));
        return MASK_ERR;
    }

    size_t bytes_to_read = (size_t)st.st_size;
    if (bytes_to_read >= output_size) {
        bytes_to_read = output_size - 1;
    }

    size_t read = fread(output, 1, bytes_to_read, f);
    output[read] = '\0';
    fclose(f);

    cJSON_Delete(args);
    return MASK_OK;
}

/* Tool: file_write - Write to a file with bounded size and path validation */
int tool_file_write(const char *args_json, char *output, size_t output_size) {
    if (!args_json || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(output, output_size, "error: invalid JSON arguments");
        return MASK_ERR;
    }

    cJSON *path_obj = cJSON_GetObjectItemCaseSensitive(args, "path");
    cJSON *content_obj = cJSON_GetObjectItemCaseSensitive(args, "content");

    if (!cJSON_IsString(path_obj) || !path_obj->valuestring ||
        !cJSON_IsString(content_obj) || !content_obj->valuestring) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: missing 'path' or 'content' argument");
        return MASK_ERR;
    }

    const char *path = path_obj->valuestring;
    const char *content = content_obj->valuestring;

    if (validate_path(path) != MASK_OK) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: invalid path");
        return MASK_ERR;
    }

    size_t content_len = strlen(content);
    if (content_len > MAX_FILE_SIZE) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: content too large (max %d bytes)", MAX_FILE_SIZE);
        return MASK_ERR;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot open file for writing: %s", strerror(errno));
        return MASK_ERR;
    }

    size_t written = fwrite(content, 1, content_len, f);
    fclose(f);

    if (written != content_len) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: incomplete write");
        return MASK_ERR;
    }

    snprintf(output, output_size, "wrote %zu bytes to %s", written, path);
    cJSON_Delete(args);
    return MASK_OK;
}

/* Tool: file_list - List directory contents with bounded depth */
int tool_file_list(const char *args_json, char *output, size_t output_size) {
    if (!args_json || !output || output_size == 0) {
        return MASK_ERR;
    }

    output[0] = '\0';

    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        snprintf(output, output_size, "error: invalid JSON arguments");
        return MASK_ERR;
    }

    cJSON *path_obj = cJSON_GetObjectItemCaseSensitive(args, "path");
    const char *path = cJSON_IsString(path_obj) && path_obj->valuestring ? path_obj->valuestring : ".";

    if (validate_path(path) != MASK_OK) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: invalid path");
        return MASK_ERR;
    }

    FILE *f = popen("ls -la", "r");
    if (!f) {
        cJSON_Delete(args);
        snprintf(output, output_size, "error: cannot execute ls: %s", strerror(errno));
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
