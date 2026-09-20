import React from 'react';
import { formatBytes } from '../../utils/formatters';

export default function Header({ appVersion, storage, showSettings, showSmbPage, onSettingsClick, onRescan, refreshing, selectedDrive, onBackToDrives }) {
  return (
    <header className="border-b border-white/10 bg-[#12131a] px-4 py-3 sm:px-6">
      {/* Top Header Bar (Non-sticky, hides naturally when scrolling down) */}
        <div className="w-full flex items-center justify-between">
          <div className="flex items-center space-x-3">
            <div className="w-10 h-10 rounded-[2px] bg-[#0095ff] flex items-center justify-center border border-white/20 shrink-0">
              <svg className="w-6 h-6 text-white" viewBox="0 0 452.23 512" fill="currentColor">
                <path d="M241.45,506.44c-10.13,7.49-20.64,7.22-30.03.38L12.99,392.22C5.05,387.64,0,381.06,0,371.4l.08-234.47c0-8.9,7.83-15.2,15.02-18.6L212.58,4.33c8.76-5.77,18.31-5.77,27.07,0l197.48,114c7.19,3.4,15.01,9.7,15.02,18.6l.08,234.47c0,9.66-5.05,16.24-12.99,20.83l-197.78,114.21ZM320.91,175.85l62.59-35.82L226.06,49.07l-62.16,36.13,157.01,90.65ZM226.31,229.78l62.36-36.05-156.49-90.43-63.46,36.82,157.59,89.66ZM203.22,449.33l.12-180.25L45.62,179.46l-.02,179.06,157.62,90.82ZM406.67,358.43l-.09-178.99-157.75,89.74.12,180.28,157.72-91.03Z" />
              </svg>
            </div>
            <h1 className="text-lg font-bold tracking-wide text-white">
              PKG Manager
            </h1>
          </div>

          <div className="flex items-center space-x-3 sm:space-x-4">
            {/* Internal Storage Display Widget */}
            {storage && (
              <div className="flex items-center space-x-3 bg-white/5 px-3.5 py-1.5 rounded-[2px] border border-white/10 text-xs">
                <svg className="w-4 h-4 text-blue-400 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <rect x="2" y="3" width="20" height="18" rx="2" />
                  <line x1="2" y1="9" x2="22" y2="9" />
                  <line x1="10" y1="15" x2="10.01" y2="15" />
                </svg>
                <div className="flex flex-col">
                  <span className="text-zinc-400 font-mono text-[10px]">INTERNAL</span>
                  <div className="flex items-center space-x-2">
                    <span className="font-bold text-white font-mono">
                      {formatBytes(storage.internal?.free ?? storage.free)} free
                    </span>
                    <span className="text-zinc-500 font-mono">
                      / {formatBytes(storage.internal?.total ?? storage.total)}
                    </span>
                  </div>
                </div>

                {/* Micro Progress Bar */}
                <div className="w-16 hidden md:block">
                  <div className="w-full bg-white/10 h-1.5 rounded-full overflow-hidden">
                    <div
                      className="bg-blue-500 h-full rounded-full transition-all duration-500"
                      style={{
                        width: `${Math.min(
                          100,
                          Math.max(0, Math.round((((storage.internal?.used ?? storage.used)) / ((storage.internal?.total ?? storage.total) || 1)) * 100))
                        )}%`
                      }}
                    />
                  </div>
                </div>
              </div>
            )}

            {/* M.2 NVMe Storage Display Widget */}
            {storage?.nvme && storage.nvme.available && (
              <div className="flex items-center space-x-3 bg-white/5 px-3.5 py-1.5 rounded-[2px] border border-white/10 text-xs">
                <svg className="w-4 h-4 text-purple-400 shrink-0" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <rect x="2" y="3" width="20" height="18" rx="2" />
                  <path d="M4 7h16M4 12h16M4 17h16" />
                </svg>
                <div className="flex flex-col">
                  <span className="text-zinc-400 font-mono text-[10px]">M.2 NVME</span>
                  <div className="flex items-center space-x-2">
                    <span className="font-bold text-white font-mono">
                      {formatBytes(storage.nvme.free)} free
                    </span>
                    <span className="text-zinc-500 font-mono">
                      / {formatBytes(storage.nvme.total)}
                    </span>
                  </div>
                </div>

                {/* Micro Progress Bar */}
                <div className="w-16 hidden md:block">
                  <div className="w-full bg-white/10 h-1.5 rounded-full overflow-hidden">
                    <div
                      className="bg-purple-500 h-full rounded-full transition-all duration-500"
                      style={{
                        width: `${Math.min(
                          100,
                          Math.max(0, Math.round((storage.nvme.used / (storage.nvme.total || 1)) * 100))
                        )}%`
                      }}
                    />
                  </div>
                </div>
              </div>
            )}

            {/* Settings Button */}
            <button
              type="button"
              onClick={onSettingsClick}
              className={`px-3.5 py-1.5 rounded-[2px] ps5-focus-item border text-xs font-semibold transition-colors flex items-center space-x-1.5 cursor-pointer ${
                (showSettings || showSmbPage)
                  ? 'bg-white/20 border-white/40 text-white'
                  : 'bg-white/10 hover:bg-white/15 border-white/10 text-zinc-200'
              }`}
            >
              <svg className="w-3.5 h-3.5 text-zinc-300" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <circle cx="12" cy="12" r="3" />
                <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2 2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z" />
              </svg>
              <span>Settings</span>
            </button>

            {/* Rescan Button */}
            <button
              type="button"
              onClick={onRescan}
              disabled={refreshing}
              className="px-3.5 py-1.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 border border-white/10 text-xs font-semibold text-white transition-colors flex items-center space-x-2 disabled:opacity-50 cursor-pointer"
            >
              <svg
                className={`w-3.5 h-3.5 text-zinc-300 ${refreshing ? 'animate-spin' : ''}`}
                viewBox="0 0 24 24"
                fill="none"
                stroke="currentColor"
                strokeWidth="2"
              >
                <path d="M21 12a9 9 0 0 0-9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
                <path d="M3 3v5h5" />
                <path d="M3 12a9 9 0 0 0 9 9 9.75 9.75 0 0 0 6.74-2.74L21 16" />
                <path d="M16 21h5v-5" />
              </svg>
              <span>{refreshing ? 'Scanning...' : 'Rescan'}</span>
            </button>
          </div>
        </div>
      </header>
  );
}
