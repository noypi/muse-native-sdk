# device-lab/rpc-interop — external TC-NET-017 procedure (T5/T6)

Unit evidence for AC-NET-017 lives in `engine/net/tests/` inside this
checkout (`grpc_interop_test.cpp` golden unary + 3-message server-stream
fixtures and pooled-connection multiplexing for T5,
`graphql_subscription_test.cpp` ordered/resumable subscriptions, APQ,
batching, and cache-key shape for T6). Those unit tests use in-process
fakes (scripted reference-vector bodies, scripted loss/reorder, canned
trailers) and cannot observe a real gRPC/Connect server, real HTTP/2
multiplexing on the wire, or real SSE reconnection across a network
change — that observation belongs here, on hardware, and is owned by
the device lab (execution).

## Procedure (run by a human or the lab, not CI)

1. Stand up the lab-local reference server (self-signed CA installed on
   the device; no public network): one gRPC service with a unary method
   returning the golden response and a server-streaming method emitting
   the 3-message golden sequence, plus a Connect endpoint serving the
   same methods under `Content-Type: application/proto`.
2. gRPC: from the target app shell embedding the Batch 3
   `muse::net::RpcClient`, issue the golden unary call against the
   reference server over both `application/grpc+proto` and
   `application/proto`; then open the server-streaming call and record
   the 3 messages in order. Open N = 8 concurrent streams and record
   the server-side connection count (exactly one HTTP/2 connection).
   Force one call to fail (stop the reference method mid-stream) and
   record the surfaced `GrpcStatus`.
3. GraphQL/SSE: attach the app's `muse::net::GraphQLClient` to the
   lab-local GraphQL endpoint; start a subscription, force a
   Wi-Fi→cellular handoff mid-stream, then resume with the stored
   `LastEventId` and record every update id in order. Run the APQ flow
   (hash-first, full query on `PersistedQueryNotFound`) and a 2-query
   batch against the endpoint.

## Pass gate (TC-NET-017)

- gRPC: the unary response bytes match the golden fixture on both
  content types; the server stream delivers the 3 golden messages in
  order; 8 concurrent streams multiplex on exactly one pooled
  connection; the mid-stream failure surfaces as non-OK `GrpcStatus`
  (never an exception/crash).
- GraphQL: after the handoff + resume, every update is delivered in
  order with zero duplicates and zero gaps (dedupe-by-id holds on the
  wire); APQ completes hash-then-full; the 2-query batch completes.
- No unit-test behavior is re-litigated here: byte framing, error
  shapes, and key shapes are proven in `engine/net/tests/`; this gate
  proves the same behavior against real transports.

## Recording the result

Append the observed response bytes, message traces, connection counts,
update id logs, and any loss/duplication to the run record
(`workflow.local/runs/fr-net-005-fr-net-009-fr-net-010/run.md`) with the
device model, OS build, radio conditions, and date. A gate miss is a
device-lab finding, not a unit-test failure: file it and route to the
planner.
