// Package target platform helpers. The backend reports `platform`
// ("ps4" / "ps5" / "") from the package itself; the Title ID prefix is only
// a fallback for older backends and caches.

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

// PS5 installs PS4 and PS5 packages; PS4 installs PS4 packages only.
export function canInstallOnConsole(consoleName, platform) {
  return !(consoleName === 'ps4' && platform === 'ps5');
}

export const PLATFORM_FILTER_STORAGE_KEY = 'pkgmgr_platform_filter';
