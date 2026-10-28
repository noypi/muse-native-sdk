# Device-lab gate — connectivity state (TC-NET-026)

External gate for FR-NET-013 / AC-NET-026. This directory carries the
procedure note only (`none` beyond this for the device gate); the hermetic
unit probe is `engine/net/tests/connectivity_state_test.cpp`.

## Procedure (`device-lab/connectivity-state`)

1. Target a device with mobile data plus Wi-Fi (metered toggle available).
2. Case A (captive portal): join a captive-portal network — expect the UI
   signal for `kOnlineUnvalidated` (limited connectivity shown, no full
   prefetch).
3. Case B (metered toggle): on a validated connection, toggle metered on
   then off — expect `shouldPrefetch` false while metered and true once
   unmetered.
4. Case C (cleartext): request an `http://` URL on a denied domain —
   expect it blocked per the network security config; request an allowed
   domain — expect it to pass.

## Pass gate

Captive-portal event emits `kOnlineUnvalidated` to the listener; the
metered toggle flips `shouldPrefetch` false→true; a cleartext-denied
domain is blocked and an allowed domain passes.
