#pragma once

// WebRTC audio/video session + reactive reconnect reporting
// (FR-NET-005 / AC-NET-008, TC-NET-008 unit half).
//
// Scripted-flap model: DTLS-SRTP handles are opaque (owned elsewhere) and
// the Wi-Fi→cellular handoff is driven in-process via onNetworkChanged().
// Every RtcState transition emits exactly one reactive signal
// onReactiveSignal("rtc.state=<state>") for the reactive layer. No media
// codecs are built in this slice.

#include <functional>
#include <string>
#include <vector>

namespace muse {
namespace net {

enum class RtcState { kNew, kConnecting, kConnected, kReconnecting, kClosed };

// Opaque session config in this slice: DTLS-SRTP handles (if any) are
// consumed as opaque and owned elsewhere, so nothing needs injecting.
struct PeerConfig {};

class PeerConnection {
 public:
  struct Listener {
    std::function<void(RtcState, std::string)> onState;
    std::function<void(std::string)> onReactiveSignal;
  };

  explicit PeerConnection(PeerConfig cfg);
  void addAudioTrack(std::string id);
  void addVideoTrack(std::string id);
  void onNetworkChanged(std::string net);
  void setListener(Listener l);

 private:
  void transition(RtcState state, const std::string& reason);
  static std::string signalFor(RtcState state);

  PeerConfig config_;
  Listener listener_;
  RtcState state_ = RtcState::kNew;
  std::string current_network_ = "wifi";
  std::vector<std::string> audio_tracks_;
  std::vector<std::string> video_tracks_;
};

}  // namespace net
}  // namespace muse
