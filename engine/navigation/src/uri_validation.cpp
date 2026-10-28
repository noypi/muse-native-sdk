#include "navigation/uri_validation.h"

#include <cctype>
#include <regex>

namespace {

ValidateResult reject(RejectReason reason) {
  ValidateResult r;
  r.accepted = false;
  r.navigate = false;
  r.reason = reason;
  return r;
}

std::string toLower(std::string s) {
  for (auto& c : s) {
    c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool isHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

// Validates %-escapes in `s`; on success appends the decoded form to `out`.
bool percentDecode(const std::string& s, std::string* out) {
  out->clear();
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%') {
      if (i + 2 >= s.size() || !isHex(s[i + 1]) || !isHex(s[i + 2])) {
        return false;
      }
      char decoded = static_cast<char>((hexVal(s[i + 1]) << 4) |
                                       hexVal(s[i + 2]));
      out->push_back(decoded);
      i += 2;
    } else {
      out->push_back(s[i]);
    }
  }
  return true;
}

bool hasDotDotSegment(const std::string& path) {
  size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find('/', i);
    std::string seg =
        (j == std::string::npos) ? path.substr(i) : path.substr(i, j - i);
    if (seg == "..") return true;
    if (j == std::string::npos) break;
    i = j + 1;
  }
  return false;
}

