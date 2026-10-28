// Batch 2 contract tests — T4 (FR-NET-005 / AC-NET-008 WebRTC half,
// RULE-NET-002, TC-NET-008 unit half; external gate in
// device-lab/rtc-transitions/README.md).
//
// Hermetic: C++17 + engine/net headers only. DTLS-SRTP handles are
// consumed as opaque (owned elsewhere); the ICE-restart flap is an
// in-process script driven by onNetworkChanged(). No sockets, no
// libwebrtc linkage, no media codecs/encoders, no env/config reads, no
// filesystem, no live services, no wall-clock sleeps.
//
// Binds to the T4 `Interface:` line in plan.md (RtcState / PeerConnection
// / PeerConfig / Listener with onState + onReactiveSignal; every RtcState
// transition emits onReactiveSignal("rtc.state=<state>"); no media
// codecs). Type-shape ASSUMPTIONS (reconciliation rule: update call
// sites, not expectations, if the coder names these differently):
//   A-CFG:  `PeerConfig` is default-constructible (DTLS-SRTP handles
//           consumed as opaque; nothing to inject in this slice).
//   A-SYNC: listener callbacks fire before addAudioTrack()/
//           addVideoTrack()/onNetworkChanged()/setListener() return
//           (synchronous delivery). If the coder delivers callbacks on a
//           worker thread instead, the coder adds the wait/poll hook and
//           these call sites gain a wait — expected states/signals do not
//           change.
//   A-SIG:  the reactive signal for a state S is exactly the string
//           "rtc.state=<lowercase-suffix>" with mapping kNew→"rtc.state=new",
//           kConnecting→"rtc.state=connecting",
//           kConnected→"rtc.state=connected",
//           kReconnecting→"rtc.state=reconnecting",
//           kClosed→"rtc.state=closed".
//   A-TRACK: track liveness is observed purely through the listener: both
//           tracks stay alive across the flap iff the session recovers to
//           kConnected with no kClosed and no track-drop signal. The T4
//           Interface line exposes no track accessor, so no accessor is
//           asserted here (cf. T2 A-SUB precedent).
//   A-ERR:  invalid input (empty track id, empty network name) never
//           throws; misuse surfaces as onState(kClosed, reason) with a
//           non-empty reason, matching the T2/T3 family error shape.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "net/webrtc.h"

namespace {

using muse::net::PeerConfig;
using muse::net::PeerConnection;
using muse::net::RtcState;

std::string signal_for(RtcState s) {
  switch (s) {
    case RtcState::kNew:
      return "rtc.state=new";
    case RtcState::kConnecting:
      return "rtc.state=connecting";
    case RtcState::kConnected:
      return "rtc.state=connected";
    case RtcState::kReconnecting:
      return "rtc.state=reconnecting";
    case RtcState::kClosed:
      return "rtc.state=closed";
  }
  return "";
}

struct Captured {
  std::vector<std::pair<RtcState, std::string>> states;
  std::vector<std::string> signals;
};

typename PeerConnection::Listener capture_into(Captured* c) {
  typename PeerConnection::Listener l;
  l.onState = [c](RtcState s, std::string reason) {
    c->states.emplace_back(s, std::move(reason));
  };
  l.onReactiveSignal = [c](std::string sig) {
    c->signals.push_back(std::move(sig));
  };
  return l;
}

bool last_state_is(const Captured& c, RtcState s) {
  return !c.states.empty() && c.states.back().first == s;
}

// Every recorded state transition must have a matching reactive signal:
// signals[i] == signal_for(states[i]).
bool every_transition_has_exactly_one_signal(const Captured& c) {
  if (c.signals.size() != c.states.size()) return false;
  for (std::size_t i = 0; i < c.states.size(); ++i) {
    if (c.signals[i] != signal_for(c.states[i].first)) return false;
  }
  return true;
}

bool state_order_is(const Captured& c, const std::vector<RtcState>& want) {
  if (c.states.size() != want.size()) return false;
  for (std::size_t i = 0; i < want.size(); ++i) {
    if (c.states[i].first != want[i]) return false;
  }
  return true;
}

bool check_tracks_added_before_flap() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addVideoTrack("cam-1");
  } catch (...) {
    return false;
  }
  // Adding tracks must not close the session and must not emit signals
  // by itself (signals belong to state transitions only).
  for (const auto& st : c.states) {
    if (st.first == RtcState::kClosed) return false;
  }
  return c.signals.empty();
}

