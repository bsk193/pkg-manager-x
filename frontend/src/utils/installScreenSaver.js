// Kept independent of React so timer/input behavior can be tested directly.
export function startInstallScreenSaver(win, doc, idleMs = 60000) {
  let lastInput = Date.now();
  let overlay = null;
  let previousFocus = null;
  let swallowUntil = 0;
  let swallowHeld = false;
  let padWake = false;
  const wake = () => {
    if (!overlay) return;
    overlay.remove();
    overlay = null;
    if (previousFocus && previousFocus.isConnected) previousFocus.focus();
    swallowUntil = Date.now() + 500;
  };
  const activity = (event) => {
    lastInput = Date.now();
    if (overlay || swallowHeld || Date.now() < swallowUntil) {
      if (['keydown', 'mousedown', 'touchstart'].includes(event.type)) swallowHeld = true;
      if (['keyup', 'mouseup', 'touchend'].includes(event.type)) {
        swallowHeld = false;
        swallowUntil = Date.now() + 500;
      }
      if (event.cancelable) event.preventDefault();
      event.stopImmediatePropagation();
      wake();
    }
  };
  const events = ['keydown', 'keyup', 'mousedown', 'mouseup', 'click',
    'mousemove', 'touchstart', 'touchend', 'wheel'];
  events.forEach(name => win.addEventListener(name, activity, { capture: true, passive: false }));
  const timer = win.setInterval(() => {
    // Some PS4 web views expose pads; others deliver keyboard/click events.
    let pads = [];
    try { pads = win.navigator.getGamepads ? win.navigator.getGamepads() : []; } catch (e) {}
    let padActive = false;
    for (let i = 0; i < pads.length; i++) {
      const pad = pads[i];
      if (pad && (Array.from(pad.buttons || []).some(b => b.pressed) ||
          Array.from(pad.axes || []).some(a => Math.abs(a) > 0.3))) {
        lastInput = Date.now();
        padActive = true;
        if (overlay) padWake = true;
        wake();
      }
    }
    if (padWake) {
      swallowUntil = Date.now() + 500;
      if (!padActive) padWake = false;
    }
    if (!overlay && !doc.hidden && Date.now() - lastInput >= idleMs) {
      previousFocus = doc.activeElement;
      overlay = doc.createElement('button');
      overlay.type = 'button';
      overlay.setAttribute('aria-label', 'Screen dimmed while installing. Press a button to wake.');
      overlay.style.cssText = 'position:fixed;inset:0;top:0;left:0;width:100%;height:100%;z-index:2147483647;background:#000;border:0;padding:0;margin:0;outline:none;cursor:none;';
      doc.body.appendChild(overlay);
      overlay.focus();
    }
  }, 100);
  return () => {
    win.clearInterval(timer);
    events.forEach(name => win.removeEventListener(name, activity, true));
    wake();
  };
}
