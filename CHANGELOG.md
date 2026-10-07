# Changelog

Notable changes to this project are documented here.

This project follows [Semantic Versioning](https://semver.org/). Firmware
releases are tagged `vMAJOR.MINOR.PATCH`.

## [Unreleased]

- Split `main.cpp` into focused modules (BLE, audio, MQTT, WebSocket, WiFi,
  status, frame parser).
- Add host-side unit tests for the frame parser, run in CI.
- Fix stale flags in `parse_measurement` when no flag is set.
- Enable `-Wall -Wextra` and add a `.clang-format`.

## [0.1.0] - 2026-10-07

- Initial public prototype release for ESP32-C6 and the UNI-T UT353BT.
- BLE sound readings, optional MQTT publishing, and microphone audio streaming
  to a separately deployed receiver.
- GitHub Actions builds factory/application firmware binaries and publishes
  SHA-256 checksums with versioned releases.
