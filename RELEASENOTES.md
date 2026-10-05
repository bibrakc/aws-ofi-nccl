# v99.28.0a4 (2026-10-05)

This is a synthetic alpha release for validating the release tooling
proposed for aws/aws-ofi-nccl on the bibrakc fork, with the existing-release
check moved ahead of attestation. It is not intended for production use.

* Validate the draft, the four canonical artifacts, and their attestations.
* Validate that a rerun stops at the existing-release check without
  creating new attestations.

# v99.28.0a3 (2026-10-05)

This is a synthetic alpha release for validating the five-commit release
tooling proposed for aws/aws-ofi-nccl#1385 on the bibrakc fork. It is not
intended for production use.

* Validate that the draft is created by `gh release create` with exactly the
  four canonical artifacts and is marked as a prerelease.
* Validate build provenance attestations for all four artifacts.
* Validate that re-running the workflow refuses to touch the existing draft.
* Validate the rendered release body and job summary.

# v99.28.0a2 (2026-10-05)

This is a synthetic alpha release for validating the reworked six-commit
release tooling on the bibrakc fork. It is not intended for production use.

* Validate that the draft is created by `gh release create` with exactly the
  four canonical artifacts and is marked as a prerelease.
* Validate build provenance attestations for all four artifacts.
* Validate that re-running the workflow refuses to touch the existing draft.
* Validate the rendered release body and job summary.

# v99.28.0a1 (2026-10-02)

This is a synthetic alpha release for validating the attested, hardened
draft-release workflow on the bibrakc fork. It is not intended for production use.

* Validate that the draft carries exactly the four canonical artifacts and no
  manifest or metadata files.
* Validate build provenance attestations for all four artifacts.
* Validate least-privilege job permissions and SHA-pinned actions.
* Validate the rendered release body and job summary.

This file is a placeholder on the primary development branch of the
OFI NCCL Plugin so that "make dist" works properly.  Release branches
will have an accurate release history in this location, and each
release tarball will also have up to date release notes.

If you're looking for Plugin releases, please see the [Releases
Page](https://github.com/aws/aws-ofi-nccl/releases).
