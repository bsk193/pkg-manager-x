export async function checkVersion() {
  const res = await fetch('/api/version');
  if (!res.ok) throw new Error(`Version check failed: ${res.status}`);
  const text = await res.text();
  if (text.toLowerCase().indexOf('<!doctype') !== -1 || !text.trim()) {
    throw new Error('Invalid version response');
  }
  return text.trim();
}
