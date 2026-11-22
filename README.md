# Even Realities G1 firmware

Unofficial opensource FW for the Even Realities G1 Smart glasses. This project aims to maintain compatability with the official Mobile App while providing bug fixes and minor tweaks.

## Building

The build is setup to produce a `dfu_application.zip`. This is the standard Over The Air image that can be flashed to the glasses over Bluetooth.

Current nRF Connect SDK: 2.6.0

### VSCode

Follow the guide from NRF: https://docs.nordicsemi.com/r/bundle/nrf-connect-vscode/page/get_started/build_app_ncs.html to build using the IDE.

Instead of creating a new project use the `+ Open an existing application` and select [prj.conf](./prj.conf).

### CLI

Follow the guide to setup `west`: https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/installation/install_ncs.html

Then run:
`west build -b nrf5340dk_nrf5340_cpuapp`

## Deploying

> [!CAUTION]
> ## ⚠️ Deploying this firmware can brick your glasses and will void your warrenty ⚠️
> **Proceed with extreme caution and at your own risk!**

Run `git submodule update --init tools/ota`.

Either build the image yourself or download a build from the [Releases](https://github.com/JohnRThomas/Even-G1-Firmware/releases) page.

Then follow the directions from https://www.github.com/JohnRThomas/Even-G1-OTA

## Testing

A [Renode](https://github.com/renode/renode) based simulator is available at https://github.com/JohnRThomas/Even-G1-Simulator which can simulate two lenses at once and the communication between them and a simulated phone.

Run `git submodule update --init tools/simulator`.

Linux:
```sh
./tools/simulator/setup.sh

```

Windows:
```sh
./tools/simulator/setup.bat
```
Then follow the directions here: https://github.com/JohnRThomas/Even-G1-Simulator

### Unit Tests

Unit tests are under tests/unit and they utilize [Zephyr Native Sim](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html) to mock the hardware and run specific tests on different parts of the firmware. Based of the dependance on native_sim, the unit tests can only be run on Linux and Mac and are not compatible with Windows.

Run:
```sh
tests/unit/run.sh
```

### Regression Tests

Regression tests are under tests/reg and they utilize the renode simulator. The current tests exercise the G1 ble protocol ripped from the Android APK. Since the current firmware is built to be compatible with the Official G1 app, passing this suite should keep the glasses more or less compatible.

To run the regression tests, first setup the simulator and build the firmware, then name the update package to test:
```sh
python3 tests/reg/run.py --firmware dfu_application_<version>.zip
```
In a suite the tests are grouped by feature, one directory each: tests/reg/g1/dashboard/calendar.ref is the test `dashboard/calendar`. `-k dashboard/` runs one feature, `-k =dashboard/calendar` one test.
A run ends as PASS, FAIL, KNOWN (the only asserts that failed are marked `known_failure(...)` in the test file) or XPASS (an assert marked as a known failure held: the mark is to be removed); FAIL and XPASS fail the suite. `--out DIR` keeps the log, trace and panel pictures of every run that did not pass under DIR. `python3 tests/reg/run.py --help` and the top of tests/reg/run.py say the rest.

The engine that runs the tests (tests/reg/engine) has its own tests, which need neither the simulator nor a firmware:
```sh
python3 tests/reg/test_engine.py        # the engine
python3 tests/reg/check_packets.py      # the packet buffers are what their generators write
```
The screens a test expects have names (tests/reg/g1/frames.txt); tests/reg/g1/frames holds a picture of each name that has been taken so far (`python3 tests/reg/frames.py` lists the ones still missing and takes them from kept runs).

### Static Analysis

Three analysers run over the firmware's own sources (src/ and net_core/src/). They take the compiler flags from an existing build (`build/`, or the directory `G1_BUILD_DIR` names), check every source when none is named, and exit with 1 if anything is reported.

```sh
scripts/clang_tidy.py src/main.c      # clang-tidy with the checks of .clang-tidy
scripts/gcc_analyzer.py               # GCC's -fanalyzer, with the cross compiler of the build
scripts/cppcheck.py                   # cppcheck: defects only
scripts/cppcheck.py --style           #   ... and style findings
scripts/cppcheck.py --misra           #   ... and the MISRA C:2012 rules (about 5 minutes)
```

cppcheck is not part of the nRF Connect SDK toolchain: install it from your distribution (2.22 was used here) or point `CPPCHECK` at its binary.

The firmware does not claim MISRA compliance. The rules it does not follow, and the ones cppcheck cannot check in this setup, are listed with their reasons in scripts/misra_deviations.txt; the others are kept clean. The message of a MISRA finding is a one-line reminder from scripts/misra_rules.txt, not the text of the guideline.

## Contributing

Contributions are welcome via pull request. Pull requests may be rejected if they do not pass clang tidy and a static analysis run. Please test your change in the simulator before opening the pull request.

Pull requests are also expected to provide unit tests and may be asked to add additional simulator regression tests.