# Even Realities G1 firmware

Unofficial opensource FW for the Even Realities G1 Smart glasses. This project aims to maintain compatability with the official Mobile App while providing bug fixes and minor tweaks.

## Building

The build is setup to produce a `dfu_application.zip`. This is the standard Over The Air image that can be flashed to the glasses over Bluetooth.

Current nRF Connect SDK: 2.5.1

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

### Regression Tests

Regression tests are under tests/reg and they utilize the renode simulator. The current tests exercise the G1 ble protocol ripped from the Android APK. Since the current firmware is built to be compatible with the Official G1 app, passing this suite should keep the glasses more or less compatible.

To run the regression tests, first setup the simulator, then:
```sh
python3 tests/reg/run.py
```

### Unit Tests

Unit tests are under tests/unit and they utilize [Zephyr Native Sim](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html) to mock the hardware and run specific tests on different parts of the firmware. Based of the dependance on native_sim, the unit tests can only be run on Linux and Mac and are not compatible with Windows.

Run:
```sh
tests/unit/run.sh
```

## Contributing

Contributions are welcome via pull request. Pull requests may be rejected if they do not pass clang tidy and a static analysis run. Please test your change in the simulator before opening the pull request.

Pull requests are also expected to provide unit tests and may be asked to add additional simulator regression tests.