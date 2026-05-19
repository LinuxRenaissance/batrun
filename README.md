# batrun

**battery intelligence, zero batshit**

batrun tells you how many hours your laptop actually lasts on a single
charge, based on measurements from your own usage — not the firmware's
optimistic guess at full charge.

It records battery state on sleep, resume, boot, shutdown and AC
plug/unplug into a local SQLite database, then computes the average
discharge rate across only the *awake-on-battery* segments of your
day. Charging periods are excluded. Suspend drain is tracked
separately so you can see how much standby costs you.

## How it works

batrun is event-driven — nothing runs between events. Three hooks
feed a small C recorder:

| Event              | Mechanism                                            |
|--------------------|------------------------------------------------------|
| sleep / resume     | `/usr/lib/systemd/system-sleep/batrun`               |
| boot / shutdown    | `batrun.service` (oneshot, `ExecStart` / `ExecStop`) |
| AC plug / unplug   | udev rule on `power_supply` subsystem (`type=Mains`) |

Each hook invokes `batrun event <type>`, which reads
`/sys/class/power_supply/` and inserts one row into
`/var/lib/batrun/batrun.db`. No daemon, no polling.

## Install

Build the Debian package from source and install it:

```sh
sudo apt install build-essential libsqlite3-dev debhelper
dpkg-buildpackage -b -us -uc
sudo dpkg -i ../batrun_*.deb
```

On install, the systemd service is enabled and started immediately,
which records the first `boot` event. From then on the hooks
accumulate data automatically. No configuration needed.

To uninstall:

```sh
sudo apt remove batrun         # keep the database
sudo apt purge  batrun         # also wipe /var/lib/batrun
```

## Usage

### `batrun status`

Current battery state and two estimates of remaining time:

* **Instant** — divides current energy by `power_now` (the kernel's
  instantaneous draw). Accurate to the present moment, swings with
  load.
* **Historical** — uses the average draw across all
  awake-on-battery segments from the last 30 days. Reflects how you
  actually use the laptop, not what it's doing right this second.

```
$ batrun status
batrun status -- 2026-05-19 17:01
================================================
Battery:          59%  (34.89 Wh / 58.79 Wh)
Status:           Discharging
Health:           103.1% of design (57.00 Wh design)
Cycle count:      0
AC adapter:       not connected

Right now
  Instant draw:         7.89 W
  Remaining (instant):  4h 25m

Historical (last 30 days)
  Awake-on-battery segments: 1
  Average draw:              8.14 W
  Projected at 100%:         7h 13m
  Remaining at avg:          4h 17m   <-- batrun estimate
```

### `batrun report`

Aggregate report over a time window. Default is `--last 30d`.

```sh
batrun report                       # last 30 days
batrun report --last 7d             # last week
batrun report --last 6m             # last six months
batrun report --since 2026-01-01    # since a specific date
batrun report --month 2026-05       # calendar month
batrun report --year  2025          # calendar year
batrun report --all                 # everything in the DB
```

Duration units for `--last`: `h` hours, `d` days, `w` weeks,
`m` months (= 30 days), `y` years (= 365 days).

The report shows:

* **Active use on battery** — total awake-on-battery time, energy
  drained, average draw, and projected runtime at full charge.
* **Standby (suspend-to-RAM)** — total suspend time, energy drained,
  projected days of standby at full charge.
* **Battery health** — current full charge vs design capacity, and
  the most recent cycle count.

Early estimates will be noisy. They sharpen as data accumulates —
expect useful projections after roughly a week of normal use.

### `batrun event <type>`

The low-level recorder invoked by the hooks. You don't normally
run this yourself.

Types: `boot`, `shutdown`, `sleep`, `resume`, `ac_on`, `ac_off`.

Writes to `/var/lib/batrun/batrun.db` (requires root). Honors
`BATRUN_DB=/path/to/test.db` for testing against an alternate
database without touching the system DB.

## Data

* **Database**: `/var/lib/batrun/batrun.db` — owned by root,
  world-readable. `batrun status` and `batrun report` open it
  read-only, so any user on the system can run them.
* **Schema**: a single `events` table; sessions and averages are
  derived at query time rather than precomputed, so logic changes
  don't require migrations.

Inspect the raw events directly with `sqlite3`:

```sh
sqlite3 /var/lib/batrun/batrun.db \
  "SELECT datetime(ts,'unixepoch','localtime'), event_type,
          battery_pct, energy_now_uwh, ac_online
     FROM events ORDER BY id;"
```

## Methodology

A *session* of battery use is bounded by AC events: it starts when
the charger is unplugged (or the laptop boots on battery) and ends
when the charger is plugged in (or the laptop shuts down).

Within a session, batrun looks at consecutive events of these shapes
to extract clean drain measurements:

* `boot | resume | ac_off` → `sleep | shutdown | ac_on` — an
  **awake-on-battery segment**, used to compute active-use drain rate
* `sleep` → `resume` — a **standby segment**, used to compute
  suspend drain rate

Both endpoints must have `ac_online = 0` and a positive drain.
Segments that span an AC event are discarded. Charging is never
counted toward runtime estimates.

The projected runtime at 100% is computed using the *most recent*
observed `energy_full` value, which tracks battery degradation over
time. Historical drain rates are not rescaled to design capacity —
they reflect the battery as it is today.

## Requirements

* Linux with `/sys/class/power_supply/` exposing `energy_*`
  (preferred) or `charge_*` attributes
* systemd (for the sleep hook and the boot/shutdown service)
* udev (for AC plug/unplug detection)
* SQLite ≥ 3.25 (for window functions used by `report`)

These are present on every modern desktop Linux distribution.
Tested on Debian 13.

## License

BSD 2-Clause — see [`LICENSE`](LICENSE).

Copyright © 2026 Jerko Čilaš (Linux Renaissance)
