import assert from 'node:assert/strict';
import test from 'node:test';
import { parseLocalPkg } from '../src/utils/parseLocalPkg.js';
import { sdkTarget } from '../src/utils/pkgSdk.js';

const encode = new TextEncoder();
function put64(view, offset, value) {
  view.setUint32(offset, value % 4294967296, true);
  view.setUint32(offset + 4, Math.floor(value / 4294967296), true);
}

// Sparse virtual files cover offsets above 4 GB without allocating game-sized blobs.
function image({ lih = true, flags = 0, sdk = 0x04000031, sdkOffset = 0x24000, cnt = 0x80000 } = {}) {
  const header = new Uint8Array(128);
  const hv = new DataView(header.buffer);
  header.set(encode.encode(lih ? '\x7fLIH' : '\x7fFIH'));
  hv.setUint16(6, lih ? 1 : 3, true);
  put64(hv, lih ? 0x30 : 0x58, cnt);
  const inner = new Uint8Array(128);
  inner.set(encode.encode('\x7fFIH'));
  put64(new DataView(inner.buffer), 0x10, 0x10000);
  put64(hv, lih ? 0x20 : 0x10, lih ? 0x10000 : 0x20000);
  const self = new Uint8Array(8192);
  const sv = new DataView(self.buffer);
  sv.setUint32(0, 0xeef51454, true);
  const offset = sdkOffset - 0x20000 - 32;
  put64(sv, 16, offset + 0x1000);
  sv.setUint16(24, 1, true);
  put64(sv, 32, 0x804);
  put64(sv, 40, offset);
  put64(sv, 48, 0x1000);
  put64(sv, 56, 0x1000);
  self.set([0x7f, 0x45, 0x4c, 0x46, 2, 1, 1], 64);
  sv.setUint16(64 + 18, 62, true);
  put64(sv, 64 + 32, 64);
  sv.setUint16(64 + 54, 56, true);
  sv.setUint16(64 + 56, 2, true);
  sv.setUint32(128, 1, true);
  put64(sv, 128 + 8, 0x1000);
  put64(sv, 128 + 32, 0x1000);
  sv.setUint32(184, 0x61000001, true);
  put64(sv, 184 + 8, 0x1020);
  put64(sv, 184 + 32, 32);
  const proc = new Uint8Array(32);
  const pv = new DataView(proc.buffer);
  put64(pv, 0, 32);
  pv.setUint32(8, 0x4942524f, true);
  pv.setUint32(20, sdk, true);
  const meta = new Uint8Array(1024);
  const mv = new DataView(meta.buffer);
  meta.set(encode.encode('\x7fCNT'));
  mv.setUint32(0x78, flags, false);
  mv.setUint32(0x10, 1, false);
  mv.setUint32(0x18, 0x80, false);
  mv.setUint32(0x80, 0x2000, false);
  mv.setUint32(0x90, 0x100, false);
  const json = encode.encode(JSON.stringify({
    titleId: 'PPSA90091', titleName: 'Synthetic Image', applicationDrmType: 'standard',
    originContentVersion: '07.000.003', targetContentVersion: '07.000.003',
    contentVersion: '07.000.004', sdkVersion: '0x1200000000000000'
  }));
  mv.setUint32(0x94, json.length, false);
  meta.set(json, 0x100);
  const regions = [[0, header], [0x10000, inner], [0x20000, self], [sdkOffset, proc], [cnt, meta]];
  const reads = [];
  const file = {
    name: 'synthetic.pkg', size: cnt + meta.length,
    slice(start, end) {
      reads.push([start, end]);
      return { async arrayBuffer() {
        const bytes = new Uint8Array(end - start);
        for (const [offset, region] of regions) {
          const lo = Math.max(start, offset), hi = Math.min(end, offset + region.length);
          if (lo < hi) bytes.set(region.subarray(lo - offset, hi - offset), lo - start);
        }
        return bytes.buffer;
      } };
    }
  };
  return { file, hv, sv, pv, self, reads };
}

test('LIH reads its package version and executable SDK, independently of JSON SDK', async () => {
  for (const sdk of [0x04000031, 0x05000033]) {
    const fixture = image({ sdk });
    const result = await parseLocalPkg(fixture.file);
    assert.equal(result.pkg_type, 'update');
    assert.equal(result.app_version, 'v07.000.004');
    assert.equal(result.sdk_version, sdk);
    assert.equal(result.title_name, 'Synthetic Image');
    assert.ok(fixture.reads.reduce((sum, [a, b]) => sum + b - a, 0) < 150000);
  }
});

test('FIH recognizes all patch flags and retains base classification without patch flags', async () => {
  for (const flags of [0x00100000, 0x40000000, 0x41000000, 0x60000000]) {
    const result = await parseLocalPkg(image({ lih: false, flags, sdk: 0x11000040 }).file);
    assert.equal(result.pkg_type, 'update');
    assert.equal(sdkTarget(result.sdk_version), '11.xx');
  }
  assert.equal((await parseLocalPkg(image({ lih: false }).file)).pkg_type, 'base');
});

test('SDK inspection uses targeted reads beyond 4 GB', async () => {
  const sdkOffset = 4294967296 + 0x24000;
  const fixture = image({ sdkOffset, cnt: sdkOffset + 0x10000 });
  const result = await parseLocalPkg(fixture.file);
  assert.equal(result.sdk_version, 0x04000031);
  assert.equal(result.pkg_type, 'update');
  assert.ok(fixture.reads.some(([offset, end]) => offset === sdkOffset && end - offset === 24));
  assert.ok(fixture.reads.length < 30);
});

test('unreadable or malformed executable metadata omits SDK without losing patch metadata', async () => {
  for (const mutate of [
    (f) => put64(f.sv, 32, 0x806),
    (f) => put64(f.sv, 32, 0x80c),
    (f) => f.pv.setUint32(8, 0, true),
    (f) => f.pv.setUint32(20, 0xfa000001, true),
    (f) => put64(f.sv, 40, Number.MAX_SAFE_INTEGER),
    (f) => put64(f.sv, 64 + 32, Number.MAX_SAFE_INTEGER),
    (f) => f.sv.setUint32(184, 0x61000002, true),
    (f) => { f.sv.setUint16(64 + 56, 3, true); f.self.set(f.self.subarray(184, 240), 240); },
  ]) {
    const fixture = image();
    mutate(fixture);
    const result = await parseLocalPkg(fixture.file);
    assert.equal(result.pkg_type, 'update');
    assert.equal(result.app_version, 'v07.000.004');
    assert.equal(result.sdk_version, 0);
  }
});

test('LIH rejects unsupported versions and out-of-range CNT offsets', async () => {
  const badVersion = image();
  badVersion.hv.setUint16(6, 2, true);
  await assert.rejects(parseLocalPkg(badVersion.file), /Unsupported LIH version/);
  const badOffset = image();
  badOffset.hv.setUint32(0x30, 0xffffffff, true);
  badOffset.hv.setUint32(0x34, 0xffffffff, true);
  await assert.rejects(parseLocalPkg(badOffset.file), /Invalid package metadata offset/);
});

test('SDK badge formatting decodes decimal SDK families and suppresses unknown values', () => {
  assert.equal(sdkTarget(0x04000031), '4.xx');
  assert.equal(sdkTarget(0x05000033), '5.xx');
  assert.equal(sdkTarget(0x11000040), '11.xx');
  for (const value of [0, -1, undefined, '0x04000031', 0xfa000001, 0x100000000]) {
    assert.equal(sdkTarget(value), '');
  }
});
