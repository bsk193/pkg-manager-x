import assert from 'node:assert/strict';
import test from 'node:test';
import { refreshPackages, waitForScan, shouldAutoScanDrive } from '../src/api/packages.js';
import { browseSmb, inspectSmb } from '../src/api/smb.js';

const response = (data) => ({ ok: true, json: async () => data });

test('scan retries poll the active job without posting another scan', async (t) => {
  const calls = [];
  let poll = 0;
  t.mock.method(globalThis, 'fetch', async (url, options) => {
    calls.push([url, options?.method || 'GET']);
    if (url.endsWith('/refresh')) return response({ status: 'accepted', started: false });
    if (++poll === 2) throw new Error('Connection interrupted');
    return response({ is_scanning: poll < 4, processed_files: poll < 4 ? 1500 : 3000, total_files: 3000 });
  });
  const updates = [];
  const result = await refreshPackages((status) => updates.push(status));
  assert.equal(result.processed_files, 3000);
  assert.equal(calls.filter(([, method]) => method === 'POST').length, 1);
  assert.equal(updates.length, 3);
});

test('a reloaded browser can attach using status requests only', async (t) => {
  const calls = [];
  t.mock.method(globalThis, 'fetch', async (url) => {
    calls.push(url);
    return response({ is_scanning: false, processed_files: 3000, total_files: 3000 });
  });
  await waitForScan();
  assert.deepEqual(calls, ['/api/scan/status']);
});

test('incomplete scans report an error instead of success or an automatic restart', async (t) => {
  let requests = 0;
  t.mock.method(globalThis, 'fetch', async () => {
    requests++;
    return response({ is_scanning: false, failed_sources: 1 });
  });
  await assert.rejects(waitForScan(), /could not be fully scanned/);
  assert.equal(requests, 1);
});

test('manual browsing sends the cursor and inspects only the selected path', async (t) => {
  const calls = [];
  t.mock.method(globalThis, 'fetch', async (url, options) => {
    calls.push({ url, body: options?.body && JSON.parse(options.body) });
    return response({ success: true, entries: [], can_install: true });
  });
  await browseSmb({ server: 'nas', share: 'Games', path: 'DLC', after: 'F:0255.pkg' });
  const path = 'smb://nas/Games/DLC/A & B #1.pkg';
  const pkg = await inspectSmb(path);
  assert.equal(calls[0].body.after, 'F:0255.pkg');
  assert.equal(calls[1].url, `/api/smb/inspect?path=${encodeURIComponent(path)}`);
  assert.equal(pkg.path, path);
  assert.equal(calls.length, 2);
});

test('automatic polling includes local drives and excludes SMB and all sources', () => {
  assert.equal(shouldAutoScanDrive({ id: 'usb0', type: 'usb' }), true);
  assert.equal(shouldAutoScanDrive({ id: 'disc', type: 'disc' }), true);
  assert.equal(shouldAutoScanDrive({ id: 'smb_large', type: 'smb' }), false);
  assert.equal(shouldAutoScanDrive({ id: '__all__' }), false);
  assert.equal(shouldAutoScanDrive(null), false);
});
