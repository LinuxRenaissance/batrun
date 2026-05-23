#include "cmd_report.h"
#include "common.h"
#include "db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_WINDOW_DAYS 30

typedef struct {
    long long total_drain_uwh;     /* sum across measurable segments */
    long long total_seconds;       /* sum across measurable segments */
    int       segment_count;       /* total structural segments */
    int       measurable_count;    /* of those, with positive drain */
} drain_stats;

typedef struct {
    long long energy_full_uwh;
    long long energy_design_uwh;
    int       cycle_count;
    int       have_data;
} battery_snapshot;

static const char SQL_ACTIVE_DRAIN[] =
    "WITH e AS ("
    "  SELECT ts, event_type, ac_online, energy_now_uwh,"
    "         LAG(ts)             OVER w AS prev_ts,"
    "         LAG(event_type)     OVER w AS prev_evt,"
    "         LAG(ac_online)      OVER w AS prev_ac,"
    "         LAG(energy_now_uwh) OVER w AS prev_en"
    "  FROM events"
    "  WHERE ts >= ?1 AND ts < ?2"
    "  WINDOW w AS (ORDER BY ts, id)"
    ")"
    "SELECT"
    "  COALESCE(SUM(CASE WHEN prev_en > energy_now_uwh"
    "                    THEN prev_en - energy_now_uwh ELSE 0 END), 0),"
    "  COALESCE(SUM(CASE WHEN prev_en > energy_now_uwh"
    "                    THEN ts - prev_ts ELSE 0 END), 0),"
    "  COUNT(*),"
    "  COALESCE(SUM(CASE WHEN prev_en > energy_now_uwh THEN 1 ELSE 0 END), 0)"
    "  FROM e"
    " WHERE prev_evt IN ('boot','resume','ac_off')"
    "   AND event_type IN ('sleep','shutdown','ac_on')"
    "   AND prev_ac = 0"
    "   AND prev_en IS NOT NULL"
    "   AND energy_now_uwh IS NOT NULL"
    "   AND ts > prev_ts;";


static const char SQL_LATEST_SNAPSHOT[] =
    "SELECT energy_full_uwh, energy_design_uwh, cycle_count"
    "  FROM events"
    " WHERE energy_full_uwh IS NOT NULL"
    " ORDER BY ts DESC, id DESC"
    " LIMIT 1;";

static const char SQL_EVENT_COUNT[] =
    "SELECT COUNT(*) FROM events WHERE ts >= ?1 AND ts < ?2;";

static int query_drain(sqlite3 *db, const char *sql,
                       time_t from, time_t to, drain_stats *out) {
    memset(out, 0, sizeof *out);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        fprintf(stderr, "batrun: prepare: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)from);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)to);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out->total_drain_uwh  = sqlite3_column_int64(st, 0);
        out->total_seconds    = sqlite3_column_int64(st, 1);
        out->segment_count    = sqlite3_column_int(st, 2);
        out->measurable_count = sqlite3_column_int(st, 3);
    }
    sqlite3_finalize(st);
    return 0;
}

static int query_snapshot(sqlite3 *db, battery_snapshot *out) {
    memset(out, 0, sizeof *out);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_LATEST_SNAPSHOT, -1, &st, NULL) != SQLITE_OK)
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        out->energy_full_uwh   = sqlite3_column_int64(st, 0);
        if (sqlite3_column_type(st, 1) != SQLITE_NULL)
            out->energy_design_uwh = sqlite3_column_int64(st, 1);
        if (sqlite3_column_type(st, 2) != SQLITE_NULL)
            out->cycle_count = sqlite3_column_int(st, 2);
        out->have_data = 1;
    }
    sqlite3_finalize(st);
    return 0;
}

static int query_event_count(sqlite3 *db, time_t from, time_t to) {
    sqlite3_stmt *st = NULL;
    int count = 0;
    if (sqlite3_prepare_v2(db, SQL_EVENT_COUNT, -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)from);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)to);
    if (sqlite3_step(st) == SQLITE_ROW) count = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return count;
}

