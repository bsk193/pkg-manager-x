// One segment on the wire, with the next file read overlapped with its ACK.
// A seek during an in-flight upload is already being satisfied; a seek after
// its ACK must still be honored because the console may have evicted it.
export function createSegmentSender({ totalSegments, startSegment = 0, acknowledged, demandWindow = 0,
  readSegment, sendSegment, onError, shouldStop = () => false, retryDelayMs = 50 }) {
  const demands = new Set();
  const ahead = new Set();
  if (demandWindow > 0 && totalSegments > 0) demands.add(0);
  let baseline = startSegment;
  let inFlight = -1;
  let inFlightRead = null;
  let prepared = null;
  let pumping = false;
  let stopped = false;
  let retryTimer = null;

  const inactive = () => stopped || shouldStop();
  const nextSegment = () => {
    if (demands.size) return demands.values().next().value;
    if (demandWindow > 0) {
      for (const segment of ahead) {
        if (!acknowledged.has(segment) && segment !== inFlight) return segment;
        ahead.delete(segment);
      }
      return -1;
    }
    while (baseline < totalSegments && (acknowledged.has(baseline) || baseline === inFlight)) baseline++;
    return baseline < totalSegments ? baseline : -1;
  };
  const prepare = (segment) => {
    if (!prepared || prepared.segment !== segment) {
      // Speculative read failures are reported only if that segment is sent.
      prepared = { segment, result: Promise.resolve().then(() => readSegment(segment))
        .then((buffer) => ({ buffer }), (error) => ({ error })) };
    }
    return prepared;
  };

  function stop() {
    stopped = true;
    if (retryTimer !== null) clearTimeout(retryTimer);
    retryTimer = null;
    prepared = null;
    inFlightRead = null;
    demands.clear();
    ahead.clear();
  }

  async function pump() {
    if (inactive() || pumping || inFlight >= 0 || retryTimer !== null) return;
    const segment = nextSegment();
    if (segment < 0) return;
    pumping = true;
    inFlight = segment;
    demands.delete(segment);
    ahead.delete(segment);
    if (segment === baseline) baseline++;
    inFlightRead = prepare(segment);
    prepared = null;
    try {
      const result = await inFlightRead.result;
      if (inactive()) return;
      if (result.error) throw result.error;
      sendSegment(segment, result.buffer);
      if (!inactive() && inFlight >= 0 && retryTimer === null) {
        const next = nextSegment();
        if (next >= 0) prepare(next);
      }
    } catch (error) {
      stop();
      onError(error);
    } finally {
      pumping = false;
      // An ACK can arrive while an asynchronous send/read is unwinding.
      if (!inactive() && inFlight < 0) void pump();
    }
  }

  return {
    pump,
    stop,
    hasPending: () => inFlight >= 0 || nextSegment() >= 0,
    request(segment) {
      if (inactive() || !Number.isInteger(segment) || segment < 0 || segment >= totalSegments) return;
      // Bound speculative work across both readers and discard old windows
      // on pivots. Previously ACKed data is resent only on explicit demand.
      for (let s = segment + 1; s < Math.min(totalSegments, segment + demandWindow); s++) {
        if (!acknowledged.has(s) && s !== inFlight) ahead.add(s);
      }
      while (ahead.size > demandWindow * 2) ahead.delete(ahead.values().next().value);
      if (segment !== inFlight) demands.add(segment);
      void pump();
    },
    ack(segment) {
      if (inactive() || segment !== inFlight) return;
      acknowledged.add(segment);
      demands.delete(segment);
      inFlight = -1;
      inFlightRead = null;
      void pump();
    },
    busy(segment) {
      if (inactive() || segment !== inFlight) return;
      // Reuse the rejected buffer, including resends of previously ACKed
      // segments. An old ACK does not mean the segment is still resident.
      prepared = inFlightRead;
      inFlightRead = null;
      inFlight = -1;
      demands.add(segment);
      retryTimer = setTimeout(() => {
        retryTimer = null;
        void pump();
      }, retryDelayMs);
    }
  };
}
