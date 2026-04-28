# wifi6-http-server 

Simple HTTP server based on nRF5340 MCU and nRF7002 Wi-Fi 6 chip.

# Capabilities

- Wi-Fi provision over BLE
- FWU over BLE

## Tools

* CMake 3.27.0
* Ninja 1.11.1 
* zephyr-sdk-0.17.4
* JLink v796k

## External libraries

* ncs v3.2.2

## Build

### Generate ninja files

Set: `ZEPHYR_SDK_INSTALL_DIR`, `ZEPHYR_BASE`, `Zephyr_DIR`

`west build --sysbuild --pristine --cmake-only -b custom_plank -d build/build/<hw_version_<config> -- -DHW_BOARD_REVISION=<hw_version> -DBOARD_ROOT=. -DCONF_FILE=prj.conf;prj_<config>.conf`                
`cmake --preset <hw_version>_<config>`

### Build project
`west build -d build/<hw_version_<config>`
`cmake --build --preset <hw_version>_<config>`
