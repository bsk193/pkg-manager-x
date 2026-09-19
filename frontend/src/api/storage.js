export async function getStorage() {
  const res = await fetch('/api/storage');
  if (!res.ok) throw new Error(`Storage fetch failed: ${res.status}`);
  return res.json();
}
