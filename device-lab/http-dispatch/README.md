# device-lab/http-dispatch — external TC-NET-007 procedure (T3)

Unit evidence for AC-NET-007 lives in `engine/net/tests/` inside this
checkout (`transport_api_test.cpp` pool-reuse cases for T2,
`http_dispatch_test.cpp` dispatch-budget probe for T3). Those unit tests use
in-memory fakes and cannot observe a real JS-to-native hop or real socket
reuse — that observation belongs here, on hardware, and is owned by perf
(the <1ms budget definition) and the device lab (execution).

## Procedure (run by a human or the lab, not CI)

1. Build the target app shell embedding the Batch 1 `muse::net::HttpClient`
   (default transport) against a lab-local HTTPS fixture server
   (self-signed CA installed on the device; no public network).
2. From JS, issue 10 sequential GETs to the same origin
   (e.g. `https://fixture.lab.invalid/items`), awaiting each response.
3. Capture, per request: (a) JS-to-native dispatch latency in nanoseconds
   (recorded via the `RecordDispatchNanos` hook), and (b) the underlying
   connection/tunnel identifier from the transport log.

## Pass gate (TC-NET-007)

- Dispatch: every sampled `WithinBudget(ns)` call returns true — all
  dispatches complete in under 1,000,000 ns (<1ms).
- Reuse: all 10 sequential same-origin requests share one connection
  identifier (exactly 1 connection opened for the origin).

## Recording the result

Append the measured dispatch p50/pmax and the observed connection count to
the run record (`workflow.local/runs/fr-net-004-fr-net-001-fr-net-008/run.md`)
with the device model, OS build, and date. A gate miss is a perf/device-lab
finding, not a unit-test failure: file it and route to the planner.
