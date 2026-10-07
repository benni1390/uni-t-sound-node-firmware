# UNI-T Sound Node Firmware

ESP32-C6 firmware for connecting a UNI-T UT353BT sound-level meter over BLE,
publishing readings to MQTT, and streaming readings and microphone audio to a
receiver service. The receiver is a separate component; this repository
contains only the ESP32 firmware and its build/release tooling.

> **Status:** Prototype. It has not been validated for reliable unattended,
> long-term operation.

## Features

- Reads sound level and status data from a UNI-T UT353BT over BLE.
- Optionally publishes readings and meter state to MQTT.
- Streams 16 kHz mono microphone audio and meter readings over one ordered
  WebSocket connection. Audio is only sent while the microphone delivers a
  live signal; a missing or stuck mic is detected and nothing is streamed.
- Uses a 15-second pre-trigger buffer and threshold-triggered clip recording
  on the receiver; the node itself does not store audio or readings.

## Hardware

- ESP32-C6 development board (tested with `esp32-c6-devkitc-1`).
- UNI-T UT353BT sound-level meter.
- ICS-43434 I2S microphone.

| Microphone signal | ESP32-C6 pin |
|---|---:|
| VDD | 3V3 |
| GND and L/R | GND |
| SCK/BCLK | GPIO2 |
| WS/LRCLK | GPIO3 |
| SD/DOUT | GPIO10 |

L/R is grounded for the left channel. The sensor delivers 24-bit audio in
32-bit frames; the firmware sends the upper 16 bits as 16 kHz mono PCM. Do not
connect the microphone to 5 V. Pins are configurable in `include/secrets.h`.
Avoid GPIO 4, 5, 8, 9, 15 (strapping pins) and GPIO 12, 13 (USB).

## Configure

Copy the example configuration and edit it for your network and receiver:

```sh
make config
```

Set `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST`, `SERVER_PORT`,
`SERVER_TOKEN`, and a unique `NODE_ID` in `include/secrets.h`. The token must
match the receiver's `UPLOAD_TOKEN`. Set `METER_MAC` to bind a specific meter,
or leave it empty to use the first nearby UT353BT.

MQTT is optional; leave `MQTT_HOST` empty to disable it. For TLS, use the
receiver's DNS name and configure `MQTT_TLS` and credentials as described in
the example configuration. Never commit `include/secrets.h` or credentials.

## Build and flash

Requirements: Docker, Python 3, GNU Make, and a USB-connected ESP32-C6 for
flashing.

```sh
make help       # list targets
make build      # compile in Docker
make upload     # compile and flash over USB
make flash      # compile, flash, then open the serial monitor
make monitor    # open the serial monitor without flashing
```

For a local setup that also has the receiver repository next to this checkout,
`make deploy` deploys the receiver and then flashes the ESP32.
`make deploy-server` and `make deploy-device` run either step independently.
Set `SERVER_DIR=/path/to/uni-t-sound-node-server` to override the default
`../uni-t-sound-node-server`. These convenience targets are optional; building
and releasing the firmware does not require the receiver repository.

The Docker image pre-installs the platform, libraries, toolchain, and
framework declared in `platformio.ini`. Its first build downloads roughly 1 GB
of dependencies; Docker reuses image layers on later builds. Local builds seed
the project library cache in `.pio/libdeps`. GitHub Actions builds and exports
the firmware directly from Docker without loading the full toolchain image
onto the runner. Changes to `platformio.ini` invalidate the dependency layer.
Host flashing and serial monitor tools are installed into `.venv` by
`make tools`.

## Automatic dependency updates

This repository uses Renovate for GitHub Actions, Docker base images,
PlatformIO libraries, and the ESP32 platform. Install and enable the
[Renovate GitHub App](https://github.com/apps/renovate) for this repository;
Renovate will then open weekly update pull requests. Review and merge them
after the firmware build passes.

## Releases

Releases use semantic version tags in the form `vMAJOR.MINOR.PATCH`, for
example `v1.2.0`. Update `CHANGELOG.md`, push the version tag, and GitHub
Actions will build the firmware and publish the factory image, application
image, and SHA-256 checksums as release assets. Verify `SHA256SUMS.txt`
before flashing; the factory image can be flashed with
`esptool --chip esp32c6 write-flash 0x0 <factory-image>`. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) for the release steps.

## Privacy

The node streams audio and readings to the configured receiver and does not
buffer data when the receiver or Wi-Fi is unavailable. Audio may capture
speech; obtain consent and secure the receiver and its recordings. No data is
sent to a third-party cloud by this firmware.

## License

This project is licensed under the MIT License. See [`LICENSE`](LICENSE).
