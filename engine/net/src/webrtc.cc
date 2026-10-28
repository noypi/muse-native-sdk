#include "net/webrtc.h"

#include <vector>

namespace muse {
namespace net {

PeerConnection::PeerConnection(PeerConfig cfg) : config_(cfg) {}

void PeerConnection::setListener(Listener l) {
  try {
    listener_ = std::move(l);
  } catch (...) {
  }
}

void PeerConnection::transition(RtcState state, const std::string& reason) {
  try {
    state_ = state;
    if (listener_.onState) listener_.onState(state, reason);
    if (listener_.onReactiveSignal)
      listener_.onReactiveSignal(signalFor(state));
  } catch (...) {
  }
}

std::string PeerConnection::signalFor(RtcState state) {
  switch (state) {
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

namespace {
bool contains(const std::vector<std::string>& v, const std::string& id) {
  for (const auto& e : v) {
    if (e == id) return true;
  }
  return false;
}
}  // namespace

void PeerConnection::addAudioTrack(std::string id) {
  try {
    if (id.empty()) {
      if (state_ != RtcState::kClosed)
        transition(RtcState::kClosed, "empty audio track id");
      return;
    }
    if (contains(audio_tracks_, id)) return;  // duplicate: tolerate
    audio_tracks_.push_back(std::move(id));
  } catch (...) {
  }
}

void PeerConnection::addVideoTrack(std::string id) {
  try {
    if (id.empty()) {
      if (state_ != RtcState::kClosed)
        transition(RtcState::kClosed, "empty video track id");
      return;
    }
    if (contains(video_tracks_, id)) return;  // duplicate: tolerate
    video_tracks_.push_back(std::move(id));
  } catch (...) {
  }
}

void PeerConnection::onNetworkChanged(std::string net) {
  try {
    if (net.empty()) {
      if (state_ != RtcState::kClosed)
        transition(RtcState::kClosed, "empty network name");
      return;
    }
    if (net == current_network_) return;  // idempotent
    if (state_ == RtcState::kClosed) return;
    current_network_ = net;
    // Scripted ICE-restart flap: reconnect then recover synchronously.
    transition(RtcState::kReconnecting, "network changed");
    if (state_ == RtcState::kClosed) return;
    transition(RtcState::kConnected, "reconnected");
  } catch (...) {
  }
}

}  // namespace net
}  // namespace muse