bool parseStrictInt(const std::string& s, long long* out) {
  if (s.empty()) return false;
  size_t i = 0;
  if (s[0] == '+' || s[0] == '-') i = 1;
  if (i >= s.size()) return false;
  for (size_t k = i; k < s.size(); ++k) {
    if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
  }
  try {
    size_t idx = 0;
    long long v = std::stoll(s, &idx);
    if (idx != s.size()) return false;
    *out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool parseStrictDouble(const std::string& s, double* out) {
  if (s.empty()) return false;
  try {
    size_t idx = 0;
    double v = std::stod(s, &idx);
    if (idx != s.size()) return false;
    *out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool parseStrictBool(const std::string& s, bool* out) {
  std::string l = toLower(s);
  if (l == "true" || l == "1") {
    *out = true;
    return true;
  }
  if (l == "false" || l == "0") {
    *out = false;
    return true;
  }
  return false;
}

}  // namespace

ValidateResult ValidateUri(const std::string& uri,
                           const RouteManifest& manifest) {
  try {
    if (uri.size() > static_cast<size_t>(kMaxUriBytes)) {
      return reject(RejectReason::OVERSIZE);
    }
    // Backslashes are never valid in an App Link URI: treat as traversal.
    if (uri.find('\\') != std::string::npos) {
      return reject(RejectReason::TRAVERSAL);
    }

    size_t schemeEnd = uri.find("://");
    if (schemeEnd == std::string::npos) {
      return reject(RejectReason::ENCODING);
    }
    std::string scheme = toLower(uri.substr(0, schemeEnd));
    bool schemeOk = false;
    for (const auto& s : manifest.schemes) {
      if (toLower(s) == scheme) {
        schemeOk = true;
        break;
      }
    }
    if (!schemeOk) {
      return reject(RejectReason::UNKNOWN_HOST);
    }

    std::string rest = uri.substr(schemeEnd + 3);
    size_t authEnd = rest.find_first_of("/?#");
    std::string authority =
        (authEnd == std::string::npos) ? rest : rest.substr(0, authEnd);
    std::string after =
        (authEnd == std::string::npos) ? "" : rest.substr(authEnd);

    // Embedded userinfo (user[:pass]@host) is rejected outright.
    if (authority.find('@') != std::string::npos) {
      return reject(RejectReason::CREDENTIALS);
    }

    std::string host = authority;
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
      host = authority.substr(0, colon);
    }
    std::string hostLower = toLower(host);
    bool hostOk = false;
    for (const auto& h : manifest.hosts) {
      if (toLower(h) == hostLower) {
        hostOk = true;
        break;
      }
    }
    if (!hostOk) {
      return reject(RejectReason::UNKNOWN_HOST);
    }

    std::string path;
    std::string query;
    std::string fragmentStripped = after;
    size_t hashPos = fragmentStripped.find('#');
    if (hashPos != std::string::npos) {
      fragmentStripped = fragmentStripped.substr(0, hashPos);
    }
    size_t qPos = fragmentStripped.find('?');
    if (qPos == std::string::npos) {
      path = fragmentStripped;
    } else {
      path = fragmentStripped.substr(0, qPos);
      query = fragmentStripped.substr(qPos + 1);
    }
    if (path.empty()) path = "/";

    // Raw dotdot segments traverse out of the route scope.
    if (hasDotDotSegment(path)) {
      return reject(RejectReason::TRAVERSAL);
    }

    std::string decodedPath;
    if (!percentDecode(path, &decodedPath)) {
      return reject(RejectReason::ENCODING);
    }
    if (decodedPath.find('\\') != std::string::npos ||
        hasDotDotSegment(decodedPath)) {
      return reject(RejectReason::TRAVERSAL);
    }

    // Route match (host + regex path pattern); unknown shapes never reach
    // the trie.
    const ManifestRoute* matched = nullptr;
    for (const auto& route : manifest.routes) {
      if (toLower(route.host) != hostLower) continue;
      try {
        std::regex re(route.pathPattern, std::regex::ECMAScript);
        if (std::regex_match(decodedPath, re)) {
          matched = &route;
          break;
        }
      } catch (const std::regex_error&) {
        continue;
      }
    }
    if (matched == nullptr) {
      return reject(RejectReason::PATH_MISMATCH);
    }

    // Query params: split, limit, then typed check.
    std::map<std::string, std::string> rawParams;
    std::vector<std::pair<std::string, std::string>> ordered;
    if (!query.empty()) {
      size_t i = 0;
      while (i <= query.size()) {
        size_t j = query.find('&', i);
        std::string pair =
            (j == std::string::npos) ? query.substr(i)
                                     : query.substr(i, j - i);
        if (!pair.empty()) {
          size_t eq = pair.find('=');
          std::string name =
              (eq == std::string::npos) ? pair : pair.substr(0, eq);
          std::string value =
              (eq == std::string::npos) ? "" : pair.substr(eq + 1);
          std::string decodedName;
          std::string decodedValue;
          if (!percentDecode(name, &decodedName) ||
              !percentDecode(value, &decodedValue)) {
            return reject(RejectReason::ENCODING);
          }
          if (decodedValue.size() > static_cast<size_t>(kMaxParamBytes) ||
              pair.size() > static_cast<size_t>(kMaxParamBytes)) {
            return reject(RejectReason::OVERSIZE);
          }
          // Malformed name/value escapes already rejected above.
          ordered.emplace_back(decodedName, decodedValue);
          rawParams[decodedName] = decodedValue;
        }
        if (j == std::string::npos) break;
        i = j + 1;
      }
    }
    if (ordered.size() > static_cast<size_t>(kMaxParams)) {
      return reject(RejectReason::OVERSIZE);
    }

    ValidateResult ok;
    ok.accepted = true;
    ok.navigate = false;
    ok.routeId = matched->routeId;
    for (const auto& qp : matched->query) {
      auto it = rawParams.find(qp.name);
      if (it == rawParams.end()) {
        if (qp.required) {
          return reject(RejectReason::BAD_PARAM);
        }
        continue;
      }
      const std::string& raw = it->second;
      TypedParam tp;
      tp.type = qp.type;
      switch (qp.type) {
        case ParamType::Int: {
          long long v = 0;
          if (!parseStrictInt(raw, &v)) {
            return reject(RejectReason::BAD_PARAM);
          }
          tp.intValue = v;
          break;
        }
        case ParamType::Double: {
          double v = 0.0;
          if (!parseStrictDouble(raw, &v)) {
            return reject(RejectReason::BAD_PARAM);
          }
          tp.doubleValue = v;
          break;
        }
        case ParamType::Bool: {
          bool v = false;
          if (!parseStrictBool(raw, &v)) {
            return reject(RejectReason::BAD_PARAM);
          }
          tp.boolValue = v;
          break;
        }
        case ParamType::String:
          tp.stringValue = raw;
          break;
      }
      ok.params[qp.name] = tp;
    }
    return ok;
  } catch (...) {
    return reject(RejectReason::ENCODING);
  }
}
