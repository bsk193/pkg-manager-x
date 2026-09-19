export async function getCacheStats() {
  const res = await fetch('/api/cache/stats');
  if (!res.ok) throw new Error(`Cache stats failed: ${res.status}`);
  return res.json();
}

export async function clearCache() {
  const res = await fetch('/api/cache/clear', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({})
  });
  if (!res.ok) throw new Error(`Cache clear failed: ${res.status}`);
  return res.json();
}
