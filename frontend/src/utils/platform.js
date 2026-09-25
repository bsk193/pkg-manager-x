// Package target platform / content type helpers. The backend reports
// `platform` ("ps4" / "ps5" / ""), `content_type` and `platform_blocked` /
// `platform_reason` from the package metadata; the fallbacks below only
// cover older backends and caches.

export function titleIdPlatform(titleId) {
  const tid = (titleId || '').trim().toUpperCase();
  if (tid.startsWith('PPSA')) return 'ps5';
  if (tid.startsWith('CUSA')) return 'ps4';
  return '';
}

export function pkgPlatform(pkg) {
  if (!pkg) return '';
  if (pkg.platform === 'ps4' || pkg.platform === 'ps5') return pkg.platform;
  return titleIdPlatform(pkg.title_id);
}

// Platform of a title group: base package first, then any package that knows.
export function groupPlatform(items, base) {
  const fromBase = pkgPlatform(base);
  if (fromBase) return fromBase;
  for (const p of items || []) {
    const plat = pkgPlatform(p);
    if (plat) return plat;
  }
  return '';
}

export function platformLabel(platform) {
  return platform === 'ps5' ? 'PS5' : platform === 'ps4' ? 'PS4' : '';
}

// "game" / "dlc" / "update" / "homebrew".
export function pkgContentType(pkg) {
  if (!pkg) return 'game';
  if (pkg.content_type) return pkg.content_type;
  if (pkg.pkg_type === 'update') return 'update';
  if (pkg.pkg_type === 'dlc') return 'dlc';
  return 'game';
}

export const CONTENT_TYPE_LABELS = {
  game: 'Game',
  dlc: 'DLC',
  update: 'Update',
  homebrew: 'Homebrew'
};

export const CONTENT_TYPE_FILTERS = [
  { id: 'all', label: 'All' },
  { id: 'game', label: 'Games' },
  { id: 'dlc', label: 'DLC' },
  { id: 'update', label: 'Updates' },
  { id: 'homebrew', label: 'Homebrew' }
];

// PS5 installs PS4 and PS5 packages; PS4 installs PS4 packages only.
// (Fallback for backends that do not send platform_blocked.)
export function canInstallOnConsole(consoleName, platform) {
  return !(consoleName === 'ps4' && platform === 'ps5');
}

// Why this console cannot install the package, or '' when it can.
export function pkgBlockedReason(pkg, consoleName) {
  if (!pkg) return '';
  if (typeof pkg.platform_blocked === 'boolean') {
    return pkg.platform_blocked ? (pkg.platform_reason || 'Not supported on this console') : '';
  }
  return canInstallOnConsole(consoleName, pkgPlatform(pkg)) ? '' : 'PS5 only';
}

// Short card label for a block reason.
export function blockedBadge(reason, platform) {
  if (!reason) return '';
  if (/homebrew/i.test(reason)) return 'PS4 homebrew';
  if (/turned off/i.test(reason)) return 'PS4 off';
  return `${platformLabel(platform) || 'PS5'} only`;
}

// Greyed out: not installable on this console, or gone from the server.
export function pkgGreyed(pkg, consoleName) {
  return !!(pkg && (pkg.unavailable || pkgBlockedReason(pkg, consoleName)));
}

export const PLATFORM_FILTER_STORAGE_KEY = 'pkgmgr_platform_filter';
export const TYPE_FILTER_STORAGE_KEY = 'pkgmgr_type_filter';
export const HIDE_GREYED_STORAGE_KEY = 'pkgmgr_hide_greyed';
