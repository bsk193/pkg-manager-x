import { BUILD_VERSION, BUILD_COMMIT, BUILD_DATE, UPSTREAM_VERSION } from '../constants/config';

// "PKG Manager X v1.1.0 (abc1234, date) - based on PKG Manager v1.2.4 by PLK".
// The (commit, date) group must stay the first parenthesized "a, b" pair: it
// is parsed back from document.title below.
const BASED_ON = UPSTREAM_VERSION ? ` - based on PKG Manager v${UPSTREAM_VERSION} by PLK` : ' - based on PKG Manager by PLK';

export function getBrowserTitle(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager X v${v} (${BUILD_COMMIT}, ${BUILD_DATE})${BASED_ON}`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,()]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager X v${v} (${match[1]}, ${match[2]})${BASED_ON}`;
    }
  }
  return `PKG Manager X v${v}${BASED_ON}`;
}

export function getFullVersion(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager X v${v} (${BUILD_COMMIT}, ${BUILD_DATE})`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,()]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager X v${v} (${match[1]}, ${match[2]})`;
    }
  }
  if (BUILD_COMMIT) {
    return `PKG Manager X v${v} (${BUILD_COMMIT})`;
  }
  return `PKG Manager X v${v}`;
}

export function getUpstreamVersionLabel() {
  return UPSTREAM_VERSION ? `based on PKG Manager v${UPSTREAM_VERSION} by PLK` : 'based on PKG Manager by PLK';
}

export function getLocalizedTitle(pkg) {
  if (!pkg) return '';
  let loc = pkg.localized_titles;
  if (typeof loc === 'string' && loc.trim().startsWith('{')) {
    try { loc = JSON.parse(loc); } catch (e) { loc = null; }
  }
  if (!loc || typeof loc !== 'object' || Object.keys(loc).length === 0) {
    return pkg.title_name || '';
  }

  const navLangs = (typeof navigator !== 'undefined' && Array.isArray(navigator.languages) && navigator.languages.length > 0)
    ? navigator.languages
    : [(typeof navigator !== 'undefined' && (navigator.language || navigator.userLanguage)) || 'en-US'];

  // 1. Exact match in browser languages (e.g. 'en-US', 'ar-AE', 'ru-RU')
  for (const l of navLangs) {
    if (!l) continue;
    const cleanL = l.trim().toLowerCase();
    for (const [k, v] of Object.entries(loc)) {
      if (k.toLowerCase() === cleanL && v) return v;
    }
  }

  // 2. Primary language match (e.g. 'en' matches 'en-US', 'ar' matches 'ar-AE', 'ru' matches 'ru-RU')
  for (const l of navLangs) {
    if (!l) continue;
    const primary = l.split('-')[0].split('_')[0].toLowerCase();
    for (const [k, v] of Object.entries(loc)) {
      const kPrimary = k.split('-')[0].split('_')[0].toLowerCase();
      if (kPrimary === primary && v) return v;
    }
  }

  // 3. Fallback to default_language from package (e.g. 'en-US')
  if (pkg.default_language) {
    const defLang = pkg.default_language.trim().toLowerCase();
    for (const [k, v] of Object.entries(loc)) {
      if (k.toLowerCase() === defLang && v) return v;
    }
    const defPrimary = defLang.split('-')[0].split('_')[0];
    for (const [k, v] of Object.entries(loc)) {
      if (k.split('-')[0].split('_')[0].toLowerCase() === defPrimary && v) return v;
    }
  }

  // 4. Fallback to English
  for (const [k, v] of Object.entries(loc)) {
    if (k.toLowerCase().startsWith('en') && v) return v;
  }

  // 5. Fallback to existing title_name if not generic
  if (pkg.title_name && pkg.title_name !== 'Unknown Package' && pkg.title_name !== 'Package') {
    return pkg.title_name;
  }

  // 6. First localized title
  const first = Object.values(loc)[0];
  return first || pkg.title_name || '';
}
