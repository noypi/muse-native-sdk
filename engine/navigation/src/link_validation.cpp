#include "navigation/link_validation.h"

namespace {

LinkVerdict Deny(LinkError error) {
  LinkVerdict verdict;
  verdict.allowed = false;
  verdict.error = error;
  return verdict;
}

bool IsHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

// Percent-decodes %XX sequences (hex case-insensitive); a lone or malformed
// '%' is preserved literally. Never throws.
std::string PercentDecode(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '%' && i + 2 < raw.size() && IsHex(raw[i + 1]) &&
        IsHex(raw[i + 2])) {
      char decoded =
          static_cast<char>(HexValue(raw[i + 1]) * 16 + HexValue(raw[i + 2]));
      out.push_back(decoded);
      i += 2;
    } else {
      out.push_back(raw[i]);
    }
  }
  return out;
}

bool IsCapture(const std::string& segment) {
  return segment.size() > 1 && segment[0] == ':';
}

bool HasCredentials(const std::string& raw) {
  if (raw.find('@') != std::string::npos) return true;
  std::string decoded = PercentDecode(raw);
  return decoded.find('@') != std::string::npos;
}

bool IsTraversal(const std::string& raw) {
  // Raw backslash is never a legitimate segment character here.
  if (raw.find('\\') != std::string::npos) return true;
  if (raw == "." || raw == "..") return true;
  std::string decoded = PercentDecode(raw);
  if (decoded == "." || decoded == "..") return true;
  // An encoded separator smuggled inside one segment, or a decoded ".."
  // paired with a separator, denotes traversal.
  bool decodedHasSep = decoded.find('/') != std::string::npos ||
                       decoded.find('\\') != std::string::npos;
  if (decoded != raw && decodedHasSep) return true;
  if (decoded.find("../") != std::string::npos ||
      decoded.find("..\\") != std::string::npos ||
      decoded.find("/../") != std::string::npos ||
      decoded.find("\\..\\") != std::string::npos) {
    return true;
  }
  return false;
}

bool HasControlChars(const std::string& s) {
  for (unsigned char c : s) {
    if (c < 0x20 || c == 0x7F) return true;
  }
  return false;
}

bool RouteMatches(const RouteEntry& route, const std::string& host,
                  const std::vector<std::string>& path) {
  if (route.host != host) return false;
  if (route.path.size() != path.size()) return false;
  for (std::size_t i = 0; i < route.path.size(); ++i) {
    if (IsCapture(route.path[i])) {
      if (path[i].empty()) return false;
      continue;
    }
    if (route.path[i] != path[i]) return false;
  }
  return true;
}

}  // namespace

LinkVerdict ValidateLink(const LinkCandidate& in,
                         const LinkAllowlist& allowlist) noexcept {
  // 1. Size caps.
  std::size_t uriBytes = in.scheme.size() + 3 + in.host.size();
  for (const std::string& seg : in.path) {
    if (seg.size() > allowlist.maxParamBytes) return Deny(LinkError::OVERSIZED);
    uriBytes += 1 + seg.size();
  }
  if (!in.query.empty()) uriBytes += 1;
  for (const auto& kv : in.query) {
    if (kv.first.size() > allowlist.maxParamBytes ||
        kv.second.size() > allowlist.maxParamBytes) {
      return Deny(LinkError::OVERSIZED);
    }
    uriBytes += kv.first.size() + 1 + kv.second.size() + 1;
  }
  if (uriBytes > allowlist.maxUriBytes) return Deny(LinkError::OVERSIZED);

  // 2. Scheme allowlist.
  bool schemeOk = false;
  for (const std::string& s : allowlist.schemes) {
    if (s == in.scheme) {
      schemeOk = true;
      break;
    }
  }
  if (!schemeOk) return Deny(LinkError::SCHEMA_MISMATCH);

  // 3a. Credentials scan (raw + percent-decoded).
  if (HasCredentials(in.host)) return Deny(LinkError::CREDENTIALS_PRESENT);
  for (const std::string& seg : in.path) {
    if (HasCredentials(seg)) return Deny(LinkError::CREDENTIALS_PRESENT);
  }
  for (const auto& kv : in.query) {
    if (HasCredentials(kv.first) || HasCredentials(kv.second)) {
      return Deny(LinkError::CREDENTIALS_PRESENT);
    }
  }

  // 3b. Traversal scan (raw + percent-decoded, %2e/%2f/%5c insensitive).
  if (IsTraversal(in.host)) return Deny(LinkError::TRAVERSAL_DETECTED);
  for (const std::string& seg : in.path) {
    if (IsTraversal(seg)) return Deny(LinkError::TRAVERSAL_DETECTED);
  }
  for (const auto& kv : in.query) {
    if (IsTraversal(kv.first) || IsTraversal(kv.second)) {
      return Deny(LinkError::TRAVERSAL_DETECTED);
    }
  }

  // Malformed inputs: empty host, empty segments, control characters.
  if (in.host.empty()) return Deny(LinkError::MALFORMED_URI);
  if (HasControlChars(in.scheme) || HasControlChars(in.host)) {
    return Deny(LinkError::MALFORMED_URI);
  }
  for (const std::string& seg : in.path) {
    if (seg.empty()) return Deny(LinkError::MALFORMED_URI);
    if (HasControlChars(seg)) return Deny(LinkError::MALFORMED_URI);
  }
  for (const auto& kv : in.query) {
    if (HasControlChars(kv.first) || HasControlChars(kv.second)) {
      return Deny(LinkError::MALFORMED_URI);
    }
  }

  // 4. Host+path allowlist match (exact segment arity, :name captures).
  bool matched = false;
  for (const RouteEntry& route : allowlist.routes) {
    if (RouteMatches(route, in.host, in.path)) {
      matched = true;
      break;
    }
  }
  if (!matched) return Deny(LinkError::UNKNOWN_ROUTE);

  // 5. Asset-links verification required for https entries.
  if (in.scheme == "https" && !in.assetLinksVerified) {
    return Deny(LinkError::UNVERIFIED_HOST);
  }

  LinkVerdict verdict;
  verdict.allowed = true;
  verdict.error = LinkError::NONE;
  return verdict;
}
