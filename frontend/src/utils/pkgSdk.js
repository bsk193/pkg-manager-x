function u64(view, offset) {
  const value = view.getUint32(offset, true) + view.getUint32(offset + 4, true) * 4294967296;
  return Number.isSafeInteger(value) ? value : NaN;
}

function fits(offset, size, limit) {
  return Number.isSafeInteger(offset) && Number.isSafeInteger(size) &&
    offset >= 0 && size >= 0 && offset <= limit && size <= limit - offset;
}

export function sdkTarget(value) {
  if (!Number.isInteger(value) || value <= 0 || value > 0xffffffff) return '';
  const major = value >>> 24;
  const tens = major >>> 4;
  const ones = major & 15;
  return major && tens <= 9 && ones <= 9 ? `${tens * 10 + ones}.xx` : '';
}

// Optional SDK probe: block-aligned SELF headers in the first 1 MB of image data,
// followed by a targeted read of the main executable's process parameters.
// Unsupported, encrypted, compressed or ambiguous layouts leave the badge absent.
export async function readPkgSdk(read, header, cnt) {
  try {
    const root = new DataView(header.buffer, header.byteOffset, header.byteLength);
    let start;
    if (header[1] === 0x46) {
      start = u64(root, 0x10);
    } else if (header[1] === 0x4c) {
      const inner = u64(root, 0x20);
      if (!inner || !fits(inner, 128, cnt)) return 0;
      const fih = await read(inner, 128);
      if (fih[0] !== 0x7f || fih[1] !== 0x46 || fih[2] !== 0x49 || fih[3] !== 0x48) return 0;
      const relative = u64(new DataView(fih.buffer, fih.byteOffset, fih.byteLength), 0x10);
      if (!relative || !fits(inner, relative, cnt)) return 0;
      start = inner + relative;
    } else return 0;
    if (!start || !fits(start, 1, cnt)) return 0;
    let sdk = 0;
    let mainCount = 0;
    for (let block = 0; block < 16; block++) {
      const base = start + block * 0x10000;
      if (!fits(base, 32, cnt)) break;
      const bytes = await read(base, Math.min(8192, cnt - base));
      const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      if (v.getUint32(0, true) !== 0xeef51454) continue;
      const selfSize = u64(v, 16);
      const segments = v.getUint16(24, true);
      const elf = 32 + segments * 32;
      if (!segments || segments > 64 || !fits(base, selfSize, cnt) ||
          selfSize < elf + 64 || !fits(elf, 64, bytes.length)) continue;
      if (v.getUint32(elf, false) !== 0x7f454c46 || bytes[elf + 4] !== 2 ||
          bytes[elf + 5] !== 1 || bytes[elf + 6] !== 1 || v.getUint16(elf + 18, true) !== 62) continue;
      const phoff = u64(v, elf + 32);
      const count = v.getUint16(elf + 56, true);
      if (v.getUint16(elf + 54, true) !== 56 || !count || count > 64 || phoff < 64 ||
          !fits(elf, phoff, bytes.length) || !fits(elf + phoff, count * 56, bytes.length)) continue;
      const ph = elf + phoff;
      for (let p = 0; p < count; p++) {
        const param = ph + p * 56;
        if (v.getUint32(param, true) !== 0x61000001) continue;
        if (++mainCount > 1) return 0;
        const poff = u64(v, param + 8);
        const plen = u64(v, param + 32);
        if (!Number.isSafeInteger(plen) || plen < 24) return 0;
        for (let l = 0; l < count; l++) {
          const load = ph + l * 56;
          const loff = u64(v, load + 8);
          const llen = u64(v, load + 32);
          if (v.getUint32(load, true) !== 1 || poff < loff || !fits(poff - loff, plen, llen)) continue;
          for (let s = 0; s < segments; s++) {
            const segment = 32 + s * 32;
            const flags = u64(v, segment);
            const offset = u64(v, segment + 8);
            const stored = u64(v, segment + 16);
            const unpacked = u64(v, segment + 24);
            if (Math.floor(flags / 1048576) !== l || !(flags & 0x800)) continue;
            if ((flags & 0xa) || stored !== unpacked || offset < elf + phoff + count * 56 || !fits(offset, stored, selfSize) ||
                !fits(poff - loff, plen, stored)) return 0;
            const proc = await read(base + offset + (poff - loff), 24);
            const pv = new DataView(proc.buffer, proc.byteOffset, proc.byteLength);
            const structSize = u64(pv, 0);
            if (pv.getUint32(8, true) !== 0x4942524f || !Number.isSafeInteger(structSize) ||
                structSize < 24 || structSize > plen) return 0;
            const value = pv.getUint32(20, true);
            if (!sdkTarget(value)) return 0;
            sdk = value;
          }
        }
      }
    }
    return mainCount === 1 ? sdk : 0;
  } catch {
    return 0;
  }
}
