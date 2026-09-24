export async function getPackages(driveId) {
  let url = '/api/packages';
  if (driveId && driveId !== '__all__') {
    url += `?drive=${encodeURIComponent(driveId)}`;
  }
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Packages fetch failed: ${res.status}`);
  return res.json();
}

export async function waitForScan(onProgress = () => {}) {
  let failures = 0;
  for (;;) {
    try {
      const status = await getScanStatus();
      failures = 0;
      onProgress(status);
      if (!status.is_scanning) {
        if (status.failed_sources) throw new Error(`${status.failed_sources} share(s) could not be fully scanned. Check the connection and retry.`);
        return status;
      }
    } catch (error) {
      if (++failures >= 3 || /could not be fully scanned/.test(error.message)) throw error;
    }
    await new Promise((resolve) => setTimeout(resolve, 750));
  }
}

export async function refreshPackages(onProgress) {
  const res = await fetch('/api/packages/refresh', { method: 'POST' });
  if (!res.ok) throw new Error(`Refresh failed: ${res.status}`);
  const result = await res.json();
  if (result.status === 'accepted') return waitForScan(onProgress);
  return result;
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

// Network discovery is explicit; a large share must not be walked every 15 seconds.
export function shouldAutoScanDrive(drive) {
  return Boolean(drive && drive.id && drive.id !== '__all__' && drive.type !== 'smb');
}
