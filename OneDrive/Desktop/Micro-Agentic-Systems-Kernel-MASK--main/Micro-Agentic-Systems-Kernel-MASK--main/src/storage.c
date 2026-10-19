#include "mask/storage.h"
#include "mask/common.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>

/* SQLite amalgamation - embedded for zero external dependency */
#include "sqlite3_stub.h"

#define MASK_STORAGE_DEFAULT_PATH "mask_storage.db"
#define MASK_STORAGE_MAX_TEXT_LEN 65536

struct mask_storage {
    sqlite3 *db;
    char path[MASK_STORAGE_PATH_MAX];
};

/* Helper: escape single quotes in SQL strings */
static void escape_sql_string(char *dest, const char *src, size_t dest_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dest_size - 2; i++) {
        if (src[i] == '\'') {
            if (j + 2 < dest_size) {
                dest[j++] = '\'';
                dest[j++] = '\'';
            }
        } else {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

/* Helper: truncate string to maximum length */
static const char *truncate_string(const char *str, char *buf, size_t buf_size, size_t max_len) {
    if (!str) {
        buf[0] = '\0';
        return buf;
    }

    size_t len = strlen(str);
    if (len <= max_len) {
        return str;
    }

    size_t copy_len = max_len < buf_size ? max_len : buf_size - 1;
    memcpy(buf, str, copy_len);
    buf[copy_len] = '\0';
    return buf;
}

static int init_schema(struct mask_storage *storage) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS storage_entries ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp_ms INTEGER NOT NULL,"
        "  entry_type INTEGER NOT NULL,"
        "  asset_id TEXT,"
        "  level TEXT,"
        "  component TEXT,"
        "  tool_name TEXT,"
        "  args_json TEXT,"
        "  output_json TEXT,"
        "  exit_code INTEGER,"
        "  prompt TEXT,"
        "  response TEXT,"
        "  message TEXT"
        ");"

        "CREATE INDEX IF NOT EXISTS idx_timestamp ON storage_entries(timestamp_ms);"
        "CREATE INDEX IF NOT EXISTS idx_type ON storage_entries(entry_type);"
        "CREATE INDEX IF NOT EXISTS idx_asset ON storage_entries(asset_id);";

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to create schema: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    return MASK_OK;
}

struct mask_storage *mask_storage_open(const char *path) {
    struct mask_storage *storage = calloc(1, sizeof(struct mask_storage));
    if (!storage) {
        MASK_LOGE("storage: failed to allocate storage handle");
        return NULL;
    }

    const char *db_path = path ? path : MASK_STORAGE_DEFAULT_PATH;
    snprintf(storage->path, sizeof(storage->path), "%s", db_path);

    int rc = sqlite3_open(storage->path, &storage->db);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to open database '%s': %s",
                  storage->path, sqlite3_errmsg(storage->db));
        free(storage);
        return NULL;
    }

    /* Configure SQLite for better performance and safety */
    sqlite3_exec(storage->db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(storage->db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_exec(storage->db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);

    if (init_schema(storage) != MASK_OK) {
        sqlite3_close(storage->db);
        free(storage);
        return NULL;
    }

    MASK_LOGI("storage: opened database at '%s'", storage->path);
    return storage;
}

void mask_storage_close(struct mask_storage *storage) {
    if (!storage) {
        return;
    }

    if (storage->db) {
        sqlite3_close(storage->db);
    }

    free(storage);
}

int mask_storage_store_telemetry(struct mask_storage *storage,
                                  uint64_t timestamp_ms,
                                  const char *asset_id,
                                  const char *payload_json) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    char asset_buf[MASK_STORAGE_PATH_MAX];
    char payload_buf[MASK_STORAGE_MAX_TEXT_LEN];

    const char *asset = truncate_string(asset_id, asset_buf, sizeof(asset_buf), 256);
    const char *payload = truncate_string(payload_json, payload_buf, sizeof(payload_buf), MASK_STORAGE_MAX_TEXT_LEN);

    char sql[4096];
    char asset_esc[512];
    char payload_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];

    escape_sql_string(asset_esc, asset ? asset : "", sizeof(asset_esc));
    escape_sql_string(payload_esc, payload ? payload : "", sizeof(payload_esc));

    snprintf(sql, sizeof(sql),
             "INSERT INTO storage_entries "
             "(timestamp_ms, entry_type, asset_id, output_json) "
             "VALUES (%llu, %d, '%s', '%s');",
             (unsigned long long)timestamp_ms,
             MASK_STORAGE_ENTRY_TELEMETRY,
             asset_esc,
             payload_esc);

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to store telemetry: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    return MASK_OK;
}

