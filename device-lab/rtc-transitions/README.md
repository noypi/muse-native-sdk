# device-lab/rtc-transitions — external TC-NET-008 procedure (T4)

Unit evidence for AC-NET-008 lives in `engine/net/tests/` inside this
checkout (`webtransport_test.cpp` migration/dedupe cases for T3,
`webrtc_session_test.cpp` flap/signal cases for T4). Those unit tests use
in-process fakes (scripted flap, canned datagrams, opaque DTLS-SRTP
handles) and cannot observe real Wi-Fi→cellular handoff, real ICE
restart, or real libwebrtc media flow — that observation belongs here,
on hardware, and is owned by the device lab (execution).

## Procedure (run by a human or the lab, not CI)

1. Build the target app shell embedding the Batch 2 `muse::net::WebTransport`
   and `muse::net::PeerConnection` against a lab-local fixture peer
   (self-signed CA installed on the device; no public network).
2. WebTransport: open a session, stream datagrams, then force a Wi-Fi→cellular
   handoff mid-stream (toggle the active network or throttle Wi-Fi to
   disconnect while cellular is up). Record the `WtState` event order and
   every datagram delivered to `onDatagram`.
3. WebRTC: attach one audio track and one video track, establish the
   `kConnected` session, then force the same Wi-Fi→cellular handoff.
   Record the `RtcState` event order, every `onReactiveSignal` string, and
   whether both tracks resume media flow after recovery (remote frame
   counters advance; no re-add of tracks required).

## Pass gate (TC-NET-008)

- WebTransport: the state trace contains `kMigrating` followed by
  `kConnected` with no terminal `kClosed`; every datagram sent before and
  across the handoff is delivered exactly once, in send order (zero loss,
  zero duplication — dedupe by stream offset holds on the wire).
- WebRTC: the state trace contains `Connected→Reconnecting→Connected`
  with both tracks alive after recovery (frame counters advance without
  re-adding tracks); every transition emitted exactly one reactive signal
  `onReactiveSignal("rtc.state=<state>")` matching the transition, in order.
- No media codecs are exercised here (T4 builds none); audio/video
  assertions are transport-level (flow resumes), not quality-level.

## Recording the result

Append the observed state traces, signal logs, datagram/track counts, and
any loss/duplication to the run record
(`workflow.local/runs/fr-net-005-fr-net-009-fr-net-010/run.md`) with the
device model, OS build, radio conditions, and date. A gate miss is a
device-lab finding, not a unit-test failure: file it and route to the
planner.
