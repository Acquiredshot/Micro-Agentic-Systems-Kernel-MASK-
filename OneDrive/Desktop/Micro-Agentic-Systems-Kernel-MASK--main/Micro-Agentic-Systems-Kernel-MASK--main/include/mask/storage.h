#ifndef MASK_STORAGE_H
#define MASK_STORAGE_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file storage.h
 * @brief SQLite-based persistent storage for MASK daemon.
 *
 * Provides a lightweight, zero-dependency embedded SQLite module for
 * persisting telemetry snapshots, system logs, tool call execution outputs,
 * and LLM reasoning cycles across process restarts.
 */

#define MASK_STORAGE_PATH_MAX 512
#define MASK_STORAGE_QUERY_MAX 4096

/**
 * @brief Storage handle for SQLite database.
 */
struct mask_storage;

/**
 * @brief Entry types for storage records.
 */
typedef enum {
    MASK_STORAGE_ENTRY_TELEMETRY = 0,
    MASK_STORAGE_ENTRY_LOG,
    MASK_STORAGE_ENTRY_TOOL_OUTPUT,
    MASK_STORAGE_ENTRY_LLM_REASONING,
    MASK_STORAGE_ENTRY_SYSTEM
} mask_storage_entry_type_t;

/**
 * @brief Opens or creates the SQLite database at the specified path.
 *
 * @param path Filesystem path for the database. If NULL, uses
 *             "mask_storage.db" in the current working directory.
 * @return Storage handle on success, NULL on failure.
 */
struct mask_storage *mask_storage_open(const char *path);

/**
 * @brief Closes the storage handle and flushes all pending writes.
 *
 * @param storage Handle to close. Can be NULL.
 */
void mask_storage_close(struct mask_storage *storage);

/**
 * @brief Stores a telemetry snapshot with associated metadata.
 *
 * @param storage Storage handle.
 * @param timestamp_ms Unix timestamp in milliseconds.
 * @param asset_id Asset identifier (optional, can be NULL).
 * @param payload_json JSON payload to store.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_store_telemetry(struct mask_storage *storage,
                                  uint64_t timestamp_ms,
                                  const char *asset_id,
                                  const char *payload_json);

/**
 * @brief Stores a log entry.
 *
 * @param storage Storage handle.
 * @param timestamp_ms Unix timestamp in milliseconds.
 * @param level Log level (e.g., "INFO", "WARN", "ERROR").
 * @param component Component name (e.g., "llm", "tool", "ipc").
 * @param message Log message text.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_store_log(struct mask_storage *storage,
                            uint64_t timestamp_ms,
                            const char *level,
                            const char *component,
                            const char *message);

/**
 * @brief Stores a tool execution output.
 *
 * @param storage Storage handle.
 * @param timestamp_ms Unix timestamp in milliseconds.
 * @param tool_name Name of the tool that was executed.
 * @param args_json Arguments passed to the tool.
 * @param output_json Output from the tool execution.
 * @param exit_code Exit code from the tool (0-255, or negative for signals).
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_store_tool_output(struct mask_storage *storage,
                                    uint64_t timestamp_ms,
                                    const char *tool_name,
                                    const char *args_json,
                                    const char *output_json,
                                    int exit_code);

/**
 * @brief Stores an LLM reasoning cycle.
 *
 * @param storage Storage handle.
 * @param timestamp_ms Unix timestamp in milliseconds.
 * @param prompt Prompt sent to the LLM.
 * @param response Response received from the LLM.
 * @param tool_called Name of tool called (or NULL if none).
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_store_llm_reasoning(struct mask_storage *storage,
                                      uint64_t timestamp_ms,
                                      const char *prompt,
                                      const char *response,
                                      const char *tool_called);

/**
 * @brief Queries historical records with optional filtering.
 *
 * @param storage Storage handle.
 * @param entry_type Filter by entry type (use -1 for all types).
 * @param limit Maximum number of records to return (0 for no limit).
 * @param output_json Buffer to receive JSON array of results.
 * @param output_size Capacity of output buffer.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_query_history(struct mask_storage *storage,
                                int entry_type,
                                int limit,
                                char *output_json,
                                size_t output_size);

/**
 * @brief Queries records within a time range.
 *
 * @param storage Storage handle.
 * @param entry_type Filter by entry type (use -1 for all types).
 * @param start_ms Start timestamp in milliseconds (inclusive).
 * @param end_ms End timestamp in milliseconds (inclusive).
 * @param limit Maximum number of records to return.
 * @param output_json Buffer to receive JSON array of results.
 * @param output_size Capacity of output buffer.
 * @return MASK_OK on success, MASK_ERR on failure.
 */
int mask_storage_query_time_range(struct mask_storage *storage,
                                   int entry_type,
                                   uint64_t start_ms,
                                   uint64_t end_ms,
                                   int limit,
                                   char *output_json,
                                   size_t output_size);

/**
 * @brief Prunes old records to keep the database size bounded.
 *
 * @param storage Storage handle.
 * @param max_age_ms Maximum age of records to keep in milliseconds.
 *                  Records older than this will be deleted.
 * @return Number of records deleted, or MASK_ERR on failure.
 */
int mask_storage_prune_old(struct mask_storage *storage, uint64_t max_age_ms);

/**
 * @brief Gets the total number of records in the database.
 *
 * @param storage Storage handle.
 * @return Total record count, or MASK_ERR on failure.
 */
int mask_storage_count(struct mask_storage *storage);

#endif /* MASK_STORAGE_H */
