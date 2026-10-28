// gRPC 5-byte-prefix wire helpers for RpcClient (FR-NET-009 / RULE-NET-001).
//
// Frame layout: byte 0 is the compressed flag (always 0 here), bytes 1..4
// are the big-endian payload length, followed by exactly that many bytes.
// Helpers never throw; malformed input reports false / a fallback code.

#include "net/rpc.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace muse {
namespace net {
namespace detail {

std::string EncodeGrpcFrame(const std::string& payload) {
  std::string out;
  out.reserve(5 + payload.size());
  out.push_back('\x00');
  const auto n = static_cast<std::uint32_t>(payload.size());
  out.push_back(static_cast<char>((n >> 24) & 0xFF));
  out.push_back(static_cast<char>((n >> 16) & 0xFF));
  out.push_back(static_cast<char>((n >> 8) & 0xFF));
  out.push_back(static_cast<char>(n & 0xFF));
  out += payload;
  return out;
}

namespace {

bool SplitFrames(const std::string& body, std::vector<std::string>* payloads,
                 bool single) {
  if (payloads == nullptr) return false;
  payloads->clear();
  std::size_t at = 0;
  const std::size_t n = body.size();
  if (single && n == 0) {
    // Empty body is only valid for an empty payload frame; fall through to
    // the length checks below so it reports false.
  }
  while (at < n) {
    if (n - at < 5) return false;
    const auto b1 = static_cast<std::uint8_t>(body[at + 1]);
    const auto b2 = static_cast<std::uint8_t>(body[at + 2]);
    const auto b3 = static_cast<std::uint8_t>(body[at + 3]);
    const auto b4 = static_cast<std::uint8_t>(body[at + 4]);
    const std::uint32_t len = (static_cast<std::uint32_t>(b1) << 24) |
                              (static_cast<std::uint32_t>(b2) << 16) |
                              (static_cast<std::uint32_t>(b3) << 8) |
                              static_cast<std::uint32_t>(b4);
    if (len > n - at - 5) return false;
    payloads->push_back(body.substr(at + 5, len));
    at += 5 + len;
    if (single) break;
  }
  if (single) {
    // Unary expects exactly one frame: trailing bytes are corruption.
    if (payloads->size() != 1 || at != n) {
      payloads->clear();
      return false;
    }
  } else if (at != n) {
    payloads->clear();
    return false;
  }
  return true;
}

}  // namespace

bool DecodeGrpcFrame(const std::string& body, std::string* payload) {
  if (payload == nullptr) return false;
  std::vector<std::string> one;
  if (!SplitFrames(body, &one, true)) return false;
  *payload = std::move(one[0]);
  return true;
}

bool DecodeGrpcFrames(const std::string& body,
                      std::vector<std::string>* payloads) {
  return SplitFrames(body, payloads, false);
}

int ParseGrpcStatusHeader(const std::string& value) {
  // Trailer values are small decimal codes; anything unparseable is an
  // internal error rather than OK so it can never mask a failure.
  if (value.empty()) return 13;
  int code = 0;
  for (char c : value) {
    if (c == ' ' || c == '\t') continue;
    if (c < '0' || c > '9') return 13;
    code = code * 10 + (c - '0');
    if (code > 16) return code;
  }
  return code;
}

std::string GrpcHeaderLookup(
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& key) {
  for (const auto& h : headers) {
    if (h.first == key) return h.second;
  }
  return "";
}

}  // namespace detail
}  // namespace net
}  // namespace muse
