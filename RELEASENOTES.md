# v1.19.0.bqc1 (Simulation Release)

## Summary

Simulated release to validate source package generation changes.

## Changes

- Rewrite generate_source_packages.sh to use make dist tarball
- Switch Debian source format to 3.0 (native)
- Remove autoreconf from RPM and Debian build paths

This file is a placeholder on the primary development branch of the
OFI NCCL Plugin so that "make dist" works properly.  Release branches
will have an accurate release history in this location, and each
release tarball will also have up to date release notes.

If you're looking for Plugin releases, please see the [Releases
Page](https://github.com/aws/aws-ofi-nccl/releases).