int mask_storage_store_log(struct mask_storage *storage,
                            uint64_t timestamp_ms,
                            const char *level,
                            const char *component,
                            const char *message) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    char level_buf[64];
    char comp_buf[64];
    char msg_buf[MASK_STORAGE_MAX_TEXT_LEN];

    const char *lvl = truncate_string(level, level_buf, sizeof(level_buf), 32);
    const char *comp = truncate_string(component, comp_buf, sizeof(comp_buf), 32);
    const char *msg = truncate_string(message, msg_buf, sizeof(msg_buf), MASK_STORAGE_MAX_TEXT_LEN);

    char sql[4096];
    char level_esc[128];
    char comp_esc[128];
    char msg_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];

    escape_sql_string(level_esc, lvl ? lvl : "", sizeof(level_esc));
    escape_sql_string(comp_esc, comp ? comp : "", sizeof(comp_esc));
    escape_sql_string(msg_esc, msg ? msg : "", sizeof(msg_esc));

    snprintf(sql, sizeof(sql),
             "INSERT INTO storage_entries "
             "(timestamp_ms, entry_type, level, component, message) "
             "VALUES (%llu, %d, '%s', '%s', '%s');",
             (unsigned long long)timestamp_ms,
             MASK_STORAGE_ENTRY_LOG,
             level_esc,
             comp_esc,
             msg_esc);

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to store log: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    return MASK_OK;
}

int mask_storage_store_tool_output(struct mask_storage *storage,
                                    uint64_t timestamp_ms,
                                    const char *tool_name,
                                    const char *args_json,
                                    const char *output_json,
                                    int exit_code) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    char tool_buf[64];
    char args_buf[MASK_STORAGE_MAX_TEXT_LEN];
    char output_buf[MASK_STORAGE_MAX_TEXT_LEN];

    const char *tool = truncate_string(tool_name, tool_buf, sizeof(tool_buf), 32);
    const char *args = truncate_string(args_json, args_buf, sizeof(args_buf), MASK_STORAGE_MAX_TEXT_LEN);
    const char *output = truncate_string(output_json, output_buf, sizeof(output_buf), MASK_STORAGE_MAX_TEXT_LEN);

    char sql[8192];
    char tool_esc[128];
    char args_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];
    char output_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];

    escape_sql_string(tool_esc, tool ? tool : "", sizeof(tool_esc));
    escape_sql_string(args_esc, args ? args : "", sizeof(args_esc));
    escape_sql_string(output_esc, output ? output : "", sizeof(output_esc));

    snprintf(sql, sizeof(sql),
             "INSERT INTO storage_entries "
             "(timestamp_ms, entry_type, tool_name, args_json, output_json, exit_code) "
             "VALUES (%llu, %d, '%s', '%s', '%s', %d);",
             (unsigned long long)timestamp_ms,
             MASK_STORAGE_ENTRY_TOOL_OUTPUT,
             tool_esc,
             args_esc,
             output_esc,
             exit_code);

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to store tool output: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    return MASK_OK;
}

int mask_storage_store_llm_reasoning(struct mask_storage *storage,
                                      uint64_t timestamp_ms,
                                      const char *prompt,
                                      const char *response,
                                      const char *tool_called) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    char prompt_buf[MASK_STORAGE_MAX_TEXT_LEN];
    char response_buf[MASK_STORAGE_MAX_TEXT_LEN];
    char tool_buf[64];

    const char *prmpt = truncate_string(prompt, prompt_buf, sizeof(prompt_buf), MASK_STORAGE_MAX_TEXT_LEN);
    const char *resp = truncate_string(response, response_buf, sizeof(response_buf), MASK_STORAGE_MAX_TEXT_LEN);
    const char *tool = truncate_string(tool_called, tool_buf, sizeof(tool_buf), 32);

    char sql[8192];
    char prompt_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];
    char response_esc[MASK_STORAGE_MAX_TEXT_LEN * 2];
    char tool_esc[128];

    escape_sql_string(prompt_esc, prmpt ? prmpt : "", sizeof(prompt_esc));
    escape_sql_string(response_esc, resp ? resp : "", sizeof(response_esc));
    escape_sql_string(tool_esc, tool ? tool : "", sizeof(tool_esc));

    snprintf(sql, sizeof(sql),
             "INSERT INTO storage_entries "
             "(timestamp_ms, entry_type, tool_name, prompt, response) "
             "VALUES (%llu, %d, '%s', '%s', '%s');",
             (unsigned long long)timestamp_ms,
             MASK_STORAGE_ENTRY_LLM_REASONING,
             tool_esc,
             prompt_esc,
             response_esc);

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to store LLM reasoning: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    return MASK_OK;
}

