import assert from 'node:assert/strict';
import test from 'node:test';
import {
  pkgContentType, pkgBlockedReason, pkgGreyed, blockedBadge
} from '../src/utils/platform.js';

test('content type comes from the backend, with pkg_type fallback', () => {
  assert.equal(pkgContentType({ content_type: 'homebrew', pkg_type: 'base' }), 'homebrew');
  assert.equal(pkgContentType({ pkg_type: 'dlc' }), 'dlc');
  assert.equal(pkgContentType({ pkg_type: 'update' }), 'update');
  assert.equal(pkgContentType({ pkg_type: 'base' }), 'game');
});

test('blocked reason follows the backend flags', () => {
  const hb = { platform: 'ps4', platform_blocked: true, platform_reason: "PS4 homebrew doesn't run on PS5" };
  assert.equal(pkgBlockedReason(hb, 'ps5'), "PS4 homebrew doesn't run on PS5");
  assert.equal(blockedBadge(pkgBlockedReason(hb, 'ps5'), 'ps4'), 'PS4 homebrew');
  const five = { platform: 'ps5', platform_blocked: true, platform_reason: 'PS5 only' };
  assert.equal(blockedBadge(pkgBlockedReason(five, 'ps4'), 'ps5'), 'PS5 only');
  assert.equal(pkgBlockedReason({ platform: 'ps4', platform_blocked: false }, 'ps5'), '');
  // Older backend without the flag: PS5 packages on a PS4.
  assert.equal(pkgBlockedReason({ platform: 'ps5' }, 'ps4'), 'PS5 only');
  assert.equal(pkgBlockedReason({ platform: 'ps4' }, 'ps5'), '');
});

test('greyed covers blocked and unavailable packages', () => {
  assert.equal(pkgGreyed({ platform: 'ps4', platform_blocked: false, unavailable: true }, 'ps5'), true);
  assert.equal(pkgGreyed({ platform: 'ps4', platform_blocked: false, unavailable: false }, 'ps5'), false);
});
