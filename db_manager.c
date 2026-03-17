#include "sqlite3.h"
#include "db_manager.h"
#include "cjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static sqlite3* g_db = NULL;

static int exec_sql(const char* sql) {
    char* err_msg = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] SQL error: %s\n", err_msg ? err_msg : "unknown");
        if (err_msg) {
            sqlite3_free(err_msg);
        }
    }
    return rc;
}

int init_database(void) {
    int rc = sqlite3_open("climbing.db", &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[DB] Can't open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    const char* sql_users =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY,"
        "username TEXT NOT NULL,"
        "role TEXT NOT NULL DEFAULT 'user',"
        "nickname TEXT"
        ");";

    const char* sql_devices =
        "CREATE TABLE IF NOT EXISTS devices ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device_id TEXT NOT NULL UNIQUE,"
        "user_id INTEGER,"
        "last_seen INTEGER"
        ");";

    const char* sql_raw =
        "CREATE TABLE IF NOT EXISTS raw_samples ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "device_id TEXT NOT NULL,"
        "gx REAL, gy REAL, gz REAL,"
        "ax REAL, ay REAL, az REAL,"
        "created_at INTEGER NOT NULL"
        ");";

    const char* sql_results =
        "CREATE TABLE IF NOT EXISTS analysis_results ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "device_id TEXT NOT NULL,"
        "current_floor INTEGER NOT NULL,"
        "total_steps INTEGER NOT NULL,"
        "speed REAL NOT NULL,"
        "created_at INTEGER NOT NULL"
        ");";

    const char* sql_history =
        "CREATE TABLE IF NOT EXISTS history_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id INTEGER NOT NULL,"
        "current_floor INTEGER NOT NULL,"
        "total_steps INTEGER NOT NULL,"
        "speed REAL NOT NULL,"
        "created_at INTEGER NOT NULL"
        ");";

    const char* sql_idx1 = "CREATE INDEX IF NOT EXISTS idx_history_user_time ON history_records(user_id, created_at DESC);";
    const char* sql_idx2 = "CREATE INDEX IF NOT EXISTS idx_results_user_time ON analysis_results(user_id, created_at DESC);";

    if (exec_sql(sql_users) != SQLITE_OK ||
        exec_sql(sql_devices) != SQLITE_OK ||
        exec_sql(sql_raw) != SQLITE_OK ||
        exec_sql(sql_results) != SQLITE_OK ||
        exec_sql(sql_history) != SQLITE_OK ||
        exec_sql(sql_idx1) != SQLITE_OK ||
        exec_sql(sql_idx2) != SQLITE_OK) {
        return -1;
    }

    printf("[DB] Database initialized.\n");
    return 0;
}

void cleanup_database(void) {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
        printf("[DB] Database closed.\n");
    }
}

bool save_progress(int user_id, int floor, int steps, float speed) {
    if (!g_db) {
        return false;
    }

    time_t now = time(NULL);

    const char* sql =
        "INSERT INTO history_records(user_id, current_floor, total_steps, speed, created_at) "
        "VALUES(?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, floor);
    sqlite3_bind_int(stmt, 3, steps);
    sqlite3_bind_double(stmt, 4, speed);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return false;
    }

    const char* sql_result =
        "INSERT INTO analysis_results(user_id, device_id, current_floor, total_steps, speed, created_at) "
        "VALUES(?, 'DEV_000', ?, ?, ?, ?);";

    stmt = NULL;
    rc = sqlite3_prepare_v2(g_db, sql_result, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, floor);
    sqlite3_bind_int(stmt, 3, steps);
    sqlite3_bind_double(stmt, 4, speed);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool load_progress(int user_id, int* out_floor, int* out_steps, float* out_speed) {
    if (!g_db) {
        return false;
    }

    const char* sql =
        "SELECT current_floor, total_steps, speed FROM history_records "
        "WHERE user_id = ? ORDER BY created_at DESC LIMIT 1;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_int(stmt, 1, user_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (out_floor) {
            *out_floor = sqlite3_column_int(stmt, 0);
        }
        if (out_steps) {
            *out_steps = sqlite3_column_int(stmt, 1);
        }
        if (out_speed) {
            *out_speed = (float)sqlite3_column_double(stmt, 2);
        }
        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}

char* get_recent_history_json(int limit) {
    if (!g_db) {
        char* empty = (char*)malloc(3);
        if (empty) {
            strcpy(empty, "[]");
        }
        return empty;
    }

    if (limit <= 0) {
        limit = 10;
    }

    const char* sql =
        "SELECT created_at, current_floor, total_steps, speed "
        "FROM history_records ORDER BY created_at DESC LIMIT ?;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char* empty = (char*)malloc(3);
        if (empty) {
            strcpy(empty, "[]");
        }
        return empty;
    }

    sqlite3_bind_int(stmt, 1, limit);

    cJSON* arr = cJSON_CreateArray();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        time_t ts = (time_t)sqlite3_column_int64(stmt, 0);
        int floor = sqlite3_column_int(stmt, 1);
        int steps = sqlite3_column_int(stmt, 2);
        double speed = sqlite3_column_double(stmt, 3);

        char buf[64] = {0};
        struct tm* tm_info = localtime(&ts);
        if (tm_info) {
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
        }

        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "time", buf[0] ? buf : "N/A");
        cJSON_AddNumberToObject(obj, "floor", floor);
        cJSON_AddNumberToObject(obj, "step", steps);
        cJSON_AddNumberToObject(obj, "speed", speed);
        cJSON_AddItemToArray(arr, obj);
    }

    sqlite3_finalize(stmt);

    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return json;
}

int db_init(void) {
    return init_database();
}

void db_close(void) {
    cleanup_database();
}
