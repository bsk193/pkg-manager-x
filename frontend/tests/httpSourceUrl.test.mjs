import test from 'node:test';
import assert from 'node:assert/strict';
import { isCatalogFileUrl, normalizeUrl } from '../src/utils/httpSourceUrl.js';

test('catalog .json URLs are kept as entered', () => {
  const u = 'https://bsk193.github.io/pegasus-dl-source/catalog-ps4.json';
  assert.equal(normalizeUrl(u), u);
  assert.equal(normalizeUrl(`  ${u}  `), u);
  assert.equal(normalizeUrl(`${u}?token=abc`), `${u}?token=abc`);
  assert.equal(normalizeUrl('example.com/list.JSON'), 'http://example.com/list.JSON');
  assert.ok(isCatalogFileUrl(`${u}?t=1`));
});

test('folders and index.json become folder URLs', () => {
  assert.equal(normalizeUrl('http://host/pkgs'), 'http://host/pkgs/');
  assert.equal(normalizeUrl('http://host/pkgs/index.json'), 'http://host/pkgs/');
  assert.equal(normalizeUrl('http://host/pkgs/?x=1'), 'http://host/pkgs/');
  assert.equal(normalizeUrl('host:8080'), 'http://host:8080/');
  assert.equal(normalizeUrl(''), '');
  assert.ok(!isCatalogFileUrl('http://host/index.json'));
});
