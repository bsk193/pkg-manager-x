export async function getPackages(driveId) {
  let url = '/api/packages';
  if (driveId && driveId !== '__all__') {
    url += `?drive=${encodeURIComponent(driveId)}`;
  }
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Packages fetch failed: ${res.status}`);
  return res.json();
}

export async function refreshPackages() {
  const res = await fetch('/api/packages/refresh', { method: 'POST' });
  if (!res.ok) throw new Error(`Refresh failed: ${res.status}`);
  return res.json();
}

export async function getScanStatus() {
  const res = await fetch('/api/scan/status');
  if (!res.ok) throw new Error(`Scan status failed: ${res.status}`);
  return res.json();
}

export async function quickScan(driveId) {
  let url = '/api/packages/quick-scan';
  if (driveId && driveId !== '__all__') {
    url += `?drive=${encodeURIComponent(driveId)}`;
  }
  const res = await fetch(url, { method: 'POST' });
  if (!res.ok) throw new Error(`Quick scan failed: ${res.status}`);
  return res.json();
}
