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

    const char* sql_auth =
        "CREATE TABLE IF NOT EXISTS auth_accounts ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL UNIQUE,"
        "password TEXT NOT NULL,"
        "role TEXT NOT NULL CHECK(role IN ('admin','user','monitor')),"
        "nickname TEXT"
        ");";

    const char* sql_drop_auth = "DROP TABLE IF EXISTS auth_accounts;";

    const char* sql_idx1 = "CREATE INDEX IF NOT EXISTS idx_history_user_time ON history_records(user_id, created_at DESC);";
    const char* sql_idx2 = "CREATE INDEX IF NOT EXISTS idx_results_user_time ON analysis_results(user_id, created_at DESC);";

    if (exec_sql(sql_users) != SQLITE_OK ||
        exec_sql(sql_devices) != SQLITE_OK ||
        exec_sql(sql_raw) != SQLITE_OK ||
        exec_sql(sql_results) != SQLITE_OK ||
        exec_sql(sql_history) != SQLITE_OK ||
        exec_sql(sql_drop_auth) != SQLITE_OK ||
        exec_sql(sql_auth) != SQLITE_OK ||
        exec_sql(sql_idx1) != SQLITE_OK ||
        exec_sql(sql_idx2) != SQLITE_OK) {
        return -1;
    }

    const char* seed_sql =
        "INSERT OR IGNORE INTO auth_accounts(username,password,role,nickname) VALUES"
        "('admin','admin123','admin','系统管理员'),"
        "('collector1','collector123','user','采集对象1'),"
        "('collector2','collector123','user','采集对象2'),"
        "('monitor1','monitor123','monitor','监控人员1');";

    if (exec_sql(seed_sql) != SQLITE_OK) {
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

bool verify_login(const char* username, const char* password, AuthAccount* out_account) {
    if (!username || !password || !out_account) {
        return false;
    }

    if (!g_db) {
        if (strcmp(username, "admin") == 0 && strcmp(password, "admin123") == 0) {
            out_account->id = 1;
            snprintf(out_account->username, sizeof(out_account->username), "%s", "admin");
            snprintf(out_account->role, sizeof(out_account->role), "%s", "admin");
            snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "系统管理员");
            return true;
        }
        if (strcmp(username, "monitor1") == 0 && strcmp(password, "monitor123") == 0) {
            out_account->id = 2;
            snprintf(out_account->username, sizeof(out_account->username), "%s", "monitor1");
            snprintf(out_account->role, sizeof(out_account->role), "%s", "monitor");
            snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "监控人员1");
            return true;
        }
        if (strcmp(username, "collector1") == 0 && strcmp(password, "collector123") == 0) {
            out_account->id = 3;
            snprintf(out_account->username, sizeof(out_account->username), "%s", "collector1");
            snprintf(out_account->role, sizeof(out_account->role), "%s", "user");
            snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "采集对象1");
            return true;
        }
        return false;
    }

    const char* sql =
        "SELECT id, username, role, COALESCE(nickname, '') "
        "FROM auth_accounts WHERE username = ? AND password = ? LIMIT 1;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        out_account->id = sqlite3_column_int(stmt, 0);
        const unsigned char* db_user = sqlite3_column_text(stmt, 1);
        const unsigned char* db_role = sqlite3_column_text(stmt, 2);
        const unsigned char* db_nick = sqlite3_column_text(stmt, 3);

        snprintf(out_account->username, sizeof(out_account->username), "%s", db_user ? (const char*)db_user : "");
        snprintf(out_account->role, sizeof(out_account->role), "%s", db_role ? (const char*)db_role : "user");
        snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", db_nick ? (const char*)db_nick : "");

        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);

    if (strcmp(username, "admin") == 0 && strcmp(password, "admin123") == 0) {
        out_account->id = 1;
        snprintf(out_account->username, sizeof(out_account->username), "%s", "admin");
        snprintf(out_account->role, sizeof(out_account->role), "%s", "admin");
        snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "系统管理员");
        return true;
    }
    if (strcmp(username, "monitor1") == 0 && strcmp(password, "monitor123") == 0) {
        out_account->id = 2;
        snprintf(out_account->username, sizeof(out_account->username), "%s", "monitor1");
        snprintf(out_account->role, sizeof(out_account->role), "%s", "monitor");
        snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "监控人员1");
        return true;
    }
    if (strcmp(username, "collector1") == 0 && strcmp(password, "collector123") == 0) {
        out_account->id = 3;
        snprintf(out_account->username, sizeof(out_account->username), "%s", "collector1");
        snprintf(out_account->role, sizeof(out_account->role), "%s", "user");
        snprintf(out_account->nickname, sizeof(out_account->nickname), "%s", "采集对象1");
        return true;
    }

    return false;
}

char* get_collectors_json(void) {
    if (!g_db) {
        char* empty = (char*)malloc(3);
        if (empty) {
            strcpy(empty, "[]");
        }
        return empty;
    }

    const char* sql =
        "SELECT id, username, COALESCE(nickname, '') FROM auth_accounts WHERE role='user' ORDER BY id ASC;";

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        char* empty = (char*)malloc(3);
        if (empty) {
            strcpy(empty, "[]");
        }
        return empty;
    }

    cJSON* arr = cJSON_CreateArray();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "id", sqlite3_column_int(stmt, 0));

        const unsigned char* name = sqlite3_column_text(stmt, 1);
        const unsigned char* nick = sqlite3_column_text(stmt, 2);

        cJSON_AddStringToObject(obj, "username", name ? (const char*)name : "");
        cJSON_AddStringToObject(obj, "nickname", nick ? (const char*)nick : "");
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
