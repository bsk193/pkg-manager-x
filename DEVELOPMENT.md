# Development

This document describes how to build, test, and deploy **PKG Manager**.

## Getting Started

Clone the repository with submodules:
```bash
git clone --recurse-submodules https://github.com/itsPLK/ps5-pkg-manager.git
# Or if already cloned:
git submodule update --init --recursive
```

## How to Build


### 1. Build the Frontend
You must build the React UI first. This compiles the JSX into the single-file bundle that gets converted into C header assets:
```bash
make frontend-build
```

### 2. Build the SDK Docker Image
If you haven't already, build the PS5 payload SDK Docker container:
```bash
docker build -t ps5-payload-sdk-pkgmgr -f Dockerfile.sdk .
```

### 3. Build the ELF
Compile the native ELF using the Docker container. It is recommended to run `make clean` before rebuilding if headers or frontend changed:
```bash
docker run --rm -v $(pwd):/src -w /src ps5-payload-sdk-pkgmgr make clean all
```

The resulting `pkgmgr.elf` will be created in the root directory.

### 4. Build a Versioned Release
To build a versioned release binary (`pkgmgr_v<VERSION>.elf`):
```bash
./build_release.sh
```

## Running Unit Tests

You can run the full host test suite locally without Docker:
```bash
make test
```

This compiles and runs tests for:
- Package parser (`test_pkg_parser`)
- Drive and package scanner with manifest caching (`test_pkg_scanner`)
- Package cache and settings (`test_pkg_cache`)
- Installer state machine and space checks (`test_installer`)
- Orphaned update and DLC detection (`test_leftovers`)
- Edge cases and error handling (`test_edge_cases`)
- Multi-part packages and virtual stream engine (`test_multipart`)
- Legacy CSS syntax transformer (`test_fix_legacy_css.py`)

## Automated Deploy

For a fast build and deploy cycle over the local network, use the `deploy.sh` script:
```bash
./deploy.sh [PS5_IP]
```
(Requires PS5 IP as the first argument; sends `pkgmgr.elf` via `socat` to port 9021).
