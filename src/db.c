#include "db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS events ("
    "  id                 INTEGER PRIMARY KEY,"
    "  ts                 INTEGER NOT NULL,"
    "  event_type         TEXT    NOT NULL,"
    "  battery_pct        REAL,"
    "  energy_now_uwh     INTEGER,"
    "  energy_full_uwh    INTEGER,"
    "  energy_design_uwh  INTEGER,"
    "  voltage_uv         INTEGER,"
    "  power_now_uw       INTEGER,"
    "  cycle_count        INTEGER,"
    "  ac_online          INTEGER NOT NULL,"
    "  charge_status      TEXT"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_events_ts   ON events(ts);"
    "CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);";

int db_open_rw(const char *path, sqlite3 **out) {
    int rc = sqlite3_open_v2(path, out,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "batrun: sqlite open %s: %s\n",
                path, *out ? sqlite3_errmsg(*out) : sqlite3_errstr(rc));
        return -1;
    }
    sqlite3_busy_timeout(*out, 2000);
    return 0;
}

int db_open_ro(const char *path, sqlite3 **out) {
    int rc = sqlite3_open_v2(path, out, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "batrun: sqlite open %s: %s\n",
                path, *out ? sqlite3_errmsg(*out) : sqlite3_errstr(rc));
        if (*out) { sqlite3_close(*out); *out = NULL; }
        return -1;
    }
    sqlite3_busy_timeout(*out, 2000);
    return 0;
}

int db_init_schema(sqlite3 *db) {
    char *err = NULL;
    int rc = sqlite3_exec(db, SCHEMA, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "batrun: schema init: %s\n", err ? err : "?");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

static void bind_ll_or_null(sqlite3_stmt *s, int idx, long long v) {
    if (v < 0) sqlite3_bind_null(s, idx);
    else       sqlite3_bind_int64(s, idx, (sqlite3_int64)v);
}

int db_insert_event(sqlite3 *db, time_t ts, const char *event_type,
                    const battery_info *b, const ac_info *a) {
    const char *sql =
        "INSERT INTO events ("
        "  ts, event_type, battery_pct,"
        "  energy_now_uwh, energy_full_uwh, energy_design_uwh,"
        "  voltage_uv, power_now_uw, cycle_count,"
        "  ac_online, charge_status"
        ") VALUES (?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "batrun: prepare insert: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)ts);
    sqlite3_bind_text (st, 2, event_type, -1, SQLITE_STATIC);

    if (b->present && b->battery_pct >= 0)
        sqlite3_bind_double(st, 3, b->battery_pct);
    else
        sqlite3_bind_null(st, 3);

    bind_ll_or_null(st, 4, b->present ? b->energy_now_uwh    : -1);
    bind_ll_or_null(st, 5, b->present ? b->energy_full_uwh   : -1);
    bind_ll_or_null(st, 6, b->present ? b->energy_design_uwh : -1);
    bind_ll_or_null(st, 7, b->present ? b->voltage_uv        : -1);
    bind_ll_or_null(st, 8, b->present ? b->power_now_uw      : -1);
    bind_ll_or_null(st, 9, b->present ? b->cycle_count       : -1);

    sqlite3_bind_int(st, 10, a->online);

    if (b->present && b->status[0])
        sqlite3_bind_text(st, 11, b->status, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 11);

    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "batrun: insert step: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

void db_close(sqlite3 *db) {
    sqlite3_close(db);
}
