#include "power_supply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define PSU_ROOT "/sys/class/power_supply"

static int read_file_str(const char *path, char *buf, size_t bufsz) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)bufsz, f)) { fclose(f); return -1; }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ' || buf[n-1] == '\t'))
        buf[--n] = '\0';
    return 0;
}

static long long read_file_ll(const char *path, long long fallback) {
    char buf[64];
    if (read_file_str(path, buf, sizeof buf) != 0) return fallback;
    char *end = NULL;
    long long v = strtoll(buf, &end, 10);
    if (end == buf) return fallback;
    return v;
}

static int psu_type_eq(const char *name, const char *want) {
    char p[512], t[32];
    snprintf(p, sizeof p, PSU_ROOT "/%s/type", name);
    if (read_file_str(p, t, sizeof t) != 0) return 0;
    return strcmp(t, want) == 0;
}

void read_battery(battery_info *b) {
    memset(b, 0, sizeof *b);
    b->battery_pct       = -1.0;
    b->energy_now_uwh    = -1;
    b->energy_full_uwh   = -1;
    b->energy_design_uwh = -1;
    b->voltage_uv        = -1;
    b->power_now_uw      = -1;
    b->cycle_count       = -1;

    DIR *d = opendir(PSU_ROOT);
    if (!d) return;
    struct dirent *e;
    char chosen[64] = {0};
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (!psu_type_eq(e->d_name, "Battery")) continue;
        /* Prefer BAT* names if multiple batteries appear. */
        if (chosen[0] == 0 || strncmp(e->d_name, "BAT", 3) == 0) {
            snprintf(chosen, sizeof chosen, "%.*s",
                     (int)(sizeof chosen - 1), e->d_name);
        }
    }
    closedir(d);
    if (chosen[0] == 0) return;

    char p[512];

    snprintf(p, sizeof p, PSU_ROOT "/%s/voltage_now", chosen);
    b->voltage_uv = read_file_ll(p, -1);
    snprintf(p, sizeof p, PSU_ROOT "/%s/power_now", chosen);
    b->power_now_uw = read_file_ll(p, -1);
    snprintf(p, sizeof p, PSU_ROOT "/%s/capacity", chosen);
    long long cap = read_file_ll(p, -1);
    b->battery_pct = cap >= 0 ? (double)cap : -1.0;
    snprintf(p, sizeof p, PSU_ROOT "/%s/cycle_count", chosen);
    b->cycle_count = (int)read_file_ll(p, -1);
    snprintf(p, sizeof p, PSU_ROOT "/%s/status", chosen);
    read_file_str(p, b->status, sizeof b->status);

    snprintf(p, sizeof p, PSU_ROOT "/%s/energy_now", chosen);
    b->energy_now_uwh = read_file_ll(p, -1);
    snprintf(p, sizeof p, PSU_ROOT "/%s/energy_full", chosen);
    b->energy_full_uwh = read_file_ll(p, -1);
    snprintf(p, sizeof p, PSU_ROOT "/%s/energy_full_design", chosen);
    b->energy_design_uwh = read_file_ll(p, -1);

    /* Some batteries expose charge_* (uAh) instead of energy_* (uWh). */
    if (b->energy_now_uwh < 0 && b->voltage_uv > 0) {
        snprintf(p, sizeof p, PSU_ROOT "/%s/charge_now", chosen);
        long long q = read_file_ll(p, -1);
        if (q >= 0) b->energy_now_uwh = (q * (b->voltage_uv / 1000)) / 1000;
    }
    if (b->energy_full_uwh < 0 && b->voltage_uv > 0) {
        snprintf(p, sizeof p, PSU_ROOT "/%s/charge_full", chosen);
        long long q = read_file_ll(p, -1);
        if (q >= 0) b->energy_full_uwh = (q * (b->voltage_uv / 1000)) / 1000;
    }
    if (b->energy_design_uwh < 0 && b->voltage_uv > 0) {
        snprintf(p, sizeof p, PSU_ROOT "/%s/charge_full_design", chosen);
        long long q = read_file_ll(p, -1);
        if (q >= 0) b->energy_design_uwh = (q * (b->voltage_uv / 1000)) / 1000;
    }

    b->present = 1;
}

void read_ac(ac_info *a) {
    memset(a, 0, sizeof *a);
    DIR *d = opendir(PSU_ROOT);
    if (!d) return;
    struct dirent *e;
    char chosen[64] = {0};
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (psu_type_eq(e->d_name, "Mains")) {
            snprintf(chosen, sizeof chosen, "%.*s",
                     (int)(sizeof chosen - 1), e->d_name);
            break;
        }
    }
    closedir(d);
    if (chosen[0] == 0) return;
    char p[512];
    snprintf(p, sizeof p, PSU_ROOT "/%s/online", chosen);
    a->online  = read_file_ll(p, 0) ? 1 : 0;
    a->present = 1;
}
