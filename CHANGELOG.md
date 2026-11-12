# Changelog

## 1.1.0 (PKG Manager X): based on upstream v1.5.1

- Tested on PS4 only. The PS5 build is included but was not tested on a PS5 for this release.
- Merged PKG Manager v1.5.0 / v1.5.1. Its console-owned install queue (full-page Queue view, cancel / retry / clear, auto-clear after 60 minutes, base/update/DLC as separate jobs, dependents blocked when their base is missing) replaces PKG Manager X's own queue, "Continue in Background" bar and Base + Update batch. HTTP/HTTPS packages are queue jobs like USB, disc and SMB ones.
- PS5: USB/disc direct-storage installs are submitted to the PS5 system queue (upstream behavior). PS4 keeps its BGFT installs: download-first for HTTP sources, and USB/disc installs tracked until verified.
- Also from upstream: PS5 backport patch detection, SDK target badge, separate base packages per title, optional filename/path in title details, readable out-of-space errors, SMB catalog fix for several locations on one share.
- Kept: console refusal reasons and "unavailable on the server" now appear as the queue job's error.
- HTTP/HTTPS sources read Pegasus DL+ catalogs: enter the catalog's `.json` URL (a query string such as a token is kept). Each `downloadLinks` entry (or `downloadSource`) is a package, `sizeBytes` is used without a request per link, relative links resolve against the catalog folder, and worker links that redirect to a debrid CDN (e.g. TorBox) are followed and refreshed like signed R2 links.
- PS4 tile: if an older (or any other) version of PKG Manager X is already running, the tile now replaces it with the version it bundles, instead of opening the old one. It waits while an install is running and tells you to open the tile again afterwards.
- PS4: HTTP packages of 256 MB and more download to the console over several connections at once (Settings > PS4 download connections: 1, 2 or 3; default 2). The connections take consecutive 32 MB pieces in order, so the console's drive writes almost sequentially. Use 1 if the console lags while downloading.
- Pegasus DL+ catalogs: rescans no longer add the catalog's packages again (they appeared two, three times...). Catalog links live on other hosts than the catalog file, so the scanner now remembers which catalog listed each link.

## 1.0.6 (PKG Manager X)

- PS4 HTTP packages download to console storage before BGFT installs them, avoiding the URL-registration failures encountered partway through some packages.
- Skip queued updates and DLC when their base install fails; allow browsing while installation continues.
- Refresh the PS4 idle power timer only during installation. Actual rest-mode downloading remains unsupported.
- Blank the PS4 manager page after 60 seconds of inactivity during installation; consume the first input to wake safely and restore the page when installation ends.
- Replace an older service through its shutdown API and cover URL/storage registration, queue cleanup, power policy, and screen wake behavior with regression tests.

## Unreleased (PKG Manager X)

### HTTP / HTTPS Sources
- Install packages from web servers with byte-range streaming (PS4 HTTP installs download a temporary copy first)
- Discovery through `index.json` (`tools/make_http_index.py`) or HTML directory listings, including sub-folders
- Basic auth, redirects, keep-alive connection pool for the installer's parallel requests
- HTTPS via mbedTLS with CA verification, certificate pinning ("Trust this certificate") or no check

### PS4 / PS5 Tags
- Every package reports its console, detected from the package itself (FIH / param.json / param.sfo), with a Title ID fallback
- PS4 / PS5 filter in the package browser and badges in the detail view
- `PS4/` and `PS5/` folders on USB drives and discs are scanned like `pkg/`

### PS4 Payload (experimental)
- New `PLATFORM=ps4` build (ps4-payload-sdk, GoldHEN) sharing the UI and all sources
- Installs through BGFT; PS5 packages are listed but refused on PS4
- See docs/PS4.md for the on-console test checklist

### Home Server
- Gateway sources: one address, packages from `api/catalog`, downloads from `files/<path>` (local or a signed Cloudflare R2 redirect)
- Signed links that expire during an install are refreshed and the download continues at the same offset; network drops are retried for up to two minutes
- Sizes are checked against the catalog; files the server no longer has show as "Unavailable" instead of an install error
- nginx `autoindex_format json` listings (sizes and dates without extra requests)
- Mozilla CA list compiled in: HTTPS to Let's Encrypt / R2 hosts works on PS4 and PS5 without copying a CA file
- Relaxed TLS settings (pinned / unchecked certificate) and credentials apply to the source's own host only, never to redirect targets

