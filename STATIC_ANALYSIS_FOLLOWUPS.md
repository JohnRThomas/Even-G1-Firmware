# Static analysis follow-ups

Open items left after the clang-format / clang-tidy setup on the `static-analysis`
branch.  Setup and usage are in the README under
[Formatting and static analysis](README.md#formatting-and-static-analysis).

## Before merging

- [ ] **Smoke-test the branch on hardware.**  The reformat and the clang-tidy fixes
  compile to the same code apart from line-number constants and branch layout, but
  the bug-fix commits change code.  Exercise notifications, Even AI, the teleprompter,
  dashboard switching and BLE pairing.
- [ ] **Test the AW9320x error path.**  `aw9320x_i2c_write_bits()`
  (`src/aw9320x/aw9320x.c`) now returns the read error instead of writing an
  uninitialised value when the I2C read fails.  This is the only behaviour change on
  a path that can actually run, and only on a failed read.
- [ ] **Enable the hooks in every clone.**  Each contributor runs
  `pre-commit install` once; Git does not copy hooks on clone.

## Behaviour to decide

These were kept as they behaved; the code now says what it does.

- [ ] **`slave_display_thread()` gives `sem_2` unconditionally** (`src/threads/threads.c`,
  `sync_hdr.cmd` case 2).  The old condition was `mode != 9 || mode != 10 || …`, which
  is always true.  If the intent was `&&`, dashboard modes 9, 10, 11, 16 and 20 would
  skip the give; check what waits on `sem_2` in those modes before changing it.
- [ ] **`ui_ancs_notificaton_task()` redraws on every event**
  (`src/notify/ui_notification.c`).  The removed check compared `msg->uid` before
  `pull_message()` had set `msg`, so it never skipped a redraw.  Restoring a working
  skip (index, count and uid unchanged) needs `pull_message()` first, and must still
  draw a pending particle effect (`particle_pending`).

## CI

- [ ] **Add a GitHub Actions job** that runs `pre-commit run --all-files` on pushes and
  pull requests.  Without it, anyone who skips `pre-commit install` or commits with
  `--no-verify` is not checked.
  - The format and whitespace hooks need no build and can run first with
    `SKIP=clang-tidy`.
  - clang-tidy needs an NCS v2.6.0 build of both cores (Nordic's toolchain container)
    for the compile databases.
  - The commit hook only checks staged `.c` files; header-only changes are checked
    through the sources that include them.  A full-tree CI run covers that gap.

## Checks to turn on later

Disabled in `.clang-tidy`.  Counts are current hits in `src/` and `net_core/src/`.

| Check | Hits | Notes |
| --- | ---: | --- |
| `misc-use-internal-linkage` | 126 | Functions and variables that should be `static`.  Mechanical; best next candidate. |
| `bugprone-reserved-identifier` | 49 | Only 9 names: 4 include guards (`_FOO_H_`, `__AW_TYPE_H__`) and 5 underscore-prefixed functions and variables (`_reflash_fb_data_to_lcd`, `_dashboard_prv_data`, …). |
| `bugprone-switch-missing-default-case` | 27 | Add `default:` cases; decide per switch whether to log or ignore. |
| `bugprone-unhandled-code-paths` | 12 | Overlaps with the previous check. |
| `readability-redundant-nested-if` | 30 | Off because its fixes fold side-effecting calls (e.g. `enable_charger()`) into conditions.  Could be enabled without `--fix` and handled by hand. |
| `bugprone-signed-char-misuse` | 62 | All `int8_t` numbers.  The `CharTypdefsToIgnore: int8_t` option has no effect in clang-tidy 23.1.4; re-enable with that option once it works. |
| `bugprone-easily-swappable-parameters` | 76 | Advisory; mostly GUI drawing functions with adjacent coordinate parameters. |
| `misc-unused-parameters` | 262 | Mostly callbacks and thread entry points with fixed signatures (`arg`, `req_len`, `p2`, `p3`).  Low value. |
| `bugprone-signed-bitwise` | 694 | Bit operations on `int` constants.  Low value. |
| `bugprone-narrowing-conversions` | 1528 | Implicit narrowing everywhere.  Only worth it per module. |
| `misc-include-cleaner` | 1021 | Zephyr's umbrella headers make this noisy.  Not recommended. |

For each one: remove it from the disabled list, run
`pre-commit run clang-tidy --all-files`, fix or suppress, and commit the config
change with the fixes.

## Suppressions to revisit

- `font_ascii_get_glyph()` (`src/fonts/font_ascii.c`) returns a glyph offset cast to a
  pointer from a NULL base (`NOLINTNEXTLINE(performance-no-int-to-ptr)`).  Returning an
  offset would remove the cast; check its callers first.
- `spec_ble_command_hook()` (`src/ble/ble_proto.c`) uses `atoi()` for test-command
  arguments, inside `NOLINTBEGIN(bugprone-unchecked-string-to-number-conversion)`.
  Fine for a test hook; drop the block if the hook ever parses real input.
- `mbedtls_md5_update()` (`src/utils/md5.c`) has two
  `NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)` false positives.  If the stock
  mbedTLS MD5 can be used instead, this file can go.
- `K_THREAD_DEFINE` in `net_core/src/timeslot.c` is wrapped in `NOLINTBEGIN` for two
  reports that come from inside the kernel macro.

## Housekeeping

- [ ] **`src/utils/burial_point.c` is not built.**  No CMakeLists references it, so the
  clang-tidy hook skips it.  Delete it or add it to the build.
- [ ] **Deeper analysis on demand.**  This Zephyr version supports
  `west build -- -DZEPHYR_SCA_VARIANT=codechecker` for inter-procedural analysis; GCC's
  `-fanalyzer` is another option.  Both are too slow for a commit hook.
- [ ] **Tool upgrades.**  clang-format and clang-tidy are pinned to 23.1.4 in
  `.pre-commit-config.yaml`.  When bumping, run `pre-commit run --all-files`, commit
  any reformat on its own, and add that commit to `.git-blame-ignore-revs`.
