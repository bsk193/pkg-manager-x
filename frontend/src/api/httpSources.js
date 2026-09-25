export async function getHttpSources() {
  const res = await fetch('/api/http/sources');
  if (!res.ok) throw new Error(`HTTP sources fetch failed: ${res.status}`);
  const data = await res.json();
  return Array.isArray(data) ? data : [];
}

export async function saveHttpSources(sources) {
  const res = await fetch('/api/http/sources', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ sources })
  });
  const data = await res.json().catch(() => ({}));
  if (!res.ok || !data.success) {
    throw new Error(data.error || `HTTP sources save failed: ${res.status}`);
  }
  return data;
}

export async function testHttpSource(config) {
  const res = await fetch('/api/http/test', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config)
  });
  if (!res.ok && res.status !== 400) throw new Error(`HTTP source test failed: ${res.status}`);
  return res.json();
}
