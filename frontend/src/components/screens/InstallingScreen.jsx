import React from 'react';
import BlurIcon, { iconUrlFor } from '../../BlurIcon';
import { formatBytes } from '../../utils/formatters';
import DebugSpeedOverlay from './DebugSpeedOverlay';

export default function InstallingScreen({ installerStatus, batchInstall, etaInfo, storage, isDiscSource, onCancel, onDismiss, onBackground, queue = [], onRemoveQueued, consoleName = 'ps5', packages = [], directIconUrl, debugSpeeds }) {
const isBatch = !!(batchInstall && batchInstall.combinedTotal > 0);
    let totalBytes = installerStatus.total_bytes;
    let downloadedBytes = installerStatus.downloaded_bytes;
    let progressVal = installerStatus.progress;
    let titleToDisplay = installerStatus.title_name || 'Installing Package...';
    let displayIconPath = installerStatus.pkg_path || null;
    let displayIconPkg = (displayIconPath && packages)
      ? packages.find((p) => p.path === displayIconPath) || null
      : null;
    const isDirectStorage = !!(installerStatus.is_direct_storage || progressVal < 0);
    let statusChip = installerStatus.is_multipart
      ? `Installing Part ${installerStatus.current_part} of ${installerStatus.total_parts}`
      : isDirectStorage
        ? 'Direct Storage Install'
        : `Installing to ${consoleName === 'ps4' ? 'PS4' : 'PS5'}`;
    const queuedName = (path) => {
      const pkg = packages && packages.find((p) => p.path === path);
      return (pkg && pkg.title_name) || decodeURIComponent(String(path).split('/').pop() || path);
    };

    if (isBatch) {
      totalBytes = batchInstall.combinedTotal;
      if (batchInstall.stage === 'base') {
        downloadedBytes = Math.min(batchInstall.baseSize, installerStatus.downloaded_bytes);
        statusChip = `Installing Base + Update (Part 1/2: Base Package)`;
      } else {
        downloadedBytes = batchInstall.baseSize + Math.min(batchInstall.updateSize, installerStatus.downloaded_bytes);
        statusChip = `Installing Base + Update (Part 2/2: Update)`;
      }
      progressVal = totalBytes > 0 ? (downloadedBytes / totalBytes) * 100 : 0;
      if (batchInstall.titleName) {
        titleToDisplay = batchInstall.titleName;
      }
      if (batchInstall.iconPath) {
        displayIconPath = batchInstall.iconPath;
        displayIconPkg = (packages && packages.find((p) => p.path === displayIconPath)) || null;
      }
    }
    const displayIconUrl = iconUrlFor(
      displayIconPath,
      displayIconPkg ? displayIconPkg.mtime : 0,
      displayIconPkg ? displayIconPkg.file_size : 0
    );

    return (
      <div className="fixed inset-0 z-50 bg-[#0a0a0f] text-white flex flex-col items-center justify-center p-6 overflow-hidden select-none">
        {isDirectStorage && (
          <button
            type="button"
            onClick={onDismiss}
            aria-label="Close installation screen"
            title="Stop tracking this install; PS5 installation will continue"
            className="absolute top-4 right-4 z-20 w-11 h-11 flex items-center justify-center rounded-[2px] bg-white/10 hover:bg-white/20 border border-white/20 text-zinc-200 hover:text-white transition-colors cursor-pointer"
          >
            <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M18 6 6 18M6 6l12 12" />
            </svg>
          </button>
        )}
        {debugSpeeds && <DebugSpeedOverlay uploadSpeed={debugSpeeds.upload} installSpeed={debugSpeeds.install} />}
        <div className="relative z-10 flex flex-col items-center max-w-4xl w-full text-center">
          {/* Status Chip */}
          <div className="px-3 py-1 rounded-[2px] bg-blue-500/20 text-blue-300 border border-blue-500/30 text-xs uppercase font-bold tracking-wider mb-5">
            {statusChip}
          </div>

          {/* Big Package Picture */}
          <div className="relative w-48 h-48 sm:w-60 sm:h-60 rounded-[2px] overflow-hidden bg-[#141520] border border-white/20 flex items-center justify-center">
            {/* Fallback Icon */}
            <div className="absolute inset-0 flex items-center justify-center text-zinc-600 pointer-events-none">
              <svg className="w-16 h-16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                <rect x="2" y="3" width="20" height="14" rx="2" />
                <line x1="8" y1="21" x2="16" y2="21" />
                <line x1="12" y1="17" x2="12" y2="21" />
              </svg>
            </div>
            {displayIconPath?.startsWith('live:') && directIconUrl ? (
              <img src={directIconUrl} alt={titleToDisplay} className="absolute inset-0 w-full h-full object-cover z-10" />
            ) : displayIconUrl ? (
              <BlurIcon
                path={displayIconPath}
                pkg={displayIconPkg}
                alt={titleToDisplay}
                priority={true}
                imgClassName="absolute inset-0 w-full h-full object-cover z-10"
              />
            ) : null}
          </div>

          {/* Title Below Package Picture */}
          <h2 className="text-2xl sm:text-3xl font-black text-white mt-6 max-w-2xl truncate">
            {titleToDisplay}
          </h2>

          <p className="text-xs sm:text-sm font-mono text-zinc-400 mt-1">
            {installerStatus.title_id}
            {installerStatus.content_id ? ` • ${installerStatus.content_id}` : ''}
          </p>

          {installerStatus.is_direct_storage || progressVal < 0 ? (
            <div className="w-[75vw] max-w-3xl mt-8 p-5 bg-white/[0.04] border border-blue-500/30 rounded-lg text-left shadow-lg backdrop-blur-sm">
              <div className="flex items-start space-x-3.5">
                <div className="w-8 h-8 rounded-full bg-blue-500/20 border border-blue-400/40 flex items-center justify-center shrink-0 mt-0.5">
                  <svg className="w-4 h-4 text-blue-300" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                    <path strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
                  </svg>
                </div>
                <div className="flex-1 space-y-1.5">
                  <h3 className="text-sm font-semibold text-white tracking-wide">
                    Direct Storage Installation in Progress
                  </h3>
                  <p className="text-xs text-zinc-300 leading-relaxed">
                    Track installation progress directly in the PS5 home menu / Notifications downloads.
                  </p>
                  <p className="text-xs text-amber-300/90 leading-relaxed pt-0.5">
                    Tip: An active network connection is required on your PS5 to stream packages and view detailed real-time progress here due to system limitations.
                  </p>
                </div>
              </div>
            </div>
          ) : (
            <>
              {installerStatus.prompt_message ? (
                <p className="text-xs text-blue-300 font-medium mt-1">
                  {installerStatus.prompt_message}
                </p>
              ) : null}

              {/* Wide Progress Bar Across ~3/4 Screen */}
              <div className="w-[75vw] max-w-3xl bg-white/10 rounded-[2px] h-4 sm:h-5 mt-8 overflow-hidden border border-white/20 p-0.5">
                <div
                  className="bg-[#0070d1] h-full rounded-[2px] transition-all duration-300"
                  style={{ width: `${Math.min(100, Math.max(0, progressVal))}%` }}
                />
              </div>

              {/* Progress Stats & ETA */}
              <div className="w-[75vw] max-w-3xl flex items-center justify-between text-xs sm:text-sm text-zinc-300 mt-3 px-1">
                <span className="font-bold text-white">
                  {progressVal.toFixed(1)}%
                  <span className="text-zinc-400 font-normal ml-2">
                    ({formatBytes(downloadedBytes)} / {formatBytes(totalBytes)})
                  </span>
                </span>

                <span>
                  {etaInfo && etaInfo.text ? (
                    <span>
                      <span className="font-medium text-white">{etaInfo.text}</span>
                      {etaInfo.speedStr ? (
                        <span className="text-zinc-400 font-mono ml-2">({etaInfo.speedStr})</span>
                      ) : null}
                    </span>
                  ) : (
                    <span className="text-zinc-400 font-mono capitalize">{installerStatus.status}</span>
                  )}
                </span>
              </div>
            </>
          )}

          {/* Background + Cancel (cancel hidden for direct storage installs) */}
          {!isDirectStorage && (
            <div className="mt-8 flex flex-wrap items-center justify-center gap-3">
              {onBackground && (
                <button
                  type="button"
                  onClick={onBackground}
                  className="px-6 py-2.5 rounded-[2px] ps5-focus-item bg-blue-600/80 hover:bg-blue-600 border border-white/20 text-sm font-semibold text-white transition-all cursor-pointer"
                >
                  Continue in Background
                </button>
              )}
              <button
                type="button"
                onClick={onCancel}
                className="px-6 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-rose-600/80 border border-white/20 text-sm font-semibold text-zinc-200 hover:text-white transition-all cursor-pointer"
              >
                Cancel Installation
              </button>
            </div>
          )}

          {queue.length > 0 && (
            <div className="w-[75vw] max-w-3xl mt-8 text-left">
              <h3 className="text-xs uppercase font-bold tracking-wider text-zinc-400 mb-2">
                Up next ({queue.length})
              </h3>
              <ul className="space-y-1.5">
                {queue.map((path, i) => (
                  <li key={path} className="flex items-center justify-between rounded-[2px] bg-white/[0.04] border border-white/10 px-3 py-2">
                    <span className="text-sm text-zinc-200 truncate">
                      <span className="text-zinc-500 font-mono mr-2">{i + 1}.</span>{queuedName(path)}
                    </span>
                    {onRemoveQueued && (
                      <button
                        type="button"
                        onClick={() => onRemoveQueued(path)}
                        className="ml-3 shrink-0 px-3 py-1 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-rose-600/80 border border-white/20 text-xs font-semibold text-zinc-200 cursor-pointer"
                      >
                        Remove
                      </button>
                    )}
                  </li>
                ))}
              </ul>
            </div>
          )}
        </div>
      </div>
    );
  }
