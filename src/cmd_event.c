#include "cmd_event.h"
#include "common.h"
#include "db.h"
#include "power_supply.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int valid_type(const char *t) {
    static const char *const types[] = {
        "boot", "shutdown", "sleep", "resume", "ac_on", "ac_off", NULL
    };
    for (int i = 0; types[i]; i++)
        if (strcmp(t, types[i]) == 0) return 1;
    return 0;
}

int cmd_event(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: batrun event <boot|shutdown|sleep|resume|ac_on|ac_off>\n");
        return 2;
    }
    const char *type = argv[1];
    if (!valid_type(type)) {
        fprintf(stderr, "batrun: invalid event type: %s\n", type);
        return 2;
    }

    const char *db_path = getenv("BATRUN_DB");
    if (!db_path || !*db_path) {
        db_path = DB_PATH;
        mkdir(DB_DIR, 0755);
    }

    /* World-readable DB file when first created. */
    mode_t prev_umask = umask(0022);

    battery_info b;
    ac_info      a;
    read_battery(&b);
    read_ac(&a);

    /* The kernel battery driver returns a stale energy_now after resume.
       Poll until the value has been unchanged for STABLE_SECS consecutive
       reads or MAX_POLL_SECS elapses, whichever comes first. */
    if (strcmp(type, "resume") == 0) {
        if (b.energy_now_uwh >= 0) {
#define MAX_POLL_SECS   30
#define STABLE_SECS      5
            long long last = b.energy_now_uwh;
            int stable = 0;
            for (int i = 0; i < MAX_POLL_SECS && stable < STABLE_SECS; i++) {
                sleep(1);
                read_battery(&b);
                if (b.energy_now_uwh == last)
                    stable++;
                else {
                    stable = 0;
                    last = b.energy_now_uwh;
                }
            }
#undef MAX_POLL_SECS
#undef STABLE_SECS
        }
        read_ac(&a);
    }

    sqlite3 *db = NULL;
    int rc = 1;
    if (db_open_rw(db_path, &db) == 0) {
        if (db_init_schema(db) == 0 &&
            db_insert_event(db, time(NULL), type, &b, &a) == 0) {
            rc = 0;
        }
        db_close(db);
    }

    chmod(db_path, 0644);
    umask(prev_umask);
    return rc;
}
