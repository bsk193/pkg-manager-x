import { BUILD_VERSION, BUILD_COMMIT, BUILD_DATE } from '../constants/config';

export function getBrowserTitle(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager v${v} (${BUILD_COMMIT}, ${BUILD_DATE}) by PLK`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager v${v} (${match[1]}, ${match[2]}) by PLK`;
    }
  }
  return `PKG Manager v${v} by PLK`;
}

export function getFullVersion(ver) {
  const raw = ver || BUILD_VERSION;
  const v = String(raw).trim().replace(/^v+/i, '') || BUILD_VERSION;
  if (BUILD_COMMIT && BUILD_DATE) {
    return `PKG Manager v${v} (${BUILD_COMMIT}, ${BUILD_DATE})`;
  }
  if (typeof document !== 'undefined' && document.title) {
    const match = document.title.match(/\(([^,]+),\s*([^)]+)\)/);
    if (match) {
      return `PKG Manager v${v} (${match[1]}, ${match[2]})`;
    }
  }
  if (BUILD_COMMIT) {
    return `PKG Manager v${v} (${BUILD_COMMIT})`;
  }
  return `PKG Manager v${v}`;
}
