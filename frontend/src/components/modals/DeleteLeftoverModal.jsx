import React from 'react';
import { formatBytes } from '../../utils/formatters';

export default function DeleteLeftoverModal({ show, item, onConfirm, onClose, deleting }) {
  if (!show || !item) return null;

  return (
    <div data-modal-dialog="true" role="dialog" aria-modal="true" className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
      <div className="bg-[#181a27] border border-rose-500/30 rounded-[2px] max-w-lg w-full p-6 space-y-5">
        <div className="flex items-center space-x-3">
          <div className="w-12 h-12 rounded-[2px] bg-rose-600/20 border border-rose-500/40 flex items-center justify-center text-rose-400 shrink-0">
            <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
            </svg>
          </div>
          <div className="min-w-0 flex-1">
            <h3 className="text-lg font-bold text-white truncate">
              Delete Orphaned Leftovers?
            </h3>
            <p className="text-xs text-zinc-400">
              {item.title_name} ({item.title_id})
            </p>
          </div>
        </div>

        <div className="space-y-2">
          <p className="text-xs text-zinc-300 leading-relaxed">
            The base package for this title is not installed. Deleting will free{' '}
            <strong className="text-rose-400 font-mono font-bold">
              {formatBytes(item.total_size)}
            </strong>{' '}
            from console internal storage.
          </p>
          <p className="text-xs font-semibold text-zinc-400">
            The following files and directories will be permanently deleted:
          </p>
        </div>

        {/* Monospace exact paths list */}
        <div className="bg-black/60 border border-white/10 rounded-[2px] p-3 max-h-48 overflow-y-auto space-y-1.5 font-mono text-[11px] text-zinc-300 select-all">
          {item.paths && item.paths.length > 0 ? (
            item.paths.map((p, idx) => (
              <div key={idx} className="flex items-start space-x-2 text-rose-300/90 break-all">
                <span className="text-rose-500/70 select-none">✕</span>
                <span>{p}</span>
              </div>
            ))
          ) : (
            <div className="text-zinc-500 italic">No paths recorded.</div>
          )}
        </div>

        <p className="text-[11px] text-zinc-400">
          This action cannot be undone.
        </p>

        <div className="flex items-center justify-end space-x-3 pt-2">
          <button
            type="button"
            onClick={onClose}
            disabled={deleting}
            className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-300 text-xs font-semibold transition-colors cursor-pointer disabled:opacity-50"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={onConfirm}
            disabled={deleting}
            className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-xs font-bold transition-colors cursor-pointer disabled:opacity-50 flex items-center space-x-2"
          >
            {deleting ? (
              <>
                <div className="ps5-robust-spinner-sm" />
                <span>Deleting Leftovers...</span>
              </>
            ) : (
              <>
                <svg className="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <polyline points="3 6 5 6 21 6" />
                  <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
                </svg>
                <span>Delete Permanently ({formatBytes(item.total_size)})</span>
              </>
            )}
          </button>
        </div>
      </div>
    </div>
  );
}
