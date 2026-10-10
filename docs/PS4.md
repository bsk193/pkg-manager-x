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
| Home screen shortcut | installed by the payload | separate tile package `*_ps4-tile.pkg` (see below) |
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
- `sceAppInstUtilInstallByPackage` may also exist on newer PS4 firmware. If BGFT
  proves unreliable, it could be tried as an alternative backend.

## Home screen tile (optional)

PS4 has no "web link" tile like the PS5 shortcut, so releases include a small
app package, `pkg-manager-x_<version>_ps4-tile.pkg` (title ID `PKGX00001`).
Install it once with GoldHEN's **Package Installer** (or any fake-PKG installer).

Pressing the tile:
1. If PKG Manager X is not running yet (nothing answers on port 8844), it sends
   the payload bundled in the package to GoldHEN's **BinLoader** on
   `127.0.0.1:9090` and waits for the server to start. The BinLoader must be
   enabled in GoldHEN's settings; otherwise load the ELF as usual first.
2. It shows `http://127.0.0.1:8844/` in the system web browser dialog, inside
   the tile. Circle closes it and the tile exits to the home screen. Unlike the
   Browser app, this opens no new browser window on each launch. If the dialog
   cannot open, the tile opens the Browser app instead and closes itself.

**Don't use rest mode while PKG Manager X is installing.** The PS4 downloads
every byte through the payload on the console (`127.0.0.1`); with GoldHEN's
rest mode support on, suspending it mid-transfer has crashed the console
(kernel panic). Let installs finish first.

The tile and the browser are only the UI: closing either never stops the
PKG Manager X service, which runs until reboot or rest mode. If the same
version is already running, a newly sent payload (tile, or manual) exits and
leaves the running service alone; a different version replaces it.

So after a reboot + jailbreak the tile is all you need. The bundled payload is
the one from the same release; install the new tile package when you update
(pre-releases of the same version share the package version, so remove the
old tile first if the installer says it is already installed).

The tile is built in CI with the [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain)
from `ps4-launcher/` (`make -C ps4-launcher OO_PS4_TOOLCHAIN=... PAYLOAD=pkgmgr-ps4.elf`).

## Building

```bash
docker build -t ps4-payload-sdk-pkgmgr -f Dockerfile.sdk-ps4 .
make frontend-build
docker run --rm -v "$(pwd)":/src -w /src ps4-payload-sdk-pkgmgr make clean all PLATFORM=ps4
# -> pkgmgr-ps4.elf
```

`./build_release.sh` builds both consoles (`ps5` / `ps4` arguments select one).
