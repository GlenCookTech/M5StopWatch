# Aqua Timer routines

CSV routines for the **Aqua Timer** app on the M5Stack StopWatch. Put these
files in a public location the watch can reach over HTTPS — a GitHub repo is the
easiest — then point the watch at the **folder** URL in Wi-Fi Setup.

For a GitHub repo, the folder URL is the `raw.githubusercontent.com` address of
the directory that holds `index.csv`, e.g.

```
https://raw.githubusercontent.com/<owner>/<repo>/main/routines
```

The watch downloads `index.csv`, then each routine it lists, caches them to flash,
and runs classes fully offline. Re-run **Sync** whenever you change a file.

## Files

- **`index.csv`** — the manifest. One row per routine.
- **`*.csv`** — one file per routine.

## `index.csv` format

```csv
# comments start with #
file,title,minutes
swet_deep_water.csv,S'WET Deep Water,47
```

| column | meaning |
|--------|---------|
| `file` | routine file name in this same folder (no slashes) |
| `title` | name shown in the picker |
| `minutes` | advisory only — the watch shows the real total after parsing |

## Routine format

```csv
# title: S'WET Deep Water
# author: Glen
# version: 1
section,label,type,seconds,reps
Warm-Up,Forward Run,work,45,1
Warm-Up,Flexed-hand push pull,power,15,1
Wall Work,Straddle Jog + boxing arms,work,45,1
Wall Work,Recover,rest,15,1
Wall Work,,repeat,0,2
```

**Metadata** — lines starting with `#`. Recognised keys: `title`, `author`,
`version`. `title` is what the running screen shows; if absent, the file name is used.

**Header row** — exactly `section,label,type,seconds,reps` (the `reps` column is
optional). Must appear once, before any interval rows.

**Interval rows** — one timed step each:

| column | meaning |
|--------|---------|
| `section` | block name, shown dim above the timer (e.g. `Wall Work`) |
| `label` | the move, shown large (e.g. `Straddle Jog + boxing arms`) |
| `type` | `prep`, `work`, `rest`, `power`, or `cooldown` — sets the colour and cue |
| `seconds` | duration, 1–3600 |
| `reps` | optional; repeats this single row back-to-back N times |

**Repeat a whole section** — a row with `type` = `repeat` re-runs the preceding
run of rows that share its `section`, so the section plays `reps` times in total:

```csv
Wall Work,Jog,work,45,1
Wall Work,Recover,rest,15,1
Wall Work,,repeat,0,2      # the Wall Work block above now plays twice
```

## Cues

At every interval change the watch buzzes and beeps, with a distinct pattern per
type (work rises, rest is a single low tone, power is a triple). It also gives a
3-2-1 buzz/beep countdown in the last three seconds of each interval (suppressed
on intervals shorter than 5 s). A 3-second "get ready" lead-in runs before the
first move.

## Rules & gotchas

- **No commas inside a label.** There is no quoting or escaping — a row with the
  wrong number of fields is rejected (and reported), not mangled.
- **Per-side moves**: write two rows (`Kick R`, `Kick L`) rather than one, so the
  "next up" preview stays honest.
- Unknown `type` is treated as `work` with a warning.
- A routine is capped at 500 intervals after `repeat` expansion, and 32 routines
  per index — generous for any real class, but it stops a runaway `reps`.
- `seconds` outside 1–3600 drops just that row; the rest of the file still loads.

## Tip: auto-generate `index.csv`

If you would rather not hand-maintain the manifest, a tiny GitHub Action can
rebuild `index.csv` from the CSV files on every push. Ask if you want one.
