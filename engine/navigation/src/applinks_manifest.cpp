#include "navigation/applinks_manifest.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace {

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool boolean = false;
  std::string str;
  std::vector<JsonValue> array;
  std::map<std::string, JsonValue> object;
};

struct Parser {
  const std::string& s;
  size_t pos = 0;
  bool failed = false;

  explicit Parser(const std::string& text) : s(text) {}

  void skipWs() {
    while (pos < s.size() &&
           (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' ||
            s[pos] == '\r')) {
      ++pos;
    }
  }

  bool expect(char c) {
    skipWs();
    if (pos < s.size() && s[pos] == c) {
      ++pos;
      return true;
    }
    failed = true;
    return false;
  }

  JsonValue parseValue() {
    skipWs();
    JsonValue v;
    if (pos >= s.size()) {
      failed = true;
      return v;
    }
    char c = s[pos];
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') {
      v.type = JsonValue::Type::String;
      v.str = parseString();
      return v;
    }
    if (c == 't' || c == 'f') return parseBool();
    if (c == 'n') {
      if (s.compare(pos, 4, "null") == 0) {
        pos += 4;
        return v;
      }
      failed = true;
      return v;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
    failed = true;
    return v;
  }

  std::string parseString() {
    // Assumes s[pos] == '"'.
    ++pos;
    std::string out;
    while (pos < s.size()) {
      char c = s[pos++];
      if (c == '"') return out;
      if (c == '\\') {
        if (pos >= s.size()) break;
        char e = s[pos++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'u': {
            // Minimal \uXXXX handling: ASCII subset only.
            if (pos + 4 > s.size()) {
              failed = true;
              return out;
            }
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              char h = s[pos++];
              code <<= 4;
              if (h >= '0' && h <= '9') code += static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f')
                code += static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                code += static_cast<unsigned>(h - 'A' + 10);
              else
                failed = true;
            }
            out += static_cast<char>(code & 0x7F);
            break;
          }
          default: out += e; break;
        }
      } else {
        out += c;
      }
    }
    failed = true;
    return out;
  }

  JsonValue parseObject() {
    JsonValue v;
    v.type = JsonValue::Type::Object;
    ++pos;  // '{'
    skipWs();
    if (pos < s.size() && s[pos] == '}') {
      ++pos;
      return v;
    }
    while (true) {
      skipWs();
      if (pos >= s.size() || s[pos] != '"') {
        failed = true;
        return v;
      }
      std::string key = parseString();
      if (failed) return v;
      if (!expect(':')) return v;
      JsonValue val = parseValue();
      if (failed) return v;
      v.object[key] = val;
      skipWs();
      if (pos >= s.size()) {
        failed = true;
        return v;
      }
      if (s[pos] == ',') {
        ++pos;
        continue;
      }
      if (s[pos] == '}') {
        ++pos;
        return v;
      }
      failed = true;
      return v;
    }
  }

  JsonValue parseArray() {
    JsonValue v;
    v.type = JsonValue::Type::Array;
    ++pos;  // '['
    skipWs();
    if (pos < s.size() && s[pos] == ']') {
      ++pos;
      return v;
    }
    while (true) {
      JsonValue val = parseValue();
      if (failed) return v;
      v.array.push_back(val);
      skipWs();
      if (pos >= s.size()) {
        failed = true;
        return v;
      }
      if (s[pos] == ',') {
        ++pos;
        continue;
      }
      if (s[pos] == ']') {
        ++pos;
        return v;
      }
      failed = true;
      return v;
    }
  }

  JsonValue parseBool() {
    JsonValue v;
    v.type = JsonValue::Type::Bool;
    if (s.compare(pos, 4, "true") == 0) {
      v.boolean = true;
      pos += 4;
    } else if (s.compare(pos, 5, "false") == 0) {
      v.boolean = false;
      pos += 5;
    } else {
      failed = true;
    }
    return v;
  }

  JsonValue parseNumber() {
    JsonValue v;
    v.type = JsonValue::Type::Number;
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-') ++pos;
    while (pos < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.' ||
            s[pos] == 'e' || s[pos] == 'E' || s[pos] == '+' ||
            s[pos] == '-')) {
      ++pos;
    }
    v.str = s.substr(start, pos - start);
    return v;
  }
};

