# Contributing

## Changes

1. Create a branch and make a focused change.
2. Run the unit tests with `make test` and build the firmware with
   `make build`.
3. Open a pull request describing the change and the hardware/configuration
   used to verify it.

Do not commit `include/secrets.h`, credentials, recordings, build output, or
local IDE settings. Keep `include/secrets.example.h` safe to publish.

Renovate opens weekly pull requests for supported dependencies. Check the
dependency dashboard for pending updates and merge dependency pull requests
only after the firmware build passes.

## Firmware releases

1. Update `CHANGELOG.md` and commit the release changes to `main`.
2. Create and push a semantic version tag, such as `v1.2.0`.
3. GitHub Actions builds the firmware from that tag and publishes the factory
   image, application image, and SHA-256 checksums as a GitHub Release.
4. Verify the release notes, assets, and checksum before distributing the
   firmware.

If a tag build needs to be retried from a workflow fix on `main`, run the
Firmware workflow manually from `main` and enter the existing version tag.

Use patch releases for compatible fixes, minor releases for backward-
compatible functionality, and major releases for breaking configuration or
behavior changes. Tags containing a prerelease suffix, such as `v1.2.0-rc.1`,
are published as prereleases.
