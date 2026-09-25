import React from 'react';
import { getFullVersion, getUpstreamVersionLabel } from '../../utils/title';
import { isPlayStation, FORK_REPO_URL, UPSTREAM_REPO_URL } from '../../constants/config';

const linkClass = 'text-zinc-500 hover:text-zinc-400 underline underline-offset-2 transition-colors';

export default function Footer({ appVersion }) {
  return (
    <footer className="w-full py-4 px-4 text-center text-[11px] text-zinc-600 border-t border-white/5 select-none">
      {/* Footer */}
        <div className="flex flex-wrap items-center justify-center gap-x-3 gap-y-1">
          <span>{getFullVersion(appVersion)}</span>
          <span className="text-zinc-700 hidden sm:inline">&bull;</span>
          <span>{getUpstreamVersionLabel()}</span>
          <span className="text-zinc-700 hidden sm:inline">&bull;</span>
          {isPlayStation ? (
            <span>Free and open source: github.com/bsk193/pkg-manager-x</span>
          ) : (
            <span>
              Free and open source:{' '}
              <a href={FORK_REPO_URL} target="_blank" rel="noopener noreferrer" className={linkClass}>
                GitHub
              </a>
              {' '}(upstream:{' '}
              <a href={UPSTREAM_REPO_URL} target="_blank" rel="noopener noreferrer" className={linkClass}>
                PKG Manager
              </a>
              )
            </span>
          )}
        </div>
      </footer>
  );
}