bool check_flap_yields_connected_reconnecting_connected() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addVideoTrack("cam-1");
    c.states.clear();
    c.signals.clear();
    pc.onNetworkChanged("cellular");  // scripted Wi-Fi→cellular flap
  } catch (...) {
    return false;
  }
  if (!state_order_is(c, {RtcState::kReconnecting, RtcState::kConnected}))
    return false;
  if (!every_transition_has_exactly_one_signal(c)) return false;
  // Both tracks alive: recovery to kConnected with no kClosed and no
  // track-drop anywhere in the full trace (A-TRACK).
  return last_state_is(c, RtcState::kConnected);
}

bool check_reactive_signal_per_transition_exact_strings() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addVideoTrack("cam-1");
    c.states.clear();
    c.signals.clear();
    pc.onNetworkChanged("cellular");
  } catch (...) {
    return false;
  }
  if (c.signals.size() != 2) return false;
  if (c.signals[0] != "rtc.state=reconnecting") return false;
  if (c.signals[1] != "rtc.state=connected") return false;
  return every_transition_has_exactly_one_signal(c);
}

bool check_second_flap_recovers_again() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addVideoTrack("cam-1");
    pc.onNetworkChanged("cellular");
    c.states.clear();
    c.signals.clear();
    pc.onNetworkChanged("wifi");
  } catch (...) {
    return false;
  }
  if (!state_order_is(c, {RtcState::kReconnecting, RtcState::kConnected}))
    return false;
  return every_transition_has_exactly_one_signal(c);
}

bool check_same_network_change_is_idempotent() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.onNetworkChanged("cellular");
    std::size_t n = c.states.size();
    pc.onNetworkChanged("cellular");  // no change: no new transitions
    if (c.states.size() != n) return false;
  } catch (...) {
    return false;
  }
  return every_transition_has_exactly_one_signal(c);
}

bool check_empty_track_id_reports_kClosed_never_throws() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("");
  } catch (...) {
    return false;
  }
  return last_state_is(c, RtcState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_empty_video_track_id_reports_kClosed_never_throws() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addVideoTrack("");
  } catch (...) {
    return false;
  }
  return last_state_is(c, RtcState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_empty_network_name_reports_kClosed_never_throws() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.onNetworkChanged("");
  } catch (...) {
    return false;
  }
  return last_state_is(c, RtcState::kClosed) &&
         !c.states.back().second.empty();
}

bool check_duplicate_track_id_never_throws() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addAudioTrack("mic-1");  // duplicate: must not throw
  } catch (...) {
    return false;
  }
  // Either tolerated (no kClosed) or rejected with reason — but never
  // a throw, and signal/state counts must stay consistent.
  return every_transition_has_exactly_one_signal(c);
}

bool check_no_kClosed_without_cause_across_flap() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    pc.addVideoTrack("cam-1");
    pc.onNetworkChanged("cellular");
  } catch (...) {
    return false;
  }
  for (const auto& st : c.states) {
    if (st.first == RtcState::kClosed) return false;
  }
  return every_transition_has_exactly_one_signal(c);
}

bool check_audio_only_session_survives_flap() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addAudioTrack("mic-1");
    c.states.clear();
    c.signals.clear();
    pc.onNetworkChanged("cellular");
  } catch (...) {
    return false;
  }
  if (!state_order_is(c, {RtcState::kReconnecting, RtcState::kConnected}))
    return false;
  return every_transition_has_exactly_one_signal(c);
}

bool check_video_only_session_survives_flap() {
  PeerConfig cfg;
  PeerConnection pc(cfg);
  Captured c;
  pc.setListener(capture_into(&c));
  try {
    pc.addVideoTrack("cam-1");
    c.states.clear();
    c.signals.clear();
    pc.onNetworkChanged("cellular");
  } catch (...) {
    return false;
  }
  if (!state_order_is(c, {RtcState::kReconnecting, RtcState::kConnected}))
    return false;
  return every_transition_has_exactly_one_signal(c);
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_tracks_added_before_flap);
  RUN_CASE(check_flap_yields_connected_reconnecting_connected);
  RUN_CASE(check_reactive_signal_per_transition_exact_strings);
  RUN_CASE(check_second_flap_recovers_again);
  RUN_CASE(check_same_network_change_is_idempotent);
  RUN_CASE(check_empty_track_id_reports_kClosed_never_throws);
  RUN_CASE(check_empty_video_track_id_reports_kClosed_never_throws);
  RUN_CASE(check_empty_network_name_reports_kClosed_never_throws);
  RUN_CASE(check_duplicate_track_id_never_throws);
  RUN_CASE(check_no_kClosed_without_cause_across_flap);
  RUN_CASE(check_audio_only_session_survives_flap);
  RUN_CASE(check_video_only_session_survives_flap);
  if (failures > 0) {
    std::printf("webrtc_session_test: %d failing\n", failures);
    return 1;
  }
  std::printf("webrtc_session_test: all passing\n");
  return 0;
}
