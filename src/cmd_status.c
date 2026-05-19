#include "cmd_status.h"
#include "common.h"
#include "db.h"
#include "power_supply.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STATUS_HISTORY_DAYS 30

static const char SQL_HIST_RATE[] =
    "WITH e AS ("
    "  SELECT ts, event_type, ac_online, energy_now_uwh,"
    "         LAG(ts)             OVER w AS prev_ts,"
    "         LAG(event_type)     OVER w AS prev_evt,"
    "         LAG(ac_online)      OVER w AS prev_ac,"
    "         LAG(energy_now_uwh) OVER w AS prev_en"
    "  FROM events"
    "  WHERE ts >= ?1"
    "  WINDOW w AS (ORDER BY ts, id)"
    ")"
    "SELECT COALESCE(SUM(prev_en - energy_now_uwh), 0),"
    "       COALESCE(SUM(ts - prev_ts), 0),"
    "       COUNT(*)"
    "  FROM e"
    " WHERE prev_evt IN ('boot','resume','ac_off')"
    "   AND event_type IN ('sleep','shutdown','ac_on')"
    "   AND prev_ac = 0"
    "   AND prev_en IS NOT NULL"
    "   AND energy_now_uwh IS NOT NULL"
    "   AND prev_en > energy_now_uwh"
    "   AND ts > prev_ts;";

static void fmt_hm(double seconds, char *out, size_t outsz) {
    if (seconds < 0 || seconds != seconds) { snprintf(out, outsz, "-"); return; }
    long total = (long)seconds;
    long h = total / 3600;
    long m = (total % 3600) / 60;
    snprintf(out, outsz, "%ldh %02ldm", h, m);
}

int cmd_status(int argc, char **argv) {
    (void)argc; (void)argv;

    battery_info b;
    ac_info      a;
    read_battery(&b);
    read_ac(&a);

    time_t now = time(NULL);
    struct tm tm; localtime_r(&now, &tm);
    char ts_s[32];
    strftime(ts_s, sizeof ts_s, "%Y-%m-%d %H:%M", &tm);

    printf("batrun status -- %s\n", ts_s);
    printf("================================================\n");

    if (!b.present) {
        printf("Battery:          not detected\n");
    } else {
        printf("Battery:          %.0f%%", b.battery_pct);
        if (b.energy_now_uwh >= 0 && b.energy_full_uwh > 0) {
            printf("  (%.2f Wh / %.2f Wh)",
                   b.energy_now_uwh / 1e6, b.energy_full_uwh / 1e6);
        }
        putchar('\n');
        if (b.status[0]) printf("Status:           %s\n", b.status);
        if (b.energy_design_uwh > 0 && b.energy_full_uwh > 0) {
            printf("Health:           %.1f%% of design (%.2f Wh design)\n",
                   100.0 * b.energy_full_uwh / b.energy_design_uwh,
                   b.energy_design_uwh / 1e6);
        }
        if (b.cycle_count >= 0) printf("Cycle count:      %d\n", b.cycle_count);
    }
    printf("AC adapter:       %s\n",
           a.present ? (a.online ? "connected" : "not connected")
                     : "not detected");

    /* Instantaneous estimate from power_now if discharging. */
    if (b.present && !a.online && b.power_now_uw > 0 && b.energy_now_uwh > 0) {
        double secs_now = (double)b.energy_now_uwh / (double)b.power_now_uw * 3600.0;
        char dur[32]; fmt_hm(secs_now, dur, sizeof dur);
        printf("\nRight now\n");
        printf("  Instant draw:         %.2f W\n", b.power_now_uw / 1e6);
        printf("  Remaining (instant):  %s\n", dur);
    }

    /* Historical average from DB. */
    const char *db_path = getenv("BATRUN_DB");
    if (!db_path || !*db_path) db_path = DB_PATH;

    sqlite3 *db = NULL;
    if (db_open_ro(db_path, &db) != 0) {
        printf("\n(no history DB at %s yet)\n", db_path);
        return 0;
    }

    sqlite3_stmt *st = NULL;
    long long total_drain = 0, total_secs = 0;
    int segments = 0;
    if (sqlite3_prepare_v2(db, SQL_HIST_RATE, -1, &st, NULL) == SQLITE_OK) {
        time_t cutoff = now - (time_t)STATUS_HISTORY_DAYS * 86400;
        sqlite3_bind_int64(st, 1, (sqlite3_int64)cutoff);
        if (sqlite3_step(st) == SQLITE_ROW) {
            total_drain = sqlite3_column_int64(st, 0);
            total_secs  = sqlite3_column_int64(st, 1);
            segments    = sqlite3_column_int(st, 2);
        }
        sqlite3_finalize(st);
    }
    db_close(db);

    printf("\nHistorical (last %d days)\n", STATUS_HISTORY_DAYS);
    printf("  Awake-on-battery segments: %d\n", segments);
    if (segments > 0 && total_secs > 0 && total_drain > 0) {
        double avg_w = (double)total_drain / ((double)total_secs * 1e6) * 3600.0;
        printf("  Average draw:              %.2f W\n", avg_w);
        if (b.present && b.energy_full_uwh > 0) {
            double rate = (double)total_drain / (double)total_secs; /* uW */
            double secs_full = (double)b.energy_full_uwh / rate;
            char proj[32]; fmt_hm(secs_full, proj, sizeof proj);
            printf("  Projected at 100%%:         %s\n", proj);
            if (!a.online && b.energy_now_uwh > 0) {
                double secs_now_hist = (double)b.energy_now_uwh / rate;
                char rem[32]; fmt_hm(secs_now_hist, rem, sizeof rem);
                printf("  Remaining at avg:          %s   <-- batrun estimate\n", rem);
            }
        }
    } else {
        printf("  (not enough history yet -- need at least one full "
               "boot/resume -> sleep/shutdown cycle on battery)\n");
    }
    return 0;
}
