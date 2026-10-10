import React, { useEffect } from 'react';

// The PS4 tile shows the UI in the system web view (opened with ?app=ps4tile).
// Its buttons do not behave like the Browser app's, so offer an explicit way
// out: window.close() ends the web view and the tile returns to the home
// screen. Circle (cancel, Escape in the web view) goes back a screen.
export function isPs4Tile() {
  try {
    return new URLSearchParams(window.location.search).get('app') === 'ps4tile';
  } catch (e) {
    return false;
  }
}

function closeTile() {
  try { window.close(); } catch (e) {}
}

export default function TileControls() {
  useEffect(() => {
    // The web view's cancel button arrives as Escape (keyCode 27, measured
    // on 13.52); confirm never reaches the page. The tile's param.sfo makes
    // Cross confirm, so Circle is cancel: go back a screen.
    const onKey = (e) => {
      if (e.key !== 'Escape' && e.keyCode !== 27) return;
      e.preventDefault();
      const hash = window.location.hash || '';
      if (hash && hash !== '#' && hash !== '#/') window.history.back();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  return (
    <button
      type="button"
      onClick={closeTile}
      aria-label="Close PKG Manager X"
      className="fixed bottom-4 right-4 z-50 px-4 py-2 rounded-[2px] ps5-focus-item bg-white/10 hover:bg-rose-600/80 border border-white/20 text-sm font-semibold text-zinc-100 cursor-pointer"
    >
      Close
    </button>
  );
}
