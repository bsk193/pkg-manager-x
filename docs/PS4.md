# PS4 payload (experimental)

`pkg-manager-x_<version>_ps4.elf` is the PS4 build of PKG Manager X. It shares
the web UI, sources (USB, disc, SMB, HTTP/HTTPS), scanner, stream server and
Direct Install with the PS5 build. Only the console integration differs:

| Area | PS5 build | PS4 build |
|------|-----------|-----------|
| Toolchain | ps5-payload-sdk (`prospero-*`) | ps4-payload-sdk (`orbis-*`) |
| Install call | `sceAppInstUtilInstallByPackage` | BGFT download task (`libSceBgft.sprx`, resolved at runtime) |
| Install progress | `sceAppInstUtilGetInstallStatus` | BGFT task progress + app.db / version checks |
| Installable packages | PS4 and PS5 | PS4 only (PS5 packages are listed, tagged "PS5 only", and refused) |
| Home screen shortcut | yes | no (hidden in Settings) |
| DLC status | native API + app.db | app.db / addcont.db / filesystem |

Target setup: **GoldHEN on firmware 13.52** (WebKit jailbreak). GoldHEN handles
the kernel side; the payload itself has no firmware-specific offsets.

## Status: not yet tested on hardware

The PS4 build compiles, but these points can only be confirmed on a console.
Please run them in order before relying on it and report the results (log:
`http://<ps4-ip>:8844/api/log`).

1. **Loading & staying resident**
   - Send the ELF to GoldHEN's payload loader: `socat -u - TCP:<ps4-ip>:9090 < pkg-manager-x_*_ps4.elf`
     (or `./deploy.sh <ps4-ip> ps4`). An elfldr on port 9021 also works (`LOADER_PORT=9021`).
   - Expected: notification "PKG Manager vX starting..." then "Found N package(s)".
   - Open `http://<ps4-ip>:8844` from a PC. The page must stay reachable after the
     loader returns (the payload runs as its own background process).
2. **Scanning**: plug a USB drive with `PS4/Game.pkg`; it should show a **PS4** badge.
   Add a PS5 package: it should show **PS5 only** and its install button stays disabled.
3. **BGFT install (the main unknown)**: install a small PS4 homebrew package (a few MB).
   Watch for log lines:
   - `[BGFT] ready` at startup; if instead `symbol ... not found` or `init returned 0x...`
     appears, the BGFT ABI differs on 13.52: report the codes.
   - `[BGFT] register type=PS4GD ... -> 0x00000000 task=N` and `start task N -> 0x00000000`.
   - `[STREAM] ...` lines showing the system downloading from `127.0.0.1:18841`.
   - The package appears under the console's Notifications / Downloads and finishes installing.
4. **Updates / DLC**: install an update (`PS4DP`) and a DLC (`PS4AC`) for an installed game.
5. **Network sources**: repeat step 3 from an SMB share and from an HTTP source.
6. **Rest mode**: put the console in rest mode during idle, wake it, and reload the page.
7. **Relaunch**: send the ELF again while it is running. The old instance must be
   replaced (upstream's process lookup reads FreeBSD `kinfo_proc` records; the
   layout is believed identical on PS4 but is unverified).
8. **Storage use**: note free space before/after a large install. BGFT may stage the
   download before installing, which would need up to 2x the package size on PS4.

## Known gaps / ideas

- BGFT structure layouts and option flags follow public PS4 homebrew headers
  (OpenOrbis `libSceBgft.h`, flatz' Remote Package Installer). If step 3 fails,
  `src/platform_install_ps4.c` is the only file to adjust.
- A PS4 home-screen tile would need a real fake-PKG app (Homebrew Launcher style); not implemented.
- `sceAppInstUtilInstallByPackage` may also exist on newer PS4 firmware. If BGFT
  proves unreliable, it could be tried as an alternative backend.

## Building

```bash
docker build -t ps4-payload-sdk-pkgmgr -f Dockerfile.sdk-ps4 .
make frontend-build
docker run --rm -v "$(pwd)":/src -w /src ps4-payload-sdk-pkgmgr make clean all PLATFORM=ps4
# -> pkgmgr-ps4.elf
```

`./build_release.sh` builds both consoles (`ps5` / `ps4` arguments select one).
