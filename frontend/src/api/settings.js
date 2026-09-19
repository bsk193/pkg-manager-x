export async function getSettings() {
  const res = await fetch('/api/settings');
  if (!res.ok) throw new Error(`Settings fetch failed: ${res.status}`);
  return res.json();
}

export async function saveSettings(settings) {
  const res = await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(settings)
  });
  if (!res.ok) throw new Error(`Settings save failed: ${res.status}`);
  return res.json();
}

export async function installShortcut() {
  const res = await fetch('/api/shortcut/install', { method: 'POST' });
  if (!res.ok) throw new Error(`Shortcut install failed: ${res.status}`);
  return res.json();
}
