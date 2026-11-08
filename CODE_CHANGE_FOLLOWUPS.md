# Code change follow-ups

Open items left after the clang-format / clang-tidy setup and the source tree
restructuring.  Setup and usage are in the README under
[Formatting and static analysis](README.md#formatting-and-static-analysis).

## Before merging

- [ ] **Smoke-test the branch on hardware.**  The reformat, the `static` qualifiers, the
  added `default:` cases and the merged nested `if`s compile to the same logic, but the
  bug-fix commits change code.  Exercise notifications, Even AI, the teleprompter,
  dashboard switching and BLE pairing.
- [ ] **Test the AW9320x error path.**  `aw9320x_i2c_write_bits()`
  (`src/drivers/touch/aw9320x.c`) now returns the read error instead of writing an
  uninitialised value when the I2C read fails.  This is the only behaviour change on
  a path that can actually run, and only on a failed read.
- [ ] **Enable the hooks in every clone.**  Each contributor runs
  `pre-commit install` once; Git does not copy hooks on clone.  CI catches what a
  missing hook lets through (below).
- [ ] **Watch the first CI run.**  `.github/workflows/pre-commit.yml` has not run yet.
  The clang-tidy job depends on Nordic's `sdk-nrf-toolchain:v2.6.0` container, a
  `west init` / `west update --narrow` of NCS v2.6.0 (cached between runs) and
  `west build` locating the workspace through `ZEPHYR_BASE`.  Any of these may need a
  tweak on the first run.

## Behaviour kept as shipped

Both were flagged during the clang-tidy clean-up as conditions that could never be
false.  The shipped firmware behaves this way, and the project tracks the shipped
behaviour, so both stay.

- **`slave_display_thread()` gives `sem_2` unconditionally** (`src/comm/esb/esb_slave.c`,
  `sync_hdr.cmd` case 2).  `sem_2` is the slave's refresh kick: the refresh thread in
  `src/ui/screen_dispatch.c` waits on it with a 500 ms timeout and drains it before
  suspending, and the semaphore's limit is 1, so an extra give only shortens that
  wait.  Had the vendor meant `&&`, the slave would redraw up to 500 ms later in
  dashboard modes 9, 10, 11, 16 and 20; nothing else waits on it.
- **`ui_ancs_notificaton_task()` redraws on every event**
  (`src/ui/screens/ui_notification.c`).  The removed check compared `msg->uid` before
  `pull_message()` had set `msg`, so the shipped firmware never skipped a redraw
  either.  A working skip (index, count and uid unchanged) would be an optimisation,
  needs `pull_message()` first, and must still draw a pending particle effect
  (`particle_pending`).

## CI

- [x] **GitHub Actions** runs `pre-commit` on pushes and pull requests
  (`.github/workflows/pre-commit.yml`): the format and whitespace hooks in one job
  (`SKIP=clang-tidy`), clang-tidy in a second job after a build of both cores.
- [x] **A stale build no longer passes files unchecked.**  The hook used to skip any
  source missing from the compile database with a note, which after a tree
  restructuring meant every staged file.  It now fails when a `CMakeLists.txt` lists
  the file, and asks for a rebuild.
- The commit hook only checks staged `.c` files; header-only changes are checked
  through the sources that include them.  The CI run over the whole tree covers that
  gap.

## clang-tidy checks

Enabled in this round (`.clang-tidy`):

| Check | Resolution |
| --- | --- |
| `misc-use-internal-linkage` | `static` on 100-odd functions and variables.  Six functions were flagged only because their source did not include its own header (`report.c`, `proto_put_local.c`, `watchdog.c`); those now include it.  Kernel macros that define non-static objects (`K_MSGQ_DEFINE`, `LOG_MODULE_REGISTER`, `K_THREAD_DEFINE`) and `bt_ctlr_assert_handle()`, which the SoftDevice Controller library calls, carry a `NOLINT` at the use.  `K_THREAD_STACK_DEFINE` and `K_TIMER_DEFINE` take a `static` prefix; `uart_slab` uses `K_MEM_SLAB_DEFINE_STATIC`. |
| `bugprone-reserved-identifier` | Renamed: `reflash_fb_data_to_lcd[_ex]`, `clean_fb_data`, `dashboard_prv`, `tpm_prv`, and the four AW9320x include guards (`G1_DRIVERS_TOUCH_*_H_`). |
| `bugprone-switch-missing-default-case`, `bugprone-unhandled-code-paths` | `default: break;` added to 27 switches on non-enum values.  Every one of them fell out of the switch for unlisted values already; none logs. |
| `readability-redundant-nested-if` | 30 nested `if`s merged with `&&`.  Semantics are unchanged (the inner condition is still evaluated only when the outer holds), including the folded calls `enable_charger()`, `nfc_timeout()`, `send_esb_stats()` and `esb_start_tx()`. |

Still disabled.  Counts are hits in `src/` and `net_core/src/`.

| Check | Hits | Notes |
| --- | ---: | --- |
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

- `font_ascii_get_glyph()` (`src/ui/gui/fonts/font_ascii.c`) returns a glyph offset cast to a
  pointer from a NULL base (`NOLINTNEXTLINE(performance-no-int-to-ptr)`).  Its callers
  in `src/ui/screens/ui_countdown.c` pass the result straight to
  `copy_to_frame_buffer_raw()` as a bitmap, so returning an offset needs the ASCII
  glyph table the offset indexes, which this tree does not have.  The suppression
  stays until that table is identified.
- `spec_ble_command_hook()` (`src/comm/ble/debug_cmd.c`) uses `atoi()` for test-command
  arguments, inside `NOLINTBEGIN(bugprone-unchecked-string-to-number-conversion)`.
  Fine for a test hook; drop the block if the hook ever parses real input.
- `mbedtls_md5_update()` (`src/util/md5.c`) has two
  `NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)` false positives.  The app image
  uses TinyCrypt, which has no MD5, and mbedTLS is not in the image; with the
  application flash at 97 % the file stays.
- [x] `ndefMessageEncode()` (`src/services/nfc_wlc/ndef.c`): `info` is initialised, the
  suppression is gone.
- `K_THREAD_DEFINE` in `net_core/src/timeslot.c` is wrapped in `NOLINTBEGIN` for three
  reports that come from inside the kernel macro.

## Housekeeping

- [x] **`burial_point.c` is not built.**  Deleted with the source tree restructuring.
- [ ] **Deeper analysis on demand.**  This Zephyr version supports
  `west build -- -DZEPHYR_SCA_VARIANT=codechecker` for inter-procedural analysis; GCC's
  `-fanalyzer` is another option.  Both are too slow for a commit hook.
- [ ] **Tool upgrades.**  clang-format and clang-tidy are pinned to 23.1.4 in
  `.pre-commit-config.yaml`.  When bumping, run `pre-commit run --all-files`, commit
  any reformat on its own, and add that commit to `.git-blame-ignore-revs`.
