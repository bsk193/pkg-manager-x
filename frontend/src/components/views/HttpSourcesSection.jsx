import React from 'react';

const TLS_LABELS = {
  verify: 'Verified certificate',
  pin: 'Pinned certificate',
  none: 'No certificate check'
};

function GlobeIcon({ className }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
      <circle cx="12" cy="12" r="10" />
      <line x1="2" y1="12" x2="22" y2="12" />
      <path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z" />
    </svg>
  );
}

export default function HttpSourcesSection({ sources, onAdd, onEdit, onToggle, onRemove, onTest, testing, httpsSupported }) {
  const list = Array.isArray(sources) ? sources : [];
  const enabled = list.filter((s) => s && s.enabled).length;

  return (
    <div className="max-w-6xl mx-auto px-4 sm:px-8 space-y-6 pb-12">
      <div className="rounded-[2px] bg-[#141520] border border-white/10 p-6 flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4">
        <div className="flex items-center space-x-4">
          <div className="w-12 h-12 rounded-[2px] bg-violet-600/20 border border-violet-500/30 flex items-center justify-center text-violet-300 shrink-0">
            <GlobeIcon className="w-6 h-6" />
          </div>
          <div>
            <h2 className="text-xl font-bold text-white">HTTP / HTTPS Servers</h2>
            <p className="text-xs text-zinc-400 mt-0.5">
              Stream packages from a web server (nginx, Apache, Caddy, <span className="font-mono">python -m http.server</span>, NAS web share)
            </p>
          </div>
        </div>

        <div className="flex items-center space-x-2 text-xs font-mono shrink-0">
          <span className="px-3 py-1 rounded-[2px] bg-white/5 border border-white/10 text-zinc-300">
            {list.length} {list.length === 1 ? 'Server' : 'Servers'}
          </span>
          <span className="px-3 py-1 rounded-[2px] bg-emerald-500/15 text-emerald-300 border border-emerald-500/30">
            {enabled} Enabled
          </span>
          <button
            type="button"
            onClick={onAdd}
            className="px-4 py-2 rounded-[2px] ps5-focus-item bg-violet-600 hover:bg-violet-500 text-white text-xs font-bold font-sans transition-colors cursor-pointer"
          >
            + Add Server
          </button>
        </div>
      </div>

      {list.length === 0 ? (
        <div className="py-10 text-center rounded-[2px] border border-white/10 bg-[#12131a]/40 p-8 space-y-3">
          <h3 className="text-base font-bold text-white">No HTTP Servers Configured</h3>
          <p className="text-xs text-zinc-400 max-w-xl mx-auto leading-relaxed">
            Point PKG Manager at a folder served over HTTP. Packages are found through the server's directory
            listing (sub-folders like <span className="font-mono">PS4/</span> and <span className="font-mono">PS5/</span> included)
            or an <span className="font-mono">index.json</span> made with <span className="font-mono">tools/make_http_index.py</span>.
            The server must support byte ranges.
            {!httpsSupported && ' This build supports plain http:// only.'}
          </p>
        </div>
      ) : (
        <div className="space-y-4">
          {list.map((src, idx) => {
            const isHttps = /^https:/i.test(src.url || '');
            return (
              <div
                key={src.id || idx}
                className={`rounded-[2px] p-5 border transition-all ${
                  src.enabled ? 'bg-[#141520] border-white/10' : 'bg-[#12131b]/60 border-white/5 opacity-50'
                }`}
              >
                <div className="flex flex-col md:flex-row md:items-center justify-between gap-4">
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center space-x-3 flex-wrap gap-y-1">
                      <h4 className="text-base font-bold text-white truncate">{src.label || src.url}</h4>
                      <span className={`text-[10px] px-2 py-0.5 rounded-[2px] font-bold border shrink-0 ${
                        isHttps ? 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30' : 'bg-white/5 text-zinc-400 border-white/10'
                      }`}>
                        {isHttps ? 'HTTPS' : 'HTTP'}
                      </span>
                      {isHttps && (
                        <span className="text-[10px] px-2 py-0.5 rounded-[2px] bg-white/5 text-zinc-400 border border-white/10 shrink-0">
                          {TLS_LABELS[src.tls_mode] || TLS_LABELS.verify}
                        </span>
                      )}
                      {!src.enabled && (
                        <span className="text-[10px] px-2 py-0.5 rounded-[2px] font-semibold bg-zinc-800 text-zinc-400 border border-zinc-700 shrink-0">
                          Disabled
                        </span>
                      )}
                      {src.username && (
                        <span className="text-[10px] px-2 py-0.5 rounded-[2px] font-mono bg-white/5 text-zinc-400 border border-white/10 shrink-0">
                          user: {src.username}
                        </span>
                      )}
                    </div>
                    <p className="text-xs font-mono text-violet-300/80 mt-1.5 truncate select-all">{src.url}</p>
                  </div>

                  <div className="flex items-center space-x-2 shrink-0">
                    <button
                      type="button"
                      onClick={() => onTest(src)}
                      disabled={testing}
                      className="px-3 py-2 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-200 hover:text-white border border-white/10 text-xs font-semibold transition-colors cursor-pointer disabled:opacity-40"
                    >
                      Test
                    </button>
                    <button
                      type="button"
                      onClick={() => onToggle(idx)}
                      className={`px-3 py-2 rounded-[2px] ps5-focus-item text-xs font-semibold border transition-colors cursor-pointer ${
                        src.enabled
                          ? 'bg-white/5 text-zinc-300 hover:bg-white/10 border-white/10'
                          : 'bg-violet-600/20 text-violet-300 border-violet-500/30 hover:bg-violet-600/30'
                      }`}
                    >
                      {src.enabled ? 'Disable' : 'Enable'}
                    </button>
                    <button
                      type="button"
                      onClick={() => onEdit(idx, src)}
                      className="px-3 py-2 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-200 hover:text-white border border-white/10 text-xs font-semibold transition-colors cursor-pointer"
                    >
                      Edit
                    </button>
                    <button
                      type="button"
                      onClick={() => onRemove(idx)}
                      className="px-3 py-2 rounded-[2px] ps5-focus-item bg-rose-600/15 hover:bg-rose-600/25 text-rose-300 border border-rose-500/25 text-xs font-semibold transition-colors cursor-pointer"
                    >
                      Delete
                    </button>
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
