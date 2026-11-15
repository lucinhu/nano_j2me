# Device profile default: generic stays the default

Date: 2026-11-15

## Question

Titles that branch on `microedition.platform` reject the generic handset (see
`docs/notes/2026-11-15-libretro-device-properties.md`). Should the runtime
default to `nokia6230` so those titles work everywhere?

## Experiment

Full 404-title corpus sweep with a wrapper that appends `--device nokia6230`
to every invocation (unless the command already selected a device):

```
/tmp/nj2me-nokia  (wrapper: exec nj2me "$@" --device nokia6230)
NJ_BIN=/tmp/nj2me-nokia tools/sweep_compat.py .local-tools/debug-runs/platform-nokia-20261115
```

Baseline: `.local-tools/debug-runs/compat-20261115-r9` (373 ok).

## Result

| Verdict | generic (baseline) | nokia6230 default |
| --- | ---: | ---: |
| ok | 373 | 369 |
| title-exit | 26 | 27 |
| title-exception | 2 | 2 |
| no-frame | 2 | 2 |
| failure | 0 | 2 |
| timeout | 0 | 1 |
| bad-jar | 1 | 1 |

Four `ok` rows regressed and none improved:

* `GT赛车2_36.jar` ok → timeout (the known load-sensitive row);
* `时光樱花册－遗失之心_1009.jar` ok → failure;
* `生化危机之恶化_8448.jar` ok → title-exit (different platform branch);
* `美男狂想曲_3876.jar` ok → title-exit;
* `比基尼美女海底影相_9643.jar` title-exit → failure.

## Decision

Keep `generic` as the default. Titles that need a handset profile are handled
where it is cheap and explicit:

* the CLI corpus sweep retries failure/timeout rows once with
  `--device nokia6230` and records the note "needs --device nokia6230";
* the libretro core retries a failed load once with `nokia6230` and logs it
  (see the r17 note), which fixes the two device-sensitive titles in the
  whole-corpus rendering probe without changing the default for the rest.

A global default would also silently change the platform string for every
title in the corpus, which this experiment shows is a net loss.