const JsonValue* find(const JsonValue& obj, const std::string& key) {
  if (obj.type != JsonValue::Type::Object) return nullptr;
  auto it = obj.object.find(key);
  if (it == obj.object.end()) return nullptr;
  return &it->second;
}

bool paramTypeFromString(const std::string& t, ParamType* out) {
  if (t == "int") *out = ParamType::Int;
  else if (t == "double") *out = ParamType::Double;
  else if (t == "bool") *out = ParamType::Bool;
  else if (t == "string") *out = ParamType::String;
  else
    return false;
  return true;
}

}  // namespace

LoadManifestResult LoadManifest(const std::string& path) {
  LoadManifestResult result;
  try {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "open failed: " + path;
      return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    // Keep a copy of the text alive for the parser reference.
    std::string text = ss.str();
    Parser p(text);
    JsonValue root = p.parseValue();
    p.skipWs();
    if (p.failed || p.pos != text.size() ||
        root.type != JsonValue::Type::Object) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "manifest: invalid JSON";
      return result;
    }

    RouteManifest m;
    const JsonValue* hosts = find(root, "hosts");
    const JsonValue* schemes = find(root, "schemes");
    const JsonValue* routes = find(root, "routes");
    const JsonValue* packageName = find(root, "packageName");
    const JsonValue* fingerprints = find(root, "sha256CertFingerprints");
    const JsonValue* autoVerify = find(root, "autoVerify");

    if (!hosts || hosts->type != JsonValue::Type::Array ||
        hosts->array.empty()) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "hosts: must be a non-empty array of strings";
      return result;
    }
    for (size_t i = 0; i < hosts->array.size(); ++i) {
      const JsonValue& h = hosts->array[i];
      if (h.type != JsonValue::Type::String || h.str.empty()) {
        result.ok = false;
        result.error.reason = ManifestErrorReason::BAD_PATTERN;
        result.error.detail = "hosts[" + std::to_string(i) +
                              "]: must be a non-empty string";
        return result;
      }
      m.hosts.push_back(h.str);
    }
    if (!schemes || schemes->type != JsonValue::Type::Array ||
        schemes->array.empty()) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "schemes: must be a non-empty array of strings";
      return result;
    }
    for (size_t i = 0; i < schemes->array.size(); ++i) {
      const JsonValue& s = schemes->array[i];
      if (s.type != JsonValue::Type::String || s.str.empty()) {
        result.ok = false;
        result.error.reason = ManifestErrorReason::BAD_PATTERN;
        result.error.detail = "schemes[" + std::to_string(i) +
                              "]: must be a non-empty string";
        return result;
      }
      m.schemes.push_back(s.str);
    }
    if (!packageName ||
        packageName->type != JsonValue::Type::String ||
        packageName->str.empty()) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "packageName: must be a non-empty string";
      return result;
    }
    m.packageName = packageName->str;
    if (!routes || routes->type != JsonValue::Type::Array ||
        routes->array.empty()) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::BAD_PATTERN;
      result.error.detail = "routes: must be a non-empty array";
      return result;
    }
    if (fingerprints && fingerprints->type == JsonValue::Type::Array) {
      for (size_t i = 0; i < fingerprints->array.size(); ++i) {
        const JsonValue& f = fingerprints->array[i];
        if (f.type != JsonValue::Type::String || f.str.empty()) {
          result.ok = false;
          result.error.reason = ManifestErrorReason::BAD_PATTERN;
          result.error.detail = "sha256CertFingerprints[" +
                                std::to_string(i) +
                                "]: must be a non-empty string";
          return result;
        }
        m.sha256CertFingerprints.push_back(f.str);
      }
    }
    m.autoVerify = true;
    if (autoVerify && autoVerify->type == JsonValue::Type::Bool) {
      m.autoVerify = autoVerify->boolean;
    }

    if (m.sha256CertFingerprints.empty()) {
      result.ok = false;
      result.error.reason = ManifestErrorReason::MISSING_FINGERPRINT;
      result.error.detail = "sha256CertFingerprints: must be non-empty";
      return result;
    }

    if (routes && routes->type == JsonValue::Type::Array) {
      for (size_t i = 0; i < routes->array.size(); ++i) {
        const JsonValue& r = routes->array[i];
        const JsonValue* host = find(r, "host");
        const JsonValue* pattern = find(r, "pathPattern");
        const JsonValue* routeId = find(r, "routeId");
        const JsonValue* query = find(r, "query");
        std::string hostStr =
            (host && host->type == JsonValue::Type::String) ? host->str : "";
        std::string patternStr =
            (pattern && pattern->type == JsonValue::Type::String) ? pattern->str
                                                                 : "";
        bool known = false;
        for (const auto& h : m.hosts) {
          if (h == hostStr) {
            known = true;
            break;
          }
        }
        if (!known) {
          result.ok = false;
          result.error.reason = ManifestErrorReason::UNKNOWN_HOST;
          result.error.detail =
              "routes[" + std::to_string(i) + "].host: unknown host '" +
              hostStr + "'";
          return result;
        }
        if (patternStr.empty() || patternStr[0] != '/') {
          result.ok = false;
          result.error.reason = ManifestErrorReason::BAD_PATTERN;
          result.error.detail =
              "routes[" + std::to_string(i) + "].pathPattern: must start with '/'";
          return result;
        }
        try {
          std::regex re(patternStr, std::regex::ECMAScript);
          (void)re;
        } catch (const std::regex_error&) {
          result.ok = false;
          result.error.reason = ManifestErrorReason::BAD_PATTERN;
          result.error.detail = "routes[" + std::to_string(i) +
                                "].pathPattern: invalid regex '" + patternStr +
                                "'";
          return result;
        }
        ManifestRoute mr;
        mr.host = hostStr;
        mr.pathPattern = patternStr;
        mr.routeId = (routeId && routeId->type == JsonValue::Type::String)
                         ? routeId->str
                         : "";
        if (query && query->type == JsonValue::Type::Array) {
          for (size_t j = 0; j < query->array.size(); ++j) {
            const JsonValue& q = query->array[j];
            const JsonValue* name = find(q, "name");
            const JsonValue* type = find(q, "type");
            const JsonValue* required = find(q, "required");
            QueryParam qp;
            qp.name = (name && name->type == JsonValue::Type::String)
                          ? name->str
                          : "";
            std::string typeStr =
                (type && type->type == JsonValue::Type::String) ? type->str
                                                               : "string";
            if (!paramTypeFromString(typeStr, &qp.type)) {
              result.ok = false;
              result.error.reason = ManifestErrorReason::BAD_PATTERN;
              result.error.detail = "routes[" + std::to_string(i) +
                                    "].query[" + std::to_string(j) +
                                    "].type: unknown type '" + typeStr + "'";
              return result;
            }
            qp.required = (required &&
                           required->type == JsonValue::Type::Bool &&
                           required->boolean);
            mr.query.push_back(qp);
          }
        }
        m.routes.push_back(mr);
      }
    }

    result.ok = true;
    result.manifest = m;
    return result;
  } catch (...) {
    result.ok = false;
    result.error.reason = ManifestErrorReason::BAD_PATTERN;
    if (result.error.detail.empty()) {
      result.error.detail = "manifest: parse failure";
    }
    return result;
  }
}
