# v99.26.0 (2026-08-18)

This is the synthetic final release for validating the alpha-to-final transition on
the bibrakc fork. It is not intended for production use.

* Validate final-version artifact names without the compact alpha suffix.
* Validate that final packaging cannot be confused with `v99.26.0a1` or `v99.26.0a2`.
* Validate that the final GitHub release remains a draft but is not marked prerelease.
* Repeat the complete build and package checks for the exact final artifact set.

# v99.26.0a2 (2026-08-18)

This is the second synthetic alpha release for validating candidate iteration on
the bibrakc fork. It is not intended for production use.

* Validate that a new alpha produces a distinct immutable draft and artifact set.
* Validate progression from `v99.26.0a1` to `v99.26.0a2` without changing alpha 1.
* Repeat plugin build and package validation for the second qualification unit.
* Prepare the release line for the subsequent final-version test.

# v99.26.0a1 (2026-08-18)

This is a synthetic alpha release for validating the aws-ofi-nccl private-draft
release qualification process on the bibrakc fork. It is not intended for
production use.

* Validate compact alpha tag and package naming (`v99.26.0a1`).
* Validate deterministic source-package metadata and the four-artifact manifest.
* Validate that alpha GitHub releases remain drafts and are marked prereleases.
* Validate the first stage of the alpha-to-final release progression.
