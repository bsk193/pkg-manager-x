# Changelog

## Unreleased (PKG Manager X)

### HTTP / HTTPS Sources
- Install packages from web servers with byte-range streaming (no temporary copy)
- Discovery through `index.json` (`tools/make_http_index.py`) or HTML directory listings, including sub-folders
- Basic auth, redirects, keep-alive connection pool for the installer's parallel requests
- HTTPS via mbedTLS with CA verification, certificate pinning ("Trust this certificate") or no check

### PS4 / PS5 Tags
- Every package reports its console, detected from the package itself (FIH / param.json / param.sfo), with Title ID and folder fallbacks
- PS4 / PS5 filter in the package browser and badges in the detail view
- Warning when a package is stored in the other console's `PS4/` / `PS5/` folder
- `PS4/` and `PS5/` folders on USB drives and discs are scanned like `pkg/`

### PS4 Payload (experimental)
- New `PLATFORM=ps4` build (ps4-payload-sdk, GoldHEN) sharing the UI and all sources
- Installs through BGFT; PS5 packages are listed but refused on PS4
- See docs/PS4.md for the on-console test checklist

---

## v1.2.4

### Direct Install
- Improved transfer speeds to around 110 MB/s on a fast local network
- Added an optional live speed display and more helpful diagnostic logs
- Limit stored stream and SMB debug logs to the 20 most recent of each

---

## v1.2.3

### Startup Reliability
- Fixed startup failures affecting some users by making process discovery safer across PS5 environments, improving initialization ordering, and adding earlier diagnostics and notifications when startup steps fail

### Installation
- Fixed the Base + Update handoff so the update is queued by the native installer and continues even if the browser is closed after starting the base installation

### SMB Network Shares
- Added guided share selection and folder browsing, so share names and paths no longer need to be entered manually
- Preserve saved credentials when browsing an edited share whose password is masked in the UI, avoiding accidental guest logins
- Treat SMB sources consistently as read-only without attempting a network write-permission probe

### Browser Navigation
- Fixed browser Back/Forward navigation so returning to the app after visiting another page preserves the app's main page and menu history

---

## v1.2.2

### Package Detection
- Fixed PS4 base packages being incorrectly identified as DLC

### Cache and Scanning
- Cache is cleared and the package catalog is rescanned automatically after an app update; clearing the cache manually also refreshes the catalog

---

## v1.2.1

### Direct Install
- PKG files can now be installed from another device, such as a PC, directly to the console without a temporary disk copy

### Networking
- Moved package streaming and Direct Install services to ports `18841` and `18842` to avoid potential conflicts with other homebrew applications. The web interface remains on port `8844`.

### SMB Streaming
- Improved SMB package streaming performance, reaching approximately 110 MB/s during installs in ideal conditions
- Added streaming diagnostics to help identify network and server bottlenecks

### Extended USB Storage
- Available-space checks now include extended USB storage (`/mnt/ext0`)

### Package Badges
- PS4 and PS5 package badges are now shown in package lists

### Package Detection and Scanning
- Fixed detection of PS5 DLC packages
- Fixed drive scans hiding other packages when an unknown package was present, such as during a copy operation

---

## v1.1.0

### Multi-Language PKG Titles
- Package titles now display in your browser's preferred language instead of the first available entry

### Controller Navigation (PS5 Browser)
- **Circle button** navigates back through pages instead of immediately closing the browser
- Pressing Circle on the root Drive Select screen or during installation still closes the browser
- Modals block spatial navigation in the background while open

### SMB Diagnostics
- SMB connection testing now shows clearer, actionable error messages — making it easier to diagnose Windows share issues

### Storage Display
- The header now shows free space for **Internal** and **M.2** drives separately
- Installation is blocked when there isn't enough free space on any available drive

---

## v1.0.0

- Initial Release
