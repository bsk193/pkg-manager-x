import React, { useState } from 'react';
import { QRCodeSVG } from 'qrcode.react';
import { DONATE_URL, isPlayStation } from '../../constants/config';

export default function DonateModal({ show, onClose, onNeverShow, donateNeverNotice }) {
  const [showModalQr, setShowModalQr] = useState(false);
  
  if (!show) return null;

  return (
    <div className="fixed inset-0 z-50 bg-black/90 flex items-center justify-center p-4">
      <div className="bg-[#181a27] border border-white/10 rounded-[2px] max-w-md w-full p-6 space-y-5 shadow-2xl">
        {donateNeverNotice ? (
              <div className="py-8 text-center space-y-3">
                <div className="w-12 h-12 rounded-[2px] bg-emerald-500/20 border border-emerald-500/40 text-emerald-400 flex items-center justify-center mx-auto">
                  <svg className="w-6 h-6" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5">
                    <polyline points="20 6 9 17 4 12" />
                  </svg>
                </div>
                <h4 className="text-base font-bold text-white">This popup won't be shown again</h4>
                <p className="text-xs text-zinc-400">Thank you for using PKG Manager!</p>
              </div>
            ) : (
              <>
                <div className="flex items-center justify-between pb-3 border-b border-white/10">
                  <div className="flex items-center space-x-3">
                    <div className="w-10 h-10 rounded-[2px] bg-rose-600/20 border border-rose-500/30 flex items-center justify-center text-rose-400 shrink-0">
                      <svg className="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                        <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z" />
                      </svg>
                    </div>
                    <div>
                      <h3 className="text-lg font-bold text-white">Support PKG Manager</h3>
                      <p className="text-xs text-zinc-400">Free &amp; Open-Source Software</p>
                    </div>
                  </div>
                  <button
                    type="button"
                    onClick={onClose}
                    className="w-8 h-8 rounded-[2px] ps5-focus-item bg-white/5 hover:bg-white/10 text-zinc-400 hover:text-white flex items-center justify-center transition-colors cursor-pointer"
                  >
                    ✕
                  </button>
                </div>

                <p className="text-xs text-zinc-300 leading-relaxed">
                  PKG Manager is free and open-source software. If you enjoy using it, please consider supporting its development!
                </p>

                {isPlayStation ? (
                  <div className="bg-black/40 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                    <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                      <QRCodeSVG
                        value={DONATE_URL}
                        size={150}
                        level="M"
                      />
                    </div>
                    <div className="text-center space-y-1">
                      <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                      <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                        github.com/itsPLK/ps5-pkg-manager
                      </span>
                    </div>
                  </div>
                ) : (
                  <div className="space-y-3">
                    <a
                      href={DONATE_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                      onClick={onClose}
                      className="w-full py-3 px-4 rounded-[2px] ps5-focus-item bg-rose-600 hover:bg-rose-500 text-white text-sm font-bold transition-colors flex items-center justify-center cursor-pointer"
                    >
                      View donation options
                    </a>
                    <div className="flex justify-center">
                      <button
                        type="button"
                        onClick={() => setShowModalQr((prev) => !prev)}
                        className="text-[11px] text-zinc-400 hover:text-zinc-200 transition-colors cursor-pointer underline underline-offset-2"
                      >
                        {showModalQr ? 'Hide QR Code' : 'Show QR Code for phone scan'}
                      </button>
                    </div>
                    {showModalQr && (
                      <div className="bg-black/40 border border-white/10 rounded-[2px] p-4 flex flex-col items-center space-y-3">
                        <div className="p-2.5 bg-white rounded-[4px] shadow-lg flex items-center justify-center">
                          <QRCodeSVG
                            value={DONATE_URL}
                            size={140}
                            level="M"
                          />
                        </div>
                        <div className="text-center space-y-1">
                          <span className="text-xs font-semibold text-white block">Scan with your phone</span>
                          <span className="text-[11px] font-mono text-zinc-500 block pt-0.5">
                            github.com/itsPLK/ps5-pkg-manager
                          </span>
                        </div>
                      </div>
                    )}
                  </div>
                )}

                <div className="flex items-center justify-between pt-2 border-t border-white/10">
                  <button
                    type="button"
                    onClick={onNeverShow}
                    className="text-xs text-zinc-500 hover:text-zinc-300 ps5-focus-item px-2.5 py-1.5 rounded-[2px] transition-colors cursor-pointer"
                  >
                    Don't show again
                  </button>
                  <button
                    type="button"
                    onClick={onClose}
                    className="px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-white/15 text-zinc-200 text-xs font-semibold transition-colors cursor-pointer"
                  >
                    Maybe Later
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
  );
}
