#ifndef SQLITE3_STUB_H
#define SQLITE3_STUB_H

/* This is a stub header for SQLite3.
 * To enable the storage engine, download the SQLite amalgamation from:
 * https://www.sqlite.org/amalgamation.html
 * Extract sqlite3.c and sqlite3.h to third_party/ and rename this file.
 *
 * For testing purposes without SQLite, the storage functions will return MASK_ERR.
 */

#ifdef USE_SQLITE
#include "sqlite3.h"
#else

#define SQLITE_OK 0
#define SQLITE_ERROR 1
#define SQLITE_ROW 100
#define SQLITE_DONE 101

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

static inline int sqlite3_open(const char *filename, sqlite3 **ppDb) { return SQLITE_ERROR; }
static inline int sqlite3_close(sqlite3 *db) { return SQLITE_OK; }
static inline const char *sqlite3_errmsg(sqlite3 *db) { return "SQLite not available"; }
static inline int sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void*,int,char**,char**), void *arg, char **errmsg) { return SQLITE_ERROR; }
static inline void sqlite3_free(void *p) {}
static inline int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail) { return SQLITE_ERROR; }
static inline int sqlite3_step(sqlite3_stmt *pStmt) { return SQLITE_ERROR; }
static inline int sqlite3_finalize(sqlite3_stmt *pStmt) { return SQLITE_OK; }
static inline int sqlite3_column_int(sqlite3_stmt *pStmt, int iCol) { return 0; }
static inline sqlite3_int64 sqlite3_column_int64(sqlite3_stmt *pStmt, int iCol) { return 0; }
static inline const unsigned char *sqlite3_column_text(sqlite3_stmt *pStmt, int iCol) { return NULL; }
static inline int sqlite3_column_int(sqlite3_stmt *pStmt, int iCol) { return 0; }
static inline int sqlite3_changes(sqlite3 *db) { return 0; }

#endif

#endif /* SQLITE3_STUB_H */
