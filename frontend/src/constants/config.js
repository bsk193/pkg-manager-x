export const BUILD_VERSION = typeof __APP_VERSION__ !== 'undefined' ? __APP_VERSION__ : '1.1.0';
export const BUILD_COMMIT = typeof __APP_COMMIT__ !== 'undefined' ? __APP_COMMIT__ : '';
export const BUILD_DATE = typeof __APP_BUILD_DATE__ !== 'undefined' ? __APP_BUILD_DATE__ : '';
// PKG Manager X: the upstream PKG Manager release this build is based on.
export const UPSTREAM_VERSION = typeof __APP_UPSTREAM_VERSION__ !== 'undefined' ? __APP_UPSTREAM_VERSION__ : '';
export const FORK_REPO_URL = 'https://github.com/bsk193/pkg-manager-x';
export const UPSTREAM_REPO_URL = 'https://github.com/itsPLK/ps5-pkg-manager';

export const DONATE_URL = 'https://github.com/itsPLK/ps5-pkg-manager/blob/main/DONATE.md';
export const isPlayStation = typeof navigator !== 'undefined' && /PlayStation/i.test(navigator.userAgent);
export const DONATE_MODAL_STORAGE_KEY = 'pkgmgr_donate_popup';
export const DONATE_MODAL_INTERVAL_MS = 7 * 24 * 60 * 60 * 1000; // 7 days

export const ALL_SOURCES_DRIVE = {
  id: '__all__',
  label: 'All Sources',
  path: 'All storage media',
  type: 'all',
  clickable: true
};
