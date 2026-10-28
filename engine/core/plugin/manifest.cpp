// Plugin capability manifest implementation (NFR-CORE-006, AC-CORE-009).
//
// parseManifest validates the load-time declaration; enforce applies
// deny-by-default checks before plugin code runs. Every denial carries
// a named reason (ManifestError message / PluginRequest::reason)
// surfaced to the sandbox peer.

#include "manifest.h"

namespace muse {
namespace core {
namespace plugin {

ParseResult ParseResult::ok(Manifest manifest) {
  ParseResult result;
  result.success_ = true;
  result.manifest_ = std::move(manifest);
  return result;
}

ParseResult ParseResult::err(ManifestError error) {
  ParseResult result;
  result.success_ = false;
  result.error_ = std::move(error);
  return result;
}

namespace {

// Minimal flat-JSON helpers: the manifest is a flat object with known
// keys only, so a small scanner suffices (no third-party dependency).

bool IsKnownKey(const std::string& key) {
  return key == "allowNetwork" || key == "fsRoots" ||
         key == "jniClasses" || key == "threadAffinity";
}

bool IsKnownAffinity(const std::string& affinity) {
  // Empty or "any" means no pinning; otherwise the manifest must name a
  // known pool. Anything else is THREAD_AFFINITY_INVALID.
  return affinity.empty() || affinity == "any" || affinity == "main" ||
         affinity == "render" || affinity == "worker";
}

bool IsKnownCapabilityName(const std::string& capability) {
  return capability.empty() || capability == "network" ||
         capability == "fs" || capability == "jni" ||
         capability == "affinity";
}

// True when path is at or under one of the declared allowlist roots.
// Deny-by-default: an empty allowlist denies every fs use.
bool UnderRoots(const std::string& path,
                const std::vector<std::string>& fsRoots) {
  if (path.empty()) return true;
  for (const std::string& root : fsRoots) {
    if (root.empty()) continue;
    if (path == root) return true;
    if (path.size() > root.size() &&
        path.compare(0, root.size(), root) == 0 &&
        (root.back() == '/' || path[root.size()] == '/')) {
      return true;
    }
  }
  return false;
}

// Record a denial with its named reason (deny helper). Returns false so
// call sites read as `return deny(...)`.
bool deny(PluginRequest& request, ManifestError* error,
          ManifestErrorCode code, const std::string& reason) {
  request.reason = reason;
  if (error != nullptr) {
    error->code = code;
    error->message = reason;
  }
  return false;
}

}  // namespace

ParseResult parseManifest(const char* json) {
  if (json == nullptr) {
    return ParseResult::err(ManifestError{
        ManifestErrorCode::UNKNOWN_CAPABILITY,
        "manifest: null JSON document"});
  }
  const std::string text(json);

  // Reject unknown capabilities: any "key" not in the known set fails
  // closed with UNKNOWN_CAPABILITY before any other check.
  std::string::size_type pos = 0;
  while ((pos = text.find('"', pos)) != std::string::npos) {
    const std::string::size_type end = text.find('"', pos + 1);
    if (end == std::string::npos) break;
    const std::string key = text.substr(pos + 1, end - pos - 1);
    const std::string::size_type colon = text.find(':', end + 1);
    const std::string::size_type quote = text.find('"', end + 1);
    // Only treat it as a key when followed by ':' (skip string values).
    if (colon != std::string::npos &&
        (quote == std::string::npos || colon < quote)) {
      if (!IsKnownKey(key)) {
        return ParseResult::err(
            ManifestError{ManifestErrorCode::UNKNOWN_CAPABILITY,
                          "manifest: unknown capability '" + key + "'"});
      }
      pos = colon + 1;
    } else {
      pos = end + 1;
    }
  }

  Manifest manifest{};
  manifest.allowNetwork =
      text.find("\"allowNetwork\"") != std::string::npos &&
      text.find("true", text.find("\"allowNetwork\"")) != std::string::npos;

  // Collect quoted values under fsRoots / jniClasses sections.
  auto collectArray = [&](const char* section,
                          std::vector<std::string>& out) {
    const std::string name(section);
    const std::string::size_type begin = text.find(name);
    if (begin == std::string::npos) return;
    const std::string::size_type open = text.find('[', begin);
    const std::string::size_type close = text.find(']', open);
    if (open == std::string::npos || close == std::string::npos) return;
    std::string::size_type p = open;
    while ((p = text.find('"', p)) != std::string::npos && p < close) {
      const std::string::size_type e = text.find('"', p + 1);
      if (e == std::string::npos || e > close) break;
      out.push_back(text.substr(p + 1, e - p - 1));
      p = e + 1;
    }
  };
  collectArray("\"fsRoots\"", manifest.fsRoots);
  collectArray("\"jniClasses\"", manifest.jniClasses);

  // threadAffinity: first quoted value after its key.
  const std::string::size_type affinityKey = text.find("\"threadAffinity\"");
  if (affinityKey != std::string::npos) {
    const std::string::size_type colon = text.find(':', affinityKey);
    const std::string::size_type open = text.find('"', colon);
    const std::string::size_type close = text.find('"', open + 1);
    if (open != std::string::npos && close != std::string::npos) {
      manifest.threadAffinity = text.substr(open + 1, close - open - 1);
    }
  }

  // Declared roots must be absolute; a relative root is outside the
  // allowlist model and fails closed.
  for (const std::string& root : manifest.fsRoots) {
    if (root.empty() || root[0] != '/') {
      return ParseResult::err(ManifestError{
          ManifestErrorCode::FS_ROOT_OUTSIDE_ALLOWLIST,
          "manifest: fs root outside allowlist '" + root + "'"});
    }
  }

  // Declared JNI classes must be fully qualified; anything else is a
  // denied class at parse time.
  for (const std::string& clazz : manifest.jniClasses) {
    if (clazz.find('/') == std::string::npos &&
        clazz.find('.') == std::string::npos) {
      return ParseResult::err(
          ManifestError{ManifestErrorCode::JNI_CLASS_DENIED,
                        "manifest: JNI class denied '" + clazz + "'"});
    }
  }

  if (!IsKnownAffinity(manifest.threadAffinity)) {
    return ParseResult::err(ManifestError{
        ManifestErrorCode::THREAD_AFFINITY_INVALID,
        "manifest: invalid thread affinity '" + manifest.threadAffinity +
            "'"});
  }

  return ParseResult::ok(std::move(manifest));
}

bool enforce(const Manifest& manifest, const PluginRequest& request) {
  PluginRequest copy = request;
  return enforce(manifest, copy, nullptr);
}

bool enforce(const Manifest& manifest, PluginRequest& request,
             ManifestError* error) {
  // Deny-by-default: every undeclared capability use is denied with a
  // named reason surfaced to the sandbox peer.
  if (!IsKnownCapabilityName(request.capability)) {
    return deny(request, error, ManifestErrorCode::UNKNOWN_CAPABILITY,
                "deny: unknown capability '" + request.capability + "'");
  }
  // Undeclared network use is denied.
  if (request.needsNetwork && !manifest.allowNetwork) {
    return deny(request, error, ManifestErrorCode::UNKNOWN_CAPABILITY,
                "deny: network use not declared in manifest");
  }
  // Filesystem use outside the declared allowlist roots is denied.
  if (!UnderRoots(request.path, manifest.fsRoots)) {
    return deny(request, error,
                ManifestErrorCode::FS_ROOT_OUTSIDE_ALLOWLIST,
                "deny: fs path outside allowlist '" + request.path + "'");
  }
  // JNI use of a class not in the declared list is denied.
  if (!request.jniClass.empty()) {
    bool allowed = false;
    for (const std::string& clazz : manifest.jniClasses) {
      if (clazz == request.jniClass) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      return deny(request, error, ManifestErrorCode::JNI_CLASS_DENIED,
                  "deny: JNI class denied '" + request.jniClass + "'");
    }
  }
  // Affinity requests must match the declared affinity (or "any").
  if (!request.threadAffinity.empty() &&
      manifest.threadAffinity != "any" &&
      request.threadAffinity != manifest.threadAffinity) {
    return deny(request, error,
                ManifestErrorCode::THREAD_AFFINITY_INVALID,
                "deny: thread affinity '" + request.threadAffinity +
                    "' not declared (manifest: '" +
                    manifest.threadAffinity + "')");
  }
  request.reason.clear();
  if (error != nullptr) error->message.clear();
  return true;
}

}  // namespace plugin
}  // namespace core
}  // namespace muse