static int parse_duration(const char *s, long long *out_secs) {
    char *end;
    long n = strtol(s, &end, 10);
    if (end == s || n <= 0) return -1;
    long mult;
    switch (*end) {
        case 'h': mult = 3600;        break;
        case 'd': mult = 86400;       break;
        case 'w': mult = 86400 * 7;   break;
        case 'm': mult = 86400 * 30;  break;
        case 'y': mult = 86400 * 365; break;
        default: return -1;
    }
    if (*(end + 1) != '\0') return -1;
    *out_secs = (long long)n * mult;
    return 0;
}

static int parse_date(const char *s, time_t *out) {
    struct tm tm; memset(&tm, 0, sizeof tm);
    int y, m, d;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    *out = mktime(&tm);
    return *out == (time_t)-1 ? -1 : 0;
}

static int parse_month(const char *s, time_t *out_start, time_t *out_end) {
    struct tm tm; memset(&tm, 0, sizeof tm);
    int y, m;
    if (sscanf(s, "%d-%d", &y, &m) != 2) return -1;
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = 1;
    tm.tm_isdst = -1;
    *out_start = mktime(&tm);
    tm.tm_mon += 1;
    *out_end = mktime(&tm);
    return (*out_start == (time_t)-1 || *out_end == (time_t)-1) ? -1 : 0;
}

static int parse_year(const char *s, time_t *out_start, time_t *out_end) {
    int y;
    if (sscanf(s, "%d", &y) != 1) return -1;
    struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon  = 0;
    tm.tm_mday = 1;
    tm.tm_isdst = -1;
    *out_start = mktime(&tm);
    tm.tm_year += 1;
    *out_end = mktime(&tm);
    return (*out_start == (time_t)-1 || *out_end == (time_t)-1) ? -1 : 0;
}

static void fmt_hm(double seconds, char *out, size_t outsz) {
    if (seconds < 0 || seconds != seconds) { snprintf(out, outsz, "-"); return; }
    long total = (long)seconds;
    long h = total / 3600;
    long m = (total % 3600) / 60;
    snprintf(out, outsz, "%ldh %02ldm", h, m);
}

static void fmt_iso(time_t t, char *out, size_t outsz) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, outsz, "%Y-%m-%d %H:%M", &tm);
}

static void print_usage(FILE *out) {
    fputs(
        "Usage: batrun report [options]\n"
        "  --last <N{h,d,w,m,y}>   window ending now (e.g. 7d, 2w, 6m, 1y)\n"
        "  --since YYYY-MM-DD      window from given date to now\n"
        "  --month YYYY-MM         calendar month\n"
        "  --year  YYYY            calendar year\n"
        "  --all                   all recorded data\n"
        "  (default: --last 30d)\n",
        out);
}

