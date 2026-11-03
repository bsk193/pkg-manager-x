import React from 'react';
import { sdkTarget } from '../utils/pkgSdk.js';

export default function SdkBadge({ pkg }) {
  const target = pkg?.title_id?.startsWith('PPSA') ? sdkTarget(pkg.sdk_version) : '';
  if (!target) return null;
  return <span className="inline-block text-[10px] font-mono px-2 py-0.5 rounded-[2px] bg-cyan-950/70 text-cyan-200 border border-cyan-700/40 shrink-0">
    SDK target: {target}
  </span>;
}
