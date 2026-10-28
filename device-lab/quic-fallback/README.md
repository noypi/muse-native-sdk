# Device-lab gate — QUIC fallback (TC-NET-012)

External gate for FR-NET-007 / AC-NET-012. This directory carries the
procedure note only (`none` beyond this for the device gate); the hermetic
unit probe is `engine/net/tests/quic_fallback_test.cpp`.

## Procedure (`device-lab/quic-fallback`)

1. Target an HTTP/3-capable origin plus an HTTP/2-only origin.
2. Case A (QUIC open): request the HTTP/3 origin — expect negotiated
   `kHttp3` and a successful response.
3. Case B (QUIC blocked): block UDP/QUIC on the lab network — expect clean
   fallback to `kHttp2` with no error surfacing to the caller.
4. Case C (both blocked): block QUIC and force H2 off — expect fallback to
   `kHttp11` with no error surfacing to the caller.
5. Repeat cases A–C against both transports (default and Cronet) and confirm
   identical outcomes.

## Pass gate

Fallback matrix passes: QUIC open → `kHttp3`; QUIC blocked → `kHttp2`;
both blocked → `kHttp11`; same matrix green on both transports.
