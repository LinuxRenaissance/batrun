#ifndef BATRUN_POWER_SUPPLY_H
#define BATRUN_POWER_SUPPLY_H

typedef struct {
    int       present;            /* 0 = no battery found */
    double    battery_pct;        /* 0..100, or -1 if unknown */
    long long energy_now_uwh;     /* -1 if unknown */
    long long energy_full_uwh;
    long long energy_design_uwh;
    long long voltage_uv;
    long long power_now_uw;
    int       cycle_count;        /* -1 if unknown */
    char      status[32];         /* "Discharging" etc, empty if unknown */
} battery_info;

typedef struct {
    int present;                  /* 0 = no AC adapter found */
    int online;                   /* 0/1 */
} ac_info;

void read_battery(battery_info *b);
void read_ac(ac_info *a);

#endif
