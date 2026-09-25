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

### 4. Build a Versioned Development Binary
To build versioned development binaries (`pkg-manager-x_v<VERSION>-dev-<SHORT_HASH>_<ps5|ps4>.elf`):
```bash
./build_release.sh          # PS5 + PS4
./build_release.sh ps5      # one console only
```

### PS4 Build (PKG Manager X)
The PS4 payload uses the ps4-payload-sdk in its own image:
```bash
docker build -t ps4-payload-sdk-pkgmgr -f Dockerfile.sdk-ps4 .
docker run --rm -v $(pwd):/src -w /src ps4-payload-sdk-pkgmgr make clean all PLATFORM=ps4
```
The result is `pkgmgr-ps4.elf`. Console-specific code is selected with
`include/platform.h` (`PKGMGR_CONSOLE_PS5` / `PKGMGR_CONSOLE_PS4`); install
backends live in `src/platform_install_ps5.c` and `src/platform_install_ps4.c`.
See [docs/PS4.md](docs/PS4.md).

### HTTPS
`build_deps.sh` builds mbedTLS 3.6.2 with a payload configuration (TLS 1.2, no
net/timing modules, entropy and zeroize hooks provided by `src/http_source.c`).
Use `make HTTPS=0` for a plain-HTTP build without mbedTLS.

### Versioning (PKG Manager X)
PKG Manager X has **its own semantic version** (`1.0.0`, `1.1.0`, `1.1.1-beta.1`, ...),
independent from upstream's. Every build also records the upstream release it is
**based on**:

- `include/version.h` (`PKGMGR_VERSION`) stays upstream's and is **never edited in
  this fork**, so upstream version bumps merge without conflicts and the "based on"
  version follows automatically.
- `include/version_x.h` defines `PKGMGR_X_VERSION` (ours) and
  `PKGMGR_UPSTREAM_VERSION`. CI compiles the release version in with
  `make X_VERSION=...`; local builds use `git describe` (e.g. `1.1.0-3-gabc1234`)
  or `0.0.0-dev`.
- Shown as "PKG Manager X 1.1.0 - based on PKG Manager v1.2.4 by PLK" in the page
  title, footer, logs, diagnostics, `/api/platform` (`version`, `upstream_version`)
  and the release notes. `/api/version` returns our version.
- Our git tags are prefixed, `x-v1.1.0`, so they never clash with upstream's
  `v1.2.4` tags that forks carry.

### CI and Releases (GitHub Actions)
- **Build** (`.github/workflows/build.yml`): every branch push and pull request runs
  `make test`, builds the frontend, and builds both payloads in their SDK images
  (cached between runs). The ELFs are attached to the run as artifacts.
- **Release** (`.github/workflows/release.yml`) runs on tag push:

  | Tag | Result |
  |-----|--------|
  | `x-v1.1.0` | Release, marked **Latest** |
  | `x-v1.1.1-beta.1` / `-rc.1` / `-alpha.1` | **Pre-release**, never marked Latest |

  ```bash
  git tag x-v1.1.0 && git push origin x-v1.1.0
  ```
  Tags must match `x-vMAJOR.MINOR.PATCH` with an optional `-(alpha|beta|rc).N`
  (write `beta.1`, not `beta1`, so `beta.10` sorts after `beta.9`); anything else
  fails before building. Backup: *Actions → Release → Run workflow* with a version
  creates the tag on the selected branch.
- Releases are only made from commits on `main` (the workflow refuses others).
- **Release script (Windows)**: `tools/release.ps1` computes the next version from
  the existing tags, checks the working tree / branch / tag, shows a summary and
  pushes the tag after confirmation:

  ```powershell
  .\tools\release.ps1 -Bump patch              # 1.0.0 -> x-v1.0.1 (release)
  .\tools\release.ps1 -Bump minor -Pre beta    # -> x-v1.1.0-beta.1 (pre-release)
  .\tools\release.ps1 -Pre beta                # next beta: x-v1.1.0-beta.2
  .\tools\release.ps1 -Pre rc                  # release candidate: x-v1.1.0-rc.1
  .\tools\release.ps1 -Promote                 # x-v1.1.0-rc.1 -> x-v1.1.0 (release)
  .\tools\release.ps1 -Version 2.0.0 -DryRun   # explicit version, preview only
  ```
- Each release publishes `pkg-manager-x_v<version>_ps5.elf` and `..._ps4.elf` with
  notes linking the upstream release it is based on.
- Move the `## Unreleased` notes in `CHANGELOG.md` under the new version when releasing.
### Keeping Up With Upstream
```bash
git remote add upstream https://github.com/itsPLK/ps5-pkg-manager.git   # once
git fetch upstream
git merge upstream/main
```
Fork additions live mostly in new files (`http_source*`, `pkg_platform*`,
`pkg_parse_reader*`, `platform*`, `tests/test_http_source.c`,
`tests/test_pkg_platform.c`) to keep merge conflicts small.

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
- PS5 installer stream simulation (`test_stream_sim`) — pairs the PS5
  request-pattern simulator (`tests/ps5_sim.c`) against the real stream
  server (`src/stream_server.c`) on the host; no PS5 required (see below)
