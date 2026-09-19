export async function getDrives() {
  const res = await fetch('/api/drives');
  if (!res.ok) throw new Error(`Drives fetch failed: ${res.status}`);
  return res.json();
}