int mask_storage_query_history(struct mask_storage *storage,
                                int entry_type,
                                int limit,
                                char *output_json,
                                size_t output_size) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    if (output_json && output_size > 0) {
        output_json[0] = '\0';
    }

    char sql[512];
    if (entry_type >= 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, timestamp_ms, entry_type, asset_id, level, component, "
                 "tool_name, args_json, output_json, exit_code, prompt, response, message "
                 "FROM storage_entries WHERE entry_type = %d "
                 "ORDER BY timestamp_ms DESC LIMIT %d;",
                 entry_type, limit > 0 ? limit : 1000);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, timestamp_ms, entry_type, asset_id, level, component, "
                 "tool_name, args_json, output_json, exit_code, prompt, response, message "
                 "FROM storage_entries "
                 "ORDER BY timestamp_ms DESC LIMIT %d;",
                 limit > 0 ? limit : 1000);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(storage->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to prepare query: %s", sqlite3_errmsg(storage->db));
        return MASK_ERR;
    }

    /* Build JSON array manually */
    char *json = malloc(output_size);
    if (!json) {
        sqlite3_finalize(stmt);
        return MASK_ERR;
    }
    json[0] = '[';
    json[1] = '\0';
    size_t json_len = 1;

    int row_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && json_len < output_size - 256) {
        if (row_count > 0) {
            json_len += snprintf(json + json_len, output_size - json_len, ",");
        }

        int64_t id = sqlite3_column_int64(stmt, 0);
        int64_t ts = sqlite3_column_int64(stmt, 1);
        int type = sqlite3_column_int(stmt, 2);

        const char *asset = (const char *)sqlite3_column_text(stmt, 3);
        const char *level = (const char *)sqlite3_column_text(stmt, 4);
        const char *component = (const char *)sqlite3_column_text(stmt, 5);
        const char *tool_name = (const char *)sqlite3_column_text(stmt, 6);
        const char *args_json = (const char *)sqlite3_column_text(stmt, 7);
        const char *output_json_col = (const char *)sqlite3_column_text(stmt, 8);
        int exit_code = sqlite3_column_int(stmt, 9);
        const char *prompt = (const char *)sqlite3_column_text(stmt, 10);
        const char *response = (const char *)sqlite3_column_text(stmt, 11);
        const char *message = (const char *)sqlite3_column_text(stmt, 12);

        json_len += snprintf(json + json_len, output_size - json_len,
                             "{\"id\":%lld,\"timestamp_ms\":%lld,\"type\":%d",
                             (long long)id, (long long)ts, type);

        if (asset) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"asset_id\":\"%s\"", asset);
        }
        if (level) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"level\":\"%s\"", level);
        }
        if (component) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"component\":\"%s\"", component);
        }
        if (tool_name) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"tool_name\":\"%s\"", tool_name);
        }
        if (args_json) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"args\":%s", args_json);
        }
        if (output_json_col) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"output\":%s", output_json_col);
        }
        if (exit_code != 0) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"exit_code\":%d", exit_code);
        }
        if (prompt) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"prompt\":\"%s\"", prompt);
        }
        if (response) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"response\":\"%s\"", response);
        }
        if (message) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"message\":\"%s\"", message);
        }

        json_len += snprintf(json + json_len, output_size - json_len, "}");
        row_count++;
    }

    sqlite3_finalize(stmt);

    if (json_len < output_size) {
        json_len += snprintf(json + json_len, output_size - json_len, "]");
    }

    if (output_json && output_size > 0) {
        snprintf(output_json, output_size, "%s", json);
    }

    free(json);
    return MASK_OK;
}

