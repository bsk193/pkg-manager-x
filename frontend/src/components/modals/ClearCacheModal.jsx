import React from 'react';
import { formatBytes } from '../../utils/formatters';

export default function ClearCacheModal({ show, cacheStats, onConfirm, onClose, clearing }) {
  if (!show) return null;

  const cachePath = (cacheStats && cacheStats.cache_path) ? cacheStats.cache_path : '/data/pkgmgr/cache';

  return (
    <div data-modal-dialog="true" role="dialog" aria-modal="true" className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
      <div className="bg-[#181a27] border border-rose-500/30 rounded-[2px] max-w-md w-full p-6 space-y-5">
        <div className="flex items-center space-x-3">
          <div className="w-10 h-10 rounded-[2px] bg-rose-600/20 border border-rose-500/40 flex items-center justify-center text-rose-400 shrink-0">
            <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
          </div>
          <div>
            <h3 className="text-lg font-bold text-white">Clear Package Cache?</h3>
            <p className="text-xs text-zinc-400">Remove cached metadata & icons from console storage</p>
          </div>
        </div>

        <p className="text-xs text-zinc-300 leading-relaxed">
          This will delete all cached metadata and icons stored at <span className="font-mono text-blue-300">{cachePath}</span>:
        </p>

        {cacheStats && cacheStats.total_count > 0 && (
          <div className="bg-black/40 border border-white/10 rounded-[2px] p-3 space-y-1.5 text-xs font-mono">
            <div className="flex items-center justify-between text-zinc-300">
              <span>Packages Cached:</span>
              <span className="text-white font-bold">{cacheStats.total_count}</span>
            </div>
            <div className="pt-2 border-t border-white/10 flex items-center justify-between font-bold text-white">
              <span>Total Space Freed:</span>
              <span className="text-rose-400">{formatBytes(cacheStats.total_bytes)}</span>
            </div>
          </div>
        )}

        <p className="text-[11px] text-zinc-400">
          Your original <span className="font-mono text-zinc-300">.pkg</span> files will not be deleted. The cache will automatically rebuild as you browse packages.
        </p>

        <div className="flex items-center justify-end space-x-3 pt-2">
          <button
            type="button"
            onClick={onClose}
            disabled={clearing}
            className="px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-300 text-xs font-semibold transition-colors cursor-pointer disabled:opacity-50"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={onConfirm}
            disabled={clearing}
            className="px-4 py-2 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-xs font-bold transition-colors cursor-pointer disabled:opacity-50 flex items-center space-x-2"
          >
            {clearing ? (
              <>
                <div className="ps5-robust-spinner-sm" />
                <span>Clearing...</span>
              </>
            ) : (
              <span>Clear Cache</span>
            )}
          </button>
        </div>
      </div>
    </div>
  );
}
