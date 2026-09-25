import React from 'react';

const inputClass =
  'w-full bg-black/50 border border-white/15 rounded-[2px] px-3.5 py-2.5 text-sm text-white placeholder-zinc-500 focus:outline-none focus:border-white/40';

function formatFingerprint(fp) {
  return (fp || '').replace(/(..)(?=.)/g, '$1:').toUpperCase();
}

export default function HttpSourceModal({
  show, onClose, isEditing, form, setForm, testResult, testing, onTest, onSave, onTrustFingerprint, httpsSupported
}) {
  if (!show) return null;

  const url = (form.url || '').trim();
  const isHttps = /^https:/i.test(url);
  const canSave = url.length > 0;
  const fingerprint = testResult && testResult.fingerprint;
  const alreadyPinned = form.tls_mode === 'pin' && fingerprint &&
    (form.tls_pin || '').replace(/[^0-9a-f]/gi, '').toLowerCase() === fingerprint;

  return (
    <div data-modal-dialog="true" role="dialog" aria-modal="true" className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
      <div className="bg-[#181a27] border border-white/15 rounded-[2px] max-w-lg w-full p-6 space-y-5 overflow-y-auto max-h-[90vh]">
        <div className="flex items-center justify-between pb-3 border-b border-white/10">
          <div>
            <h3 className="text-lg font-bold text-white">{isEditing ? 'Edit HTTP Server' : 'Add HTTP Server'}</h3>
            <p className="text-xs text-zinc-400">Base URL of the folder that holds your packages</p>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="text-zinc-500 hover:text-white text-lg font-bold px-2 py-1 cursor-pointer transition-colors"
          >
            &times;
          </button>
        </div>

        <div className="space-y-4 text-xs">
          <div>
            <label className="block font-semibold text-zinc-300 mb-1">Display Label (Optional)</label>
            <input
              type="text"
              placeholder="e.g. NAS Web, Seedbox"
              value={form.label || ''}
              onChange={(e) => setForm({ ...form, label: e.target.value })}
              className={inputClass}
            />
          </div>

          <div>
            <label className="block font-semibold text-zinc-300 mb-1">Server URL *</label>
            <input
              type="text"
              placeholder="http://192.168.1.10:8080/pkgs/"
              value={form.url || ''}
              onChange={(e) => setForm({ ...form, url: e.target.value })}
              className={`${inputClass} font-mono`}
            />
            <p className="text-[11px] text-zinc-500 mt-1">
              Sub-folders are scanned too (e.g. <span className="font-mono">PS4/</span>, <span className="font-mono">PS5/</span>).
              {!httpsSupported && ' HTTPS is not available in this build.'}
            </p>
          </div>

          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="block font-semibold text-zinc-300 mb-1">Username (Optional)</label>
              <input
                type="text"
                placeholder="Basic auth"
                value={form.username || ''}
                onChange={(e) => setForm({ ...form, username: e.target.value })}
                className={inputClass}
              />
            </div>
            <div>
              <label className="block font-semibold text-zinc-300 mb-1">Password</label>
              <input
                type="password"
                placeholder={isEditing && form.has_password ? 'unchanged' : '••••••••'}
                value={form.password || ''}
                onChange={(e) => setForm({ ...form, password: e.target.value })}
                className={inputClass}
              />
            </div>
          </div>

          {isHttps && (
            <div className="space-y-2">
              <label className="block font-semibold text-zinc-300 mb-1">Certificate Check</label>
              <select
                value={form.tls_mode || 'verify'}
                onChange={(e) => setForm({ ...form, tls_mode: e.target.value })}
                className={inputClass}
              >
                <option value="verify" className="bg-[#161722]">Verify with CA bundle (/data/pkgmgr/cacert.pem)</option>
                <option value="pin" className="bg-[#161722]">Trust a specific certificate (fingerprint)</option>
                <option value="none" className="bg-[#161722]">Do not check (self-signed, LAN only)</option>
              </select>
              {form.tls_mode === 'pin' && (
                <input
                  type="text"
                  placeholder="SHA-256 fingerprint (run Test to fill it in)"
                  value={form.tls_pin || ''}
                  onChange={(e) => setForm({ ...form, tls_pin: e.target.value })}
                  className={`${inputClass} font-mono text-xs`}
                />
              )}
            </div>
          )}

          {testResult && (
            <div className={`rounded-[2px] p-3 border text-xs space-y-2 ${
              testResult.success
                ? 'bg-emerald-500/15 border-emerald-500/30 text-emerald-300'
                : 'bg-rose-500/15 border-rose-500/30 text-rose-300'
            }`}>
              <div className="flex items-start space-x-2.5">
                <span className="font-bold">{testResult.success ? '✓' : '✗'}</span>
                <span className="flex-1">{testResult.message || (testResult.success ? 'Connected successfully!' : 'Connection failed')}</span>
              </div>
              {isHttps && fingerprint && (
                <div className="border-t border-white/10 pt-2 space-y-1.5">
                  <p className="text-zinc-300">Server certificate SHA-256:</p>
                  <p className="font-mono text-[10px] break-all text-zinc-200 select-all">{formatFingerprint(fingerprint)}</p>
                  {!alreadyPinned && (
                    <button
                      type="button"
                      onClick={() => onTrustFingerprint(fingerprint)}
                      className="px-3 py-1.5 rounded-[2px] bg-white/10 hover:bg-white/15 text-white text-[11px] font-semibold transition-colors cursor-pointer"
                    >
                      Trust this certificate
                    </button>
                  )}
                </div>
              )}
            </div>
          )}
        </div>

        <div className="flex items-center justify-between pt-2 border-t border-white/10">
          <button
            type="button"
            onClick={onTest}
            disabled={testing || !canSave}
            className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-200 text-xs font-semibold transition-colors disabled:opacity-40 cursor-pointer flex items-center space-x-2"
          >
            {testing && <div className="ps5-robust-spinner-sm" />}
            <span>{testing ? 'Testing...' : 'Test Connection'}</span>
          </button>

          <div className="flex items-center space-x-3">
            <button
              type="button"
              onClick={onClose}
              className="px-4 py-2.5 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-400 hover:text-white text-xs font-semibold transition-colors cursor-pointer"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={onSave}
              disabled={!canSave}
              className="px-5 py-2.5 rounded-[2px] ps5-focus-item bg-violet-600 hover:bg-violet-500 text-white text-xs font-bold transition-colors disabled:opacity-40 cursor-pointer"
            >
              Save Server
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
