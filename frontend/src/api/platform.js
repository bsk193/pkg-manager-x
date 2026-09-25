// Console this payload runs on. Older backends without the route are PS5.
export const DEFAULT_PLATFORM_INFO = {
  console: 'ps5',
  can_install: ['ps4', 'ps5'],
  https_supported: false,
  shortcut_supported: true
};

export async function getPlatformInfo() {
  try {
    const res = await fetch('/api/platform');
    if (!res.ok) return DEFAULT_PLATFORM_INFO;
    return { ...DEFAULT_PLATFORM_INFO, ...(await res.json()) };
  } catch (e) {
    return DEFAULT_PLATFORM_INFO;
  }
}
