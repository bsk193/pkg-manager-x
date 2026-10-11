# PKG Manager X 1.0.6 — development handoff

Updated 11 October 2026. This is the final state, superseding the earlier repair/testing notes.

## Release state

- [1.0.6 is published as Latest stable](https://github.com/bsk193/pkg-manager-x/releases/tag/x-v1.0.6), not a draft or prerelease.
- Tag `x-v1.0.6` points to main merge commit `79136a785ea9289a0a785b9bdbff8ae165a8e227`, from [PR #2](https://github.com/bsk193/pkg-manager-x/pull/2).
- [Release workflow 38133464308](https://github.com/bsk193/pkg-manager-x/actions/runs/38133464308) passed every job: host tests, PS4 registration/power tests, frontend tests/build, PS4/PS5 payloads, and PS4 tile.
- All three published asset SHA-256 digests match the downloaded CI artifacts. The PS4 tile package version is `01.06`.
- The user reported that the console test worked and later reconfirmed that everything appeared to be working. Do not treat this as independent verification across all firmware versions.

## What changed

Claude's branch `fix/queue-after-failed-base` stopped at `cbaabca`, adding PS4 HTTP download-first installation. CI still expected the old URL-only export lookup and failed. The repair preserved that work and corrected the regression tests.

- HTTP/HTTPS packages on PS4 download to `/data/pkgmgr/dl`, then BGFT installs from storage. Temporary files are cleaned up afterward. Space is needed for both the package and installed game. Local/USB/SMB behavior is unchanged.
- Failed base installs skip dependent queued updates/DLC. Back navigation permits browsing during installation.
- A newer payload asks an older manager to shut down through its API before replacing it.
- `src/ps4_power.c`, called from `src/main.c`, refreshes the PS4 idle power timer every five seconds while `is_installing` is true. It stops when idle and does not alter saved console settings.
- `frontend/src/utils/installScreenSaver.js` and `hooks/useInstallScreenSaver.js` blank the PS4 manager page after 60 seconds without input during installation. Input restores it; the wake input and held-button release are consumed to avoid activating controls underneath. Install completion also restores the page. Desktop/phone browsers are unaffected.
- **Actual rest-mode downloading is unsupported.** Manual rest mode is not blocked. The black screen is a page overlay, not system suspend; it does not cover the home screen or another app.

Relevant commits: `de2acd1` (registration tests), `71840ee` (power/black screen), `3a0698d` (held wake input), `984a0cb` (storage documentation). Claude's earlier implementation commits are included in PR #2.

## Validation and deployment

- All 24 frontend tests passed. Native regression tests cover URL versus local storage registration, optional exports, task cleanup/error propagation, and the power policy. Screen-saver tests cover timing, wake suppression, held keys, gamepad wake, and cleanup.
- On the user's PS4, the new download path passed the old RE2 failure offset (12,269,322,240 bytes) without that error. Earlier completion details were not independently captured; the user subsequently reported successful operation.
- The final code test payload `1.0.5-7-g3a0698d` was loaded after confirming no active install or queue. A Village download showed the `[POWER]` idle-timer refresh log without interrupting the transfer.
- The user set the Applications power-saving timer to 20 minutes and tested with the controller off, then reported "all worked" and authorized release.
- The assistant last verified the console running the test payload. The user subsequently said they handled the update themselves; the installed stable tile/payload version has not been independently rechecked. Do not replace a payload while an install is active.

## Continuing in the other session

Pull the latest `main` in the existing project checkout after inspecting local changes. Read this file, [PS4 behavior and limitations](PS4.md), and the [changelog](../CHANGELOG.md). Preserve any uncommitted VM work; do not reset it to this checkout.

The VM checkout and Personal vault were not edited by this repair session. The shared, versioned handoff is this repository document. The earlier Claude branch contains the initial registration-test repair but is not the authoritative final release branch; use `main` / `x-v1.0.6`.

No further code fix is known to be required for 1.0.6. Any future rest-mode implementation must account for payload suspension and the previously observed GoldHEN kernel panics; do not label the current keep-awake behavior as rest-mode download support.
