import test from 'node:test';
import assert from 'node:assert/strict';
import { startInstallScreenSaver } from '../src/utils/installScreenSaver.js';

test('idle screen wakes without clicking through, resets its timer, and cleans up', () => {
  const originalNow = Date.now;
  let now = 0, tick, overlay, restored = 0, cleared = false;
  const listeners = new Map();
  let pads = [];
  const win = {
    navigator: { getGamepads: () => pads },
    addEventListener: (name, fn) => listeners.set(name, fn),
    removeEventListener: name => listeners.delete(name),
    setInterval: fn => { tick = fn; return 1; },
    clearInterval: () => { cleared = true; },
  };
  const doc = {
    hidden: false,
    activeElement: { isConnected: true, focus: () => restored++ },
    body: { appendChild: node => { overlay = node; } },
    createElement: () => ({ style: {}, setAttribute() {}, focus() {}, remove() { overlay = null; } }),
  };
  Date.now = () => now;
  try {
    const stop = startInstallScreenSaver(win, doc);
    now = 59999; tick(); assert.equal(overlay, undefined);
    now = 60000; tick(); assert.ok(overlay);
    let prevented = 0, swallowed = 0;
    const event = { cancelable: true, preventDefault: () => prevented++, stopImmediatePropagation: () => swallowed++ };
    listeners.get('keydown')(event);
    assert.equal(overlay, null);
    listeners.get('click')(event);
    assert.equal(prevented, 2); assert.equal(swallowed, 2); assert.equal(restored, 1);
    now = 61000; listeners.get('click')(event);
    assert.equal(swallowed, 2);
    now = 120000; tick(); assert.equal(overlay, null);
    now = 121000; tick(); assert.ok(overlay);
    pads = [{ buttons: [{ pressed: true }], axes: [0] }];
    tick(); assert.equal(overlay, null);
    pads = []; now = 181000; tick(); assert.ok(overlay);
    stop(); assert.equal(overlay, null); assert.ok(cleared); assert.equal(listeners.size, 0);
  } finally { Date.now = originalNow; }
});
