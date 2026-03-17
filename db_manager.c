#include "sqlite3.h"   // ✅ 正确！引用项目本地的头文件
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "db_manager.h"
#include "global_vars.h"

sqlite3* g_db = NULL;

int db_init() {
    int rc = sqlite3_open("climbing.db", &g_db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user TEXT NOT NULL,"
        "file_index INTEGER,"
        "floor_delta INTEGER,"
        "timestamp INTEGER"
        ");";
    
    char* err_msg = NULL;
    rc = sqlite3_exec(g_db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    }
    
    printf("[DB] Database initialized.\n");
    return 0;
}

int db_insert_record(const char* user, int file_index, int floor_delta, time_t timestamp) {
    if (!g_db) return -1;
    
    const char* sql = 
        "INSERT INTO records (user, file_index, floor_delta, timestamp) VALUES (?, ?, ?, ?);";
    
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, user, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, file_index);
    sqlite3_bind_int(stmt, 3, floor_delta);
    sqlite3_bind_int64(stmt, 4, timestamp);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    
    return 0;
}

void db_close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
        printf("[DB] Database closed.\n");
    }
}