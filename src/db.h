#ifndef BATRUN_DB_H
#define BATRUN_DB_H

#include <time.h>
#include "power_supply.h"

typedef struct sqlite3 sqlite3;

int  db_open_rw(const char *path, sqlite3 **out);
int  db_open_ro(const char *path, sqlite3 **out);
int  db_init_schema(sqlite3 *db);
int  db_insert_event(sqlite3 *db, time_t ts, const char *event_type,
                     const battery_info *b, const ac_info *a);
void db_close(sqlite3 *db);

#endif
