# AWS OFI NCCL Release Notes

## v1.19.0 (TBD)

This is a major release introducing new features and improvements.

### New Features
- Migrated request handling to C++ objects for improved memory management
- Added Ubuntu 24.04 support in CI pipeline
- Enhanced LTTng tracing configuration in GitHub Actions

### Bug Fixes
- Fixed NCCL topology CPU tag generation for NUMA nodes
- Improved compiler selection in CI

### Testing
- Added build matrix for debug and release builds
- Enhanced tracing status reporting in CI tests

### Supported Distributions
- Amazon Linux 2
- Amazon Linux 2023
- Ubuntu 20.04
- Ubuntu 22.04
- Ubuntu 24.04
- Debian
- RedHat

### Compatibility
- Tested with NCCL versions: 2.21.x, 2.22.x, 2.23.x
- Tested with libfabric 1.20.x and 1.21.x
- Supported providers: EFA, TCP, sockets