### Library
- Console and type (Games / DLC / Updates / Homebrew) come from package metadata only; folder names no longer matter
- On PS4: PS5 packages are greyed "PS5 only". On PS5: PS4 games, DLC and updates are installable with a PS4 badge, PS4 homebrew is greyed "PS4 homebrew doesn't run on PS5"
- Type filter and "Hide unavailable" toggle; setting "Allow PS4 installs on PS5" (on by default)
- Install refusals now show their real reason in the browser

### Upstream
- Merged PKG Manager v1.4.1 (offline USB/disc direct-storage installs on PS5, SMB guest login and faster signing, package icons on install cards, Close PKG Manager, quick Rescan)
- Merged PKG Manager v1.3.0 (PS5 install helper process, SMB browse-only shares, paged catalog)

---

## v1.5.1

- Added support for PS5 backport patches that previously appeared as unknown packages
- Show an "SDK target" badge for PS5 packages when the executable's SDK version can be read reliably
- Improved installed-version detection so newer PS5 patches can be installed and already installed updates are correctly recognized

---

## v1.5.0

### Install Queue
- Added a console-owned background install queue for USB, disc, SMB, and browser uploads, allowing browsing across the app while installations run in the background
- Added a dedicated full-page Install Queue view (`#/queue`) with active installation hero, total queue progress, live transfer speed, remaining time, and a scrollable queue list
- Added individual package cancellation, retry for failed sources, and a Clear completed action
- Added an "All done" summary screen when queued installations complete
- Automatically clear finished and errored queue jobs after a 60-minute retention period
- Automatically abort queued packages if an identical or newer version is already installed, preventing blocked install loops
- Replaced the base/update batch modal with independent queue jobs and added a bulk "Install all DLCs" action
- Offline USB and disc packages are submitted sequentially to the PS5 system queue

### PS5 Browser Title Progress
- Display detailed live installation progress in the PS5 browser page title during active installations
- Feature a text-based progress bar (`[■■■■■□□□□□] 50%`) with batch index (`[1/3]`), package name (truncated to 40 characters), transfer speed, and estimated time remaining
- Standard app title is automatically restored when installations complete or the queue is idle

### Title Details & Settings
- Separate and display different base packages for the same title (e.g. different editions or regions) (#27)
- Display file name and relative path for updates in the title detail view (#24)
- Added a setting to show package filename and path in title details (disabled by default)

### Direct Install
- Redesigned Direct Install with package cards showing locally extracted thumbnails, platform/type badges, and "Install" and "Cancel" actions
- Added an "Install all" bulk action to queue all ready packages at once
- Streamlined the interface by removing the folder picker and redundant clear actions

### Bug Fixes
- Fixed multiple Samba locations on the same server share overwriting each other's game catalog info (#32)
- Display clear, readable error messages when installation fails due to insufficient console storage space (#25)

---

## v1.4.1

- Fixed USB and disc installs failing when the console had no active network connection. Installation progress is not shown in PKG Manager in this mode due to a PS5 system limitation.

## v1.4.0

### SMB Network Shares

- Fixed package scanning from the share root so an unreadable subfolder does not prevent scanning accessible folders
- Fixed guest authentication to try an empty-password Guest login, with anonymous fallback when no credentials are supplied
- Fixed page reloads on a Samba share package list showing an internal share ID instead of the share name
- Added clearer errors for disabled accounts and allowed entering share names when the server blocks share listing
- Reduced SMB signing overhead with hardware AES acceleration and reused encryption setup, while preserving signature verification
- Build the pinned, patched SMB library with matching headers instead of using an older SDK copy
- Added Windows 11 guest-sharing setup instructions and a host diagnostic tool for authentication, read verification and transfer benchmarks

### Settings
- Fixed cache statistics showing zero after reloading the Settings page
- Added a Close PKG Manager action that stops the server process gracefully
- Reorganized Settings cards

### Installation
- Fail fast and show a clear error if trying to install a package that no longer exists on storage or Samba
- Show the game title, update version, or DLC label instead of raw title IDs in console install notifications and download cards
- Show the package icon on the console's download cards and notifications during installation

### Cache and Scanning
- Preserve the metadata and icon cache across routine ELF updates; clear and rescan only when the cache schema changes
- Switched the Rescan button to run a fast quick rescan instead of rebuilding the full cache
- Fixed quick rescan dropping unchanged SMB packages when a package was renamed or modified, and added catalog self-healing

---

## v1.3.0

### Installation
- Fixed an issue reported on firmware 9.60 and newer where installing another package required restarting PKG Manager

### SMB Network Shares
- Added folder browsing so packages on large shares can be installed without scanning the whole share first
- Stopped repeated automatic scans of SMB shares
- Split package lists into pages for faster browsing of large libraries
- Keep saved passwords when editing a share

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
