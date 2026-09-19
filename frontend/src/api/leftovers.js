export async function scanLeftovers() {
  const res = await fetch('/api/leftovers');
  if (!res.ok) throw new Error(`Leftovers scan failed: ${res.status}`);
  return res.json();
}

export async function deleteLeftover(titleId) {
  const res = await fetch('/api/leftovers/delete', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ title_id: titleId })
  });
  if (!res.ok) throw new Error(`Leftover delete failed: ${res.status}`);
  return res.json();
}
