#include "navigation/deferred_link.h"

#include <cstdio>

#include "navigation/uri_validation.h"

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

// Default manifest mirroring the two-host AC-NAV-013/014 fixture shape so a
// path-only store still re-runs T3 ValidateUri on drain: shop + social over
// https, product route /p/.* (optional int qty), profile route /u/.*.
RouteManifest DefaultDeferredManifest() {
  RouteManifest m;
  m.hosts = {"shop.example.com", "social.example.com"};
  m.schemes = {"https"};
  m.packageName = "com.example.app";
  m.sha256CertFingerprints = {"AA:BB:CC:DD"};
  m.autoVerify = true;
  ManifestRoute product;
  product.host = "shop.example.com";
  product.pathPattern = "/p/.*";
  product.routeId = "product";
  QueryParam qty;
  qty.name = "qty";
  qty.type = ParamType::Int;
  qty.required = false;
  product.query = {qty};
  ManifestRoute profile;
  profile.host = "social.example.com";
  profile.pathPattern = "/u/.*";
  profile.routeId = "profile";
  m.routes = {product, profile};
  return m;
}

// Percent-decodes s. Never throws: invalid % sequences are copied through
// literally so the caller maps them to null instead of throwing.
std::string UrlDecodeNoThrow(const std::string& s) noexcept {
  std::string out;
  out.reserve(s.size());
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '+') {
      out.push_back(' ');
    } else if (c == '%' && i + 2 < s.size()) {
      int hi = hexVal(s[i + 1]);
      int lo = hexVal(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(c);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool WriteFileSync(const std::string& path, const std::string& data) {
#if defined(__unix__) || defined(__APPLE__)
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  size_t done = 0;
  while (done < data.size()) {
    ssize_t n = ::write(fd, data.data() + done, data.size() - done);
    if (n <= 0) break;
    done += static_cast<size_t>(n);
  }
  bool complete = (done == data.size()) && ::fsync(fd) == 0;
  ::close(fd);
  return complete;
#else
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  bool ok = true;
  if (!data.empty()) {
    ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  }
  ok = (std::fflush(f) == 0) && ok;
  std::fclose(f);
  return ok;
#endif
}

std::string ReadFileAll(const std::string& path, bool* ok) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    *ok = false;
    return "";
  }
  std::string out;
  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  std::fclose(f);
  *ok = true;
  return out;
}

void ClearStore(const std::string& path) {
  // Once-only: the stored link must not survive a drain, valid or not.
  if (std::remove(path.c_str()) == 0) return;
  // Fall back to truncating if remove failed (e.g. permissions): an empty
  // store drains to nullopt.
  WriteFileSync(path, "");
}

}  // namespace

DeferredLinkStore::DeferredLinkStore(const std::string& storePath)
    : storePath_(storePath), manifest_(DefaultDeferredManifest()) {}

DeferredLinkStore::DeferredLinkStore(const std::string& storePath,
                                     const RouteManifest& manifest)
    : storePath_(storePath), manifest_(manifest) {}

void DeferredLinkStore::CaptureReferrer(const std::string& referrerUrl) {
  static_cast<void>(WriteFileSync(storePath_, referrerUrl));
}

std::optional<std::string> DeferredLinkStore::DrainPendingLaunch() {
  try {
    bool ok = false;
    std::string stored = ReadFileAll(storePath_, &ok);
    if (!ok || stored.empty()) {
      return std::nullopt;
    }
    ClearStore(storePath_);
    ValidateResult r = ValidateUri(stored, manifest_);
    if (!r.accepted) {
      return std::nullopt;  // landing fallback, never error screen.
    }
    return stored;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> ParseReferrer(
    const std::string& installReferrer) {
  try {
    if (installReferrer.empty()) return std::nullopt;
    std::string linkValue;
    std::string deepLinkValue;
    bool haveLink = false;
    bool haveDeepLink = false;
    size_t start = 0;
    while (start <= installReferrer.size()) {
      size_t end = installReferrer.find('&', start);
      if (end == std::string::npos) end = installReferrer.size();
      std::string pair = installReferrer.substr(start, end - start);
      size_t eq = pair.find('=');
      std::string name = (eq == std::string::npos)
                             ? UrlDecodeNoThrow(pair)
                             : UrlDecodeNoThrow(pair.substr(0, eq));
      std::string value = (eq == std::string::npos)
                              ? ""
                              : UrlDecodeNoThrow(pair.substr(eq + 1));
      if (name == "link" && !haveLink) {
        linkValue = value;
        haveLink = true;
      } else if (name == "deep_link" && !haveDeepLink) {
        deepLinkValue = value;
        haveDeepLink = true;
      }
      if (end == installReferrer.size()) break;
      start = end + 1;
    }
    // First present link param wins, per the SVC-GMS contract.
    if (haveLink && !linkValue.empty()) return linkValue;
    if (haveDeepLink && !deepLinkValue.empty()) return deepLinkValue;
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}