int mask_storage_query_time_range(struct mask_storage *storage,
                                   int entry_type,
                                   uint64_t start_ms,
                                   uint64_t end_ms,
                                   int limit,
                                   char *output_json,
                                   size_t output_size) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    if (output_json && output_size > 0) {
        output_json[0] = '\0';
    }

    char sql[512];
    if (entry_type >= 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, timestamp_ms, entry_type, asset_id, level, component, "
                 "tool_name, args_json, output_json, exit_code, prompt, response, message "
                 "FROM storage_entries WHERE entry_type = %d "
                 "AND timestamp_ms >= %llu AND timestamp_ms <= %llu "
                 "ORDER BY timestamp_ms DESC LIMIT %d;",
                 entry_type,
                 (unsigned long long)start_ms,
                 (unsigned long long)end_ms,
                 limit > 0 ? limit : 1000);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT id, timestamp_ms, entry_type, asset_id, level, component, "
                 "tool_name, args_json, output_json, exit_code, prompt, response, message "
                 "FROM storage_entries "
                 "WHERE timestamp_ms >= %llu AND timestamp_ms <= %llu "
                 "ORDER BY timestamp_ms DESC LIMIT %d;",
                 (unsigned long long)start_ms,
                 (unsigned long long)end_ms,
                 limit > 0 ? limit : 1000);
    }

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(storage->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to prepare time range query: %s", sqlite3_errmsg(storage->db));
        return MASK_ERR;
    }

    /* Reuse the same JSON building logic as query_history */
    char *json = malloc(output_size);
    if (!json) {
        sqlite3_finalize(stmt);
        return MASK_ERR;
    }
    json[0] = '[';
    json[1] = '\0';
    size_t json_len = 1;

    int row_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && json_len < output_size - 256) {
        if (row_count > 0) {
            json_len += snprintf(json + json_len, output_size - json_len, ",");
        }

        int64_t id = sqlite3_column_int64(stmt, 0);
        int64_t ts = sqlite3_column_int64(stmt, 1);
        int type = sqlite3_column_int(stmt, 2);

        const char *asset = (const char *)sqlite3_column_text(stmt, 3);
        const char *level = (const char *)sqlite3_column_text(stmt, 4);
        const char *component = (const char *)sqlite3_column_text(stmt, 5);
        const char *tool_name = (const char *)sqlite3_column_text(stmt, 6);
        const char *args_json = (const char *)sqlite3_column_text(stmt, 7);
        const char *output_json_col = (const char *)sqlite3_column_text(stmt, 8);
        int exit_code = sqlite3_column_int(stmt, 9);
        const char *prompt = (const char *)sqlite3_column_text(stmt, 10);
        const char *response = (const char *)sqlite3_column_text(stmt, 11);
        const char *message = (const char *)sqlite3_column_text(stmt, 12);

        json_len += snprintf(json + json_len, output_size - json_len,
                             "{\"id\":%lld,\"timestamp_ms\":%lld,\"type\":%d",
                             (long long)id, (long long)ts, type);

        if (asset) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"asset_id\":\"%s\"", asset);
        }
        if (level) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"level\":\"%s\"", level);
        }
        if (component) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"component\":\"%s\"", component);
        }
        if (tool_name) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"tool_name\":\"%s\"", tool_name);
        }
        if (args_json) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"args\":%s", args_json);
        }
        if (output_json_col) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"output\":%s", output_json_col);
        }
        if (exit_code != 0) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"exit_code\":%d", exit_code);
        }
        if (prompt) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"prompt\":\"%s\"", prompt);
        }
        if (response) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"response\":\"%s\"", response);
        }
        if (message) {
            json_len += snprintf(json + json_len, output_size - json_len,
                                 ",\"message\":\"%s\"", message);
        }

        json_len += snprintf(json + json_len, output_size - json_len, "}");
        row_count++;
    }

    sqlite3_finalize(stmt);

    if (json_len < output_size) {
        json_len += snprintf(json + json_len, output_size - json_len, "]");
    }

    if (output_json && output_size > 0) {
        snprintf(output_json, output_size, "%s", json);
    }

    free(json);
    return MASK_OK;
}

int mask_storage_prune_old(struct mask_storage *storage, uint64_t max_age_ms) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    uint64_t cutoff = mask_now_ms() - max_age_ms;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "DELETE FROM storage_entries WHERE timestamp_ms < %llu;",
             (unsigned long long)cutoff);

    char *err_msg = NULL;
    int rc = sqlite3_exec(storage->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to prune old records: %s", err_msg);
        sqlite3_free(err_msg);
        return MASK_ERR;
    }

    int deleted = sqlite3_changes(storage->db);
    MASK_LOGI("storage: pruned %d old records", deleted);
    return deleted;
}

int mask_storage_count(struct mask_storage *storage) {
    if (!storage || !storage->db) {
        return MASK_ERR;
    }

    const char *sql = "SELECT COUNT(*) FROM storage_entries;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(storage->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        MASK_LOGE("storage: failed to prepare count query: %s", sqlite3_errmsg(storage->db));
        return MASK_ERR;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}
