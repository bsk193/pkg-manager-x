import React, { useEffect } from 'react';

// The PS4 tile shows the UI in the system web view (opened with ?app=ps4tile).
// Its buttons do not behave like the Browser app's, so offer an explicit way
// out: window.close() ends the web view and the tile returns to the home
// screen. X (Escape in the web view) confirms like Circle.
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

// Diagnostics: what the web view reports for the controller buttons (the
// first 40 key events and any gamepad button presses go to the server log).
function report(msg) {
  try { fetch(`/api/client-event?msg=${encodeURIComponent(msg)}`).catch(() => {}); } catch (e) {}
}

function useButtonDiagnostics() {
  useEffect(() => {
    let sent = 0;
    const onKey = (e) => {
      if (sent++ >= 40) return;
      report(`${e.type} key=${e.key} code=${e.code} keyCode=${e.keyCode} which=${e.which}`);
    };
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('keyup', onKey, true);
    report(`tile web view: ${navigator.userAgent} gamepadApi=${typeof navigator.getGamepads === 'function'}`);
    let last = '';
    const timer = setInterval(() => {
      if (typeof navigator.getGamepads !== 'function') return;
      const pads = Array.from(navigator.getGamepads() || []).filter(Boolean);
      const pressed = pads.map((p) => `${p.index}:[${p.buttons.map((b, i) => (b.pressed ? i : null)).filter((i) => i !== null).join(',')}]`).join(' ');
      if (pressed !== last) {
        last = pressed;
        if (pressed.replace(/\d+:\[\]/g, '').trim()) report(`gamepad ${pressed}`);
      }
    }, 100);
    return () => {
      window.removeEventListener('keydown', onKey, true);
      window.removeEventListener('keyup', onKey, true);
      clearInterval(timer);
    };
  }, []);
}

export default function TileControls() {
  useButtonDiagnostics();
  useEffect(() => {
    // The web view uses the Japanese layout: Circle confirms by itself (no
    // key event reaches the page) and X arrives as Escape (keyCode 27,
    // measured on 13.52). Make X confirm too: click the focused element.
    const onKey = (e) => {
      if (e.key !== 'Escape' && e.keyCode !== 27) return;
      e.preventDefault();
      e.stopPropagation();
      const el = document.activeElement;
      if (el && el !== document.body && typeof el.click === 'function') el.click();
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