- Direct Install WebSocket transport and live-stream tests (`test_ws_upload`,
  `test_ws_stream`, `test_ws_stream_far`, `test_direct_install_e2e`)
- In-memory package parsing (`test_parse_mem`)
- PS4/PS5 platform detection, folder scanning and PS4 install gating
  (`test_pkg_platform`, also run with `PKGMGR_CONSOLE=ps4`)
- HTTP sources against a local test server: listings, `index.json`, parsing,
  pooled range streaming, auth, redirects, no-Range servers, scanner
  integration (`test_http_source`)
- Legacy CSS syntax transformer (`test_fix_legacy_css.py`)

### PS5 Installer Stream Simulator

`test_stream_sim` reproduces the exact HTTP request pattern the PS5 background
package installer sends to the stream server (`:18841`), so install methods
(websocket client, direct stream creation) can be developed and verified on the
host without a console. It is modeled from the captures in
`.for_reference/stream_debug/`: a burst of header re-reads, a `*.crc` sidecar
probe that must 404, then two parallel bulk connections serving contiguous
16 MiB byte-ranges.

- It is built and run automatically by `make test` (listed in `TESTS`, with
  `tests/ps5_sim.c` in `TEST_SRCS`).
- `tools/ps5_installer_sim.c` is the standalone CLI half — it can point the
  same replay at any live server (`--no-server --port 18841`), or be fully
  self-contained: build a fixture PKG, start a local stream server, replay the
  PS5 pattern, and print a reference-format replay log.
- `tools/run_stream_sim.sh` builds and runs the CLI:

  ```bash
  tools/run_stream_sim.sh --demo          # build fixture, replay, print log
  tools/run_stream_sim.sh --no-server     # replay against an existing server
  ```

### Direct Install over WebSocket

The Direct Install page lets a LAN browser, such as a PC, push a local `.pkg` to the
daemon, which streams it straight into the installer from RAM — nothing
is stored on disk. Install can start as soon as the header is parsed,
while the rest still uploads:

- Transport: `src/ws_upload.c` (`include/ws_upload.h`) — RFC6455 listener
  on `:18842`, in-order chunks with resume. Narrow REST hook in
  `src/http_server.c` (`POST /api/upload/init|finish|cancel`,
  `GET /api/upload/status`); chunk bytes never go through MHD.
- Live session: `src/ws_stream.c` (`include/ws_stream.h`) — 1 MB pinned
  header cache + configurable RAM ring (64 MB default, `WS_LIVE_RING_MB`),
  blocking readers, bounded writer admission, and abort/timeout handling.
  Served through the existing HTTP range path via the `live:<id>`
  virtual-stream scheme (`src/multipart.c`, `src/stream_server.c`).
- Metadata: additive `pkg_parser_parse_mem()`; install entry
  `installer_start_live()`; `/api/install` routes `live:` URIs.
- Frontend: `DirectInstallView.jsx` + `api/directInstall.js` +
  `hooks/useDirectUpload.js` (Header "Direct Install" button and app-wide
  file drop). Install is
  enabled at `header_ready`, with sent/installed dual progress.
- Upload scheduling: the sender uploads the header first, then follows installer
  seeks with a bounded window of up to eight 1 MiB segments and up to two
  uploads in flight.
  Busy replies retry the same segment; requests for in-flight segments are
  coalesced. The WebSocket listener starts on demand and closes when idle.
- With install debug mode enabled, the install screen shows WebSocket receive
  and install speed graphs. Stream logs include build identity, receive and
  accepted throughput, cache duplicate/reload/eviction counters, and periodic
  browser file-read and send-to-ACK timing summaries. The selected debug
  directory keeps at most 20 stream logs and 20 SMB logs.
- Host tests (all in `make test`): `test_ws_stream` (ring unit),
  `test_parse_mem` (parse vs parse_mem differential),
  `test_ws_upload` (codec + socket + fragmentation), `test_direct_install_e2e`
  (concurrent push + PS5 pull, abort fail-fast, small-ring wrap,
  `installer_start_live` commit).
- Simulators: `tools/ws_push_sim.c` via `tools/run_direct_install_sim.sh`:

  ```bash
  tools/run_direct_install_sim.sh --demo                # concurrent push+pull
  tools/run_direct_install_sim.sh --demo --resume-test  # drop + resume
  tools/run_direct_install_sim.sh --pkg game.pkg --port 18842  # live server
  ```

- Frontend mock: `node frontend/mock-server.js` serves the same REST shape
  plus a memory-backed mock WS listener on `:18842`.
- The sender scheduler also has focused Node.js tests, separate from `make test`:
  ```bash
  cd frontend && npm run test:upload
  ```

## Automated Deploy

For a fast build and deploy cycle over the local network, use the `deploy.sh` script:
```bash
./deploy.sh [PS5_IP]
```
(Requires PS5 IP as the first argument; sends `pkgmgr.elf` via `socat` to port 9021).