int cmd_report(int argc, char **argv) {
    time_t now = time(NULL);
    time_t from = now - (time_t)DEFAULT_WINDOW_DAYS * 86400;
    time_t to   = now;
    char   label[128];
    snprintf(label, sizeof label, "last %d days", DEFAULT_WINDOW_DAYS);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((strcmp(a, "--last") == 0) && i + 1 < argc) {
            long long secs;
            if (parse_duration(argv[++i], &secs) != 0) {
                fprintf(stderr, "batrun report: bad duration: %s\n", argv[i]);
                return 2;
            }
            from = now - (time_t)secs;
            to   = now;
            snprintf(label, sizeof label, "last %s", argv[i]);
        } else if ((strcmp(a, "--since") == 0) && i + 1 < argc) {
            if (parse_date(argv[++i], &from) != 0) {
                fprintf(stderr, "batrun report: bad date: %s\n", argv[i]);
                return 2;
            }
            to = now;
            snprintf(label, sizeof label, "since %s", argv[i]);
        } else if ((strcmp(a, "--month") == 0) && i + 1 < argc) {
            if (parse_month(argv[++i], &from, &to) != 0) {
                fprintf(stderr, "batrun report: bad month: %s (want YYYY-MM)\n", argv[i]);
                return 2;
            }
            snprintf(label, sizeof label, "month %s", argv[i]);
        } else if ((strcmp(a, "--year") == 0) && i + 1 < argc) {
            if (parse_year(argv[++i], &from, &to) != 0) {
                fprintf(stderr, "batrun report: bad year: %s\n", argv[i]);
                return 2;
            }
            snprintf(label, sizeof label, "year %s", argv[i]);
        } else if (strcmp(a, "--all") == 0) {
            from = 0;
            to   = now;
            snprintf(label, sizeof label, "all data");
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            print_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "batrun report: unknown arg: %s\n", a);
            print_usage(stderr);
            return 2;
        }
    }

    const char *db_path = getenv("BATRUN_DB");
    if (!db_path || !*db_path) db_path = DB_PATH;

    sqlite3 *db = NULL;
    if (db_open_ro(db_path, &db) != 0) return 1;

    drain_stats active;
    if (query_drain(db, SQL_ACTIVE_DRAIN, from, to, &active) != 0) {
        db_close(db);
        return 1;
    }
    battery_snapshot snap;
    query_snapshot(db, &snap);
    int total_events = query_event_count(db, from, to);
    db_close(db);

    char from_s[32], to_s[32];
    fmt_iso(from, from_s, sizeof from_s);
    fmt_iso(to,   to_s,   sizeof to_s);

    printf("batrun report -- %s\n", label);
    printf("================================================\n");
    printf("Window:           %s -> %s\n", from_s, to_s);
    printf("Events in window: %d total\n", total_events);

    if (snap.have_data) {
        double full_wh   = snap.energy_full_uwh   / 1e6;
        double design_wh = snap.energy_design_uwh / 1e6;
        printf("Battery health:   %.2f Wh / %.2f Wh design",
               full_wh, design_wh);
        if (snap.energy_design_uwh > 0) {
            printf(" (%.1f%%)", 100.0 * snap.energy_full_uwh / snap.energy_design_uwh);
        }
        putchar('\n');
        if (snap.cycle_count >= 0) printf("Cycle count:      %d\n", snap.cycle_count);
    } else {
        printf("Battery health:   no data yet\n");
    }

    putchar('\n');
    printf("Active use on battery\n");
    if (active.segment_count > 0) {
        if (active.measurable_count < active.segment_count) {
            printf("  Segments observed:    %d (%d with measurable drain)\n",
                   active.segment_count, active.measurable_count);
        } else {
            printf("  Segments observed:    %d\n", active.segment_count);
        }
    }
    if (active.measurable_count > 0 && active.total_seconds > 0) {
        char dur[32];
        fmt_hm((double)active.total_seconds, dur, sizeof dur);
        double drain_wh = active.total_drain_uwh / 1e6;
        double avg_w    = active.total_drain_uwh /
                          ((double)active.total_seconds * 1e6) * 3600.0;
        printf("  Awake-on-battery:     %s observed\n", dur);
        printf("  Total drained:        %.2f Wh\n", drain_wh);
        printf("  Average draw:         %.2f W\n", avg_w);
        if (snap.have_data && snap.energy_full_uwh > 0) {
            double secs_at_full =
                (double)snap.energy_full_uwh /
                ((double)active.total_drain_uwh / (double)active.total_seconds);
            char proj[32];
            fmt_hm(secs_at_full, proj, sizeof proj);
            printf("  Projected at 100%%:    %s   <-- batrun estimate\n", proj);
        }
    } else if (active.segment_count > 0) {
        printf("  (drain below battery's reporting resolution -- "
               "need longer awake-on-battery segments)\n");
    } else {
        printf("  (no awake-on-battery segments in window yet)\n");
    }

    if (active.measurable_count < 5) {
        putchar('\n');
        printf("Note: only %d awake-on-battery segment(s) with measurable "
               "drain -- estimates will sharpen as data accumulates.\n",
               active.measurable_count);
    }
    return 0;
}
