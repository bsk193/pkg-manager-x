// Bounded uploads with file read-ahead. Explicit seeks always take priority.
export function createSegmentSender({ totalSegments, startSegment = 0, acknowledged, demandWindow = 0,
  maxInFlight = 1, readSegment, sendSegment, onError, onStats = () => {},
  shouldStop = () => false, retryDelayMs = 50 }) {
  const demands = new Set();
  const ahead = new Set();
  const flights = new Map();
  const retries = new Map();
  const limit = Math.min(2, Math.max(1, maxInFlight));
  if (demandWindow > 0 && totalSegments > 0) demands.add(0);
  let baseline = startSegment;
  let prepared = null;
  let pumping = false;
  let stopped = false;
  let lastStats = performance.now();
  const stats = { read_wait_us: 0, ack_latency_us: 0, ack_count: 0, sent_count: 0, window: limit };

  const inactive = () => stopped || shouldStop();
  const nextSegment = () => {
    for (const segment of demands) {
      if (!flights.has(segment) && !retries.get(segment)?.timer) return segment;
    }
    // A rejected speculative upload must not allow the baseline to race on.
    if (retries.size) return -1;
    if (demandWindow > 0) {
      for (const segment of ahead) {
        if (!acknowledged.has(segment) && !flights.has(segment)) return segment;
        ahead.delete(segment);
      }
      return -1;
    }
    while (baseline < totalSegments && (acknowledged.has(baseline) || flights.has(baseline))) baseline++;
    return baseline < totalSegments ? baseline : -1;
  };
  const prepare = (segment) => {
    if (!prepared || prepared.segment !== segment) {
      prepared = { segment, result: Promise.resolve().then(() => readSegment(segment))
        .then((buffer) => ({ buffer }), (error) => ({ error })) };
    }
    return prepared;
  };
  function flushStats(force = false) {
    const now = performance.now();
    if (force || now - lastStats >= 1000) {
      lastStats = now;
      onStats({ ...stats });
    }
  }
  function stop() {
    stopped = true;
    for (const retry of retries.values()) if (retry.timer) clearTimeout(retry.timer);
    retries.clear();
    flights.clear();
    prepared = null;
    demands.clear();
    ahead.clear();
  }

  async function pump() {
    if (inactive() || pumping) return;
    pumping = true;
    try {
      while (!inactive() && flights.size < limit) {
        const segment = nextSegment();
        if (segment < 0) break;
        demands.delete(segment);
        ahead.delete(segment);
        if (segment === baseline) baseline++;
        const read = retries.get(segment)?.read || prepare(segment);
        retries.delete(segment);
        if (prepared === read) prepared = null;
        const flight = { read, sentAt: null };
        flights.set(segment, flight); // Also coalesce seeks during the file read.
        const started = performance.now();
        const result = await read.result;
        stats.read_wait_us += Math.round((performance.now() - started) * 1000);
        if (inactive()) return;
        if (result.error) throw result.error;
        flight.sentAt = performance.now();
        stats.sent_count++;
        sendSegment(segment, result.buffer);
        // Keep a file read prepared even when both wire slots are occupied.
        const next = nextSegment();
        if (next >= 0) prepare(next);
        flushStats();
      }
    } catch (error) {
      stop();
      onError(error);
    } finally {
      pumping = false;
    }
  }

  return {
    pump, stop, flushStats: () => flushStats(true),
    hasPending: () => flights.size > 0 || retries.size > 0 || nextSegment() >= 0,
    request(segment) {
      if (inactive() || !Number.isInteger(segment) || segment < 0 || segment >= totalSegments) return;
      for (let s = segment + 1; s < Math.min(totalSegments, segment + demandWindow); s++) {
        if (!acknowledged.has(s) && !flights.has(s)) ahead.add(s);
      }
      while (ahead.size > demandWindow * 2) ahead.delete(ahead.values().next().value);
      if (!flights.has(segment)) demands.add(segment);
      void pump();
    },
    ack(segment) {
      const flight = flights.get(segment);
      if (inactive() || !flight || flight.sentAt === null) return;
      stats.ack_latency_us += Math.round((performance.now() - flight.sentAt) * 1000);
      stats.ack_count++;
      acknowledged.add(segment);
      demands.delete(segment);
      flights.delete(segment);
      flushStats();
      void pump();
    },
    busy(segment) {
      const flight = flights.get(segment);
      if (inactive() || !flight || flight.sentAt === null) return;
      flights.delete(segment);
      demands.add(segment);
      const retry = { read: flight.read, timer: null };
      retry.timer = setTimeout(() => {
        retry.timer = null;
        void pump();
      }, retryDelayMs);
      retries.set(segment, retry);
      // Other urgent seeks may proceed while this segment backs off.
      void pump();
    }
  };
}
