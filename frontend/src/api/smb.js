export async function testSmb(config) {
  const res = await fetch('/api/smb/test', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config)
  });
  if (!res.ok) throw new Error(`SMB test failed: ${res.status}`);
  return res.json();
}
