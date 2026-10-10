import React from 'react';

// Shown while an install runs in the background: progress at a glance and a
// way back to the full install screen.
export default function InstallBackgroundBar({ installerStatus, queueLength = 0, onShow }) {
  const progress = Number(installerStatus.progress) || 0;
  const title = installerStatus.title_name || 'Installing package';
  return (
    <div className="fixed bottom-4 left-1/2 -translate-x-1/2 z-40 w-[min(92vw,40rem)] rounded-[2px] bg-[#141520]/95 border border-white/20 shadow-lg backdrop-blur-sm px-4 py-3 flex items-center space-x-4">
      <div className="min-w-0 flex-1">
        <div className="flex items-center justify-between text-xs sm:text-sm">
          <span className="font-semibold text-white truncate">{title}</span>
          <span className="text-zinc-300 font-mono shrink-0 ml-3">
            {progress >= 0 ? `${progress.toFixed(1)}%` : 'Installing'}
          </span>
        </div>
        <div className="mt-2 w-full bg-white/10 rounded-[2px] h-1.5 overflow-hidden">
          <div
            className="bg-[#0070d1] h-full transition-all duration-300"
            style={{ width: `${Math.min(100, Math.max(0, progress))}%` }}
          />
        </div>
        {queueLength > 0 && (
          <p className="text-[11px] text-zinc-400 mt-1.5">{queueLength} more in queue</p>
        )}
      </div>
      <button
        type="button"
        onClick={onShow}
        className="shrink-0 px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/20 border border-white/20 text-sm font-semibold text-zinc-100 cursor-pointer"
      >
        Show
      </button>
    </div>
  );
}
