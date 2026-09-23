import test from 'node:test';
import assert from 'node:assert/strict';
import { setImmediate as tick, setTimeout as delay } from 'node:timers/promises';
import { createSegmentSender } from '../frontend/src/utils/segmentSender.js';

function fixture(t, options = {}) {
  const sent = [], reads = [], errors = [];
  const acknowledged = new Set();
  const sender = createSegmentSender({
    totalSegments: 5,
    acknowledged,
    readSegment: async (segment) => { reads.push(segment); return new Uint8Array([segment]); },
    sendSegment: (segment, buffer) => { sent.push(segment); assert.equal(buffer[0], segment); },
    onError: (error) => errors.push(error),
    ...options
  });
  t.after(() => sender.stop());
  return { sender, sent, reads, errors, acknowledged };
}

test('coalesces seeks for an upload in flight but honors a later eviction seek', async (t) => {
  const { sender, sent } = fixture(t);
  await sender.pump();
  sender.request(0);
  sender.request(0);
  sender.ack(0);
  await tick();
  assert.deepEqual(sent, [0, 1]);
  sender.request(0); // After ACK: the cache can legitimately need it again.
  sender.ack(1);
  await tick();
  assert.deepEqual(sent, [0, 1, 0]);
});

test('reads ahead during network transfer without sending before the ACK', async (t) => {
  const { sender, sent, reads } = fixture(t);
  await sender.pump();
  await tick();
  assert.deepEqual(sent, [0]);
  assert.deepEqual(reads, [0, 1]);
  sender.ack(0);
  await tick();
  assert.deepEqual(sent, [0, 1]);
  assert.equal(reads.filter((s) => s === 1).length, 1);
});

test('urgent seek preempts speculative baseline read', async (t) => {
  const { sender, sent } = fixture(t);
  await sender.pump();
  sender.request(4);
  sender.ack(0);
  await tick();
  assert.deepEqual(sent, [0, 4]);
  sender.ack(4);
  await tick();
  assert.deepEqual(sent, [0, 4, 1]);
});

test('busy resends a previously ACKed segment and reuses its buffer', async (t) => {
  const { sender, sent, reads } = fixture(t, {
    startSegment: 5, acknowledged: new Set([0, 1, 2, 3, 4]), retryDelayMs: 5
  });
  sender.request(2);
  await tick();
  sender.busy(2);
  assert.deepEqual(sent, [2]);
  await delay(30);
  assert.deepEqual(sent, [2, 2]);
  assert.deepEqual(reads, [2]);
  sender.ack(2);
  assert.equal(sender.hasPending(), false);
});

test('cancellation during file read prevents a late send', async (t) => {
  let resolveRead;
  const { sender, sent } = fixture(t, { readSegment: () => new Promise((resolve) => { resolveRead = resolve; }) });
  const pending = sender.pump();
  await tick();
  sender.stop();
  resolveRead(new Uint8Array([0]));
  await pending;
  assert.deepEqual(sent, []);
});

test('speculative read failure is reported when that segment is needed', async (t) => {
  const error = new Error('file read failed');
  const { sender, errors } = fixture(t, {
    readSegment: async (s) => { if (s === 1) throw error; return new Uint8Array([s]); }
  });
  await sender.pump();
  await tick();
  assert.deepEqual(errors, []);
  sender.ack(0);
  await tick();
  assert.deepEqual(errors, [error]);
});

test('reader seeks arriving during every transfer do not double the payload', async (t) => {
  const totalSegments = 256;
  let transfers = 0;
  let finish;
  const complete = new Promise((resolve) => { finish = resolve; });
  const acknowledged = new Set();
  const sender = createSegmentSender({
    totalSegments, acknowledged,
    readSegment: async (segment) => new Uint8Array(1024).fill(segment),
    sendSegment(segment, buffer) {
      transfers++;
      assert.equal(buffer.length, 1024);
      assert.ok(buffer.every((byte) => byte === segment));
      queueMicrotask(() => {
        sender.request(segment); // Console requests bytes while they are arriving.
        sender.request(segment);
        sender.ack(segment);
        if (acknowledged.size === totalSegments) finish();
      });
    },
    onError: (error) => { throw error; }
  });
  t.after(() => sender.stop());
  await sender.pump();
  await complete;
  assert.equal(transfers, totalSegments);
});
