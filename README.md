# wifi6-http-server 

Simple lightweight HTTP server based on Zephyr, nRF5340 MCU and nRF7002 Wi-Fi 6 chip.

# Capabilities

- **Wi-Fi power saving modes**
  - Power Save disabled (maximum performance)
  - DTIM-based wakeup (balanced power / connectivity)
  - Listen Interval mode (reduced power consumption)
  - Target Wake Time (TWT) support (Wi-Fi 6 optimized power saving)

- **Wi-Fi provisioning via shell**
  - Runtime configuration using Zephyr shell
  - Scan, connect, and manage Wi-Fi networks from CLI

- **LED control over HTTP**
  - REST-like endpoint:
    - `/led/`
  - Simple control of on-board LEDs

- **mDNS support**
  - Device discoverable via `.local` hostname
  - No need to know IP address

### Optional features

- **Wi-Fi provisioning over BLE**
  - Configure network credentials using Bluetooth Low Energy

- **HTTP-based communication**
  - Extendable HTTP interface for device interaction / data exchange

- **DFU over BLE (MCUboot)**
  - Firmware update using MCUBoot and BLE transport

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
