#include "net/codec.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace muse {
namespace net {
namespace codec_detail {

DecodeResult Fail(std::string error, std::string field) {
  DecodeResult r;
  r.ok = false;
  r.error = std::move(error);
  r.field = std::move(field);
  return r;
}

namespace {

// --- Minimal strict-JSON canonicalizer (dep-free IJsonBackend core) ---

struct Value {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Value::Kind::Null;
  bool boolean = false;
  double number = 0.0;
  std::string str;  // String contents (UTF-8) or raw canonical number text
  std::vector<Value> items;
  std::map<std::string, Value> members;  // sorted → canonical key order
  bool number_is_int = false;
  long long int_val = 0;
};

struct Parser {
  const char* p = nullptr;
  const char* end = nullptr;
  std::string error;
  std::string field;

  bool AtEnd() const { return p >= end; }

  void SkipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
      ++p;
  }

  bool Expect(char c, const std::string& path, const char* code) {
    if (AtEnd()) {
      error = "truncated";
      field = path;
      return false;
    }
    if (*p != c) {
      error = code;
      field = path;
      return false;
    }
    ++p;
    return true;
  }

  bool Literal(const char* word, const std::string& path) {
    for (const char* q = word; *q; ++q) {
      if (AtEnd() || *p != *q) {
        error = "malformed-literal";
        field = path;
        return false;
      }
      ++p;
    }
    return true;
  }

  static void AppendUtf8(std::string* out, unsigned cp) {
    if (cp < 0x80) {
      out->push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool Hex4(unsigned* cp, const std::string& path) {
    if (end - p < 4) {
      error = "truncated";
      field = path;
      return false;
    }
    unsigned v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = p[i];
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f')
        v |= static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        v |= static_cast<unsigned>(c - 'A' + 10);
      else {
        error = "bad-escape";
        field = path;
        return false;
      }
    }
    p += 4;
    *cp = v;
    return true;
  }

  bool ParseString(std::string* out, const std::string& path) {
    if (!Expect('"', path, "expected-string")) return false;
    while (true) {
      if (AtEnd()) {
        error = "truncated";
        field = path;
        return false;
      }
      char c = *p++;
      if (c == '"') return true;
      if (c == '\\') {
        if (AtEnd()) {
          error = "truncated";
          field = path;
          return false;
        }
        char e = *p++;
        switch (e) {
          case '"': out->push_back('"'); break;
          case '\\': out->push_back('\\'); break;
          case '/': out->push_back('/'); break;
          case 'b': out->push_back('\b'); break;
          case 'f': out->push_back('\f'); break;
          case 'n': out->push_back('\n'); break;
          case 'r': out->push_back('\r'); break;
          case 't': out->push_back('\t'); break;
          case 'u': {
            unsigned cp = 0;
            if (!Hex4(&cp, path)) return false;
            if (cp >= 0xD800 && cp <= 0xDBFF) {
              // Surrogate pair.
              if (end - p < 6 || p[0] != '\\' || p[1] != 'u') {
                error = "bad-escape";
                field = path;
                return false;
              }
              p += 2;
              unsigned lo = 0;
              if (!Hex4(&lo, path)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) {
                error = "bad-escape";
                field = path;
                return false;
              }
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
              error = "bad-escape";
              field = path;
              return false;
            }
            AppendUtf8(out, cp);
            break;
          }
          default:
            error = "bad-escape";
            field = path;
            return false;
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        error = "unescaped-control";
        field = path;
        return false;
      } else {
        out->push_back(c);
      }
      // String size cap: a single hostile runaway string cannot exhaust
      // memory before the hard cap trips.
      if (out->size() > kMaxJsonBytes) {
        error = "size-cap-exceeded";
        field = path;
        return false;
      }
    }
  }

  bool ParseNumber(Value* v, const std::string& path) {
    const char* start = p;
    if (!AtEnd() && *p == '-') ++p;
    if (AtEnd()) {
      error = "truncated";
      field = path;
      return false;
    }
    if (*p == '0') {
      ++p;
    } else if (*p >= '1' && *p <= '9') {
      while (!AtEnd() && *p >= '0' && *p <= '9') ++p;
    } else {
      error = "bad-number";
      field = path;
      return false;
    }
    if (!AtEnd() && *p == '.') {
      ++p;
      if (AtEnd() || *p < '0' || *p > '9') {
        error = AtEnd() ? "truncated" : "bad-number";
        field = path;
        return false;
      }
      while (!AtEnd() && *p >= '0' && *p <= '9') ++p;
    }
    if (!AtEnd() && (*p == 'e' || *p == 'E')) {
      ++p;
      if (!AtEnd() && (*p == '+' || *p == '-')) ++p;
      if (AtEnd() || *p < '0' || *p > '9') {
        error = AtEnd() ? "truncated" : "bad-number";
        field = path;
        return false;
      }
      while (!AtEnd() && *p >= '0' && *p <= '9') ++p;
    }
    std::string text(start, p);
    // Range check: strtod reports HUGE_VAL (with ERANGE) for magnitudes
    // beyond double, and we reject non-finite results outright so
    // 1e99999-style inputs fail closed instead of becoming inf.
    char* stop = nullptr;
    double d = std::strtod(text.c_str(), &stop);
    if (stop == nullptr || *stop != '\0' || !std::isfinite(d)) {
      error = "number-out-of-range";
      field = path;
      return false;
    }
    v->kind = Value::Kind::Number;
    v->number = d;
    // Preserve integer literals exactly (stable across re-encode) when
    // they fit int64; fractional forms re-emit via %.17g.
    char* iend = nullptr;
    long long iv = std::strtoll(text.c_str(), &iend, 10);
    if (iend != nullptr && *iend == '\0' && std::string(text) == std::to_string(iv)) {
      v->number_is_int = true;
      v->int_val = iv;
    }
    return true;
  }

  bool ParseValue(Value* v, const std::string& path, size_t depth) {
    if (depth > kMaxDepth) {
      error = "depth-exceeded";
      field = path;
      return false;
    }
    SkipWs();
    if (AtEnd()) {
      error = "truncated";
      field = path;
      return false;
    }
    char c = *p;
    if (c == '{') {
      ++p;
      v->kind = Value::Kind::Object;
      SkipWs();
      if (!AtEnd() && *p == '}') {
        ++p;
        return true;
      }
      while (true) {
        SkipWs();
        std::string key;
        std::string kpath = path.empty() ? "$" : path;
        if (!ParseString(&key, kpath)) return false;
        SkipWs();
        if (!Expect(':', kpath, "expected-colon")) return false;
        Value child;
        std::string cpath = kpath + "." + key;
        if (!ParseValue(&child, cpath, depth + 1)) return false;
        v->members[key] = std::move(child);
        SkipWs();
        if (AtEnd()) {
          error = "truncated";
          field = path;
          return false;
        }
        if (*p == ',') {
          ++p;
          SkipWs();
          // Strict JSON: no trailing comma before } or ].
          if (!AtEnd() && *p == '}') {
            error = "trailing-comma";
            field = path;
            return false;
          }
          continue;
        }
        if (*p == '}') {
          ++p;
          return true;
        }
        error = "expected-comma-or-close";
        field = path;
        return false;
      }
    }
    if (c == '[') {
      ++p;
      v->kind = Value::Kind::Array;
      SkipWs();
      if (!AtEnd() && *p == ']') {
        ++p;
        return true;
      }
      size_t idx = 0;
      while (true) {
        Value child;
        std::string cpath = path + "[" + std::to_string(idx) + "]";
        if (!ParseValue(&child, cpath, depth + 1)) return false;
        v->items.push_back(std::move(child));
        ++idx;
        SkipWs();
        if (AtEnd()) {
          error = "truncated";
          field = path;
          return false;
        }
        if (*p == ',') {
          ++p;
          SkipWs();
          if (!AtEnd() && *p == ']') {
            error = "trailing-comma";
            field = path;
            return false;
          }
          continue;
        }
        if (*p == ']') {
          ++p;
          return true;
        }
        error = "expected-comma-or-close";
        field = path;
        return false;
      }
    }
    if (c == '"') {
      std::string s;
      if (!ParseString(&s, path)) return false;
      v->kind = Value::Kind::String;
      v->str = std::move(s);
      return true;
    }
    if (c == 't') {
      if (!Literal("true", path)) return false;
      v->kind = Value::Kind::Bool;
      v->boolean = true;
      return true;
    }
    if (c == 'f') {
      if (!Literal("false", path)) return false;
      v->kind = Value::Kind::Bool;
      v->boolean = false;
      return true;
    }
    if (c == 'n') {
      if (!Literal("null", path)) return false;
      v->kind = Value::Kind::Null;
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(v, path);
    error = "unexpected-token";
    field = path;
    return false;
  }

  static void EmitString(const std::string& s, std::string* out) {
    out->push_back('"');
    for (unsigned char c : s) {
      switch (c) {
        case '"': *out += "\\\""; break;
        case '\\': *out += "\\\\"; break;
        case '\b': *out += "\\b"; break;
        case '\f': *out += "\\f"; break;
        case '\n': *out += "\\n"; break;
        case '\r': *out += "\\r"; break;
        case '\t': *out += "\\t"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            *out += buf;
          } else {
            out->push_back(static_cast<char>(c));
          }
      }
    }
    out->push_back('"');
  }

  static void Emit(const Value& v, std::string* out) {
    switch (v.kind) {
      case Value::Kind::Null:
        *out += "null";
        break;
      case Value::Kind::Bool:
        *out += v.boolean ? "true" : "false";
        break;
      case Value::Kind::Number:
        if (v.number_is_int) {
          *out += std::to_string(v.int_val);
        } else {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "%.17g", v.number);
          *out += buf;
        }
        break;
      case Value::Kind::String:
        EmitString(v.str, out);
        break;
      case Value::Kind::Array: {
        out->push_back('[');
        for (size_t i = 0; i < v.items.size(); ++i) {
          if (i) out->push_back(',');
          Emit(v.items[i], out);
        }
        out->push_back(']');
        break;
      }
      case Value::Kind::Object: {
        out->push_back('{');
        bool first = true;
        for (const auto& kv : v.members) {
          if (!first) out->push_back(',');
          first = false;
          EmitString(kv.first, out);
          out->push_back(':');
          Emit(kv.second, out);
        }
        out->push_back('}');
        break;
      }
    }
  }
};

class DefaultBackend : public IJsonBackend {
 public:
  bool Canonicalize(std::string_view in, std::string* out,
                    std::string* error, std::string* field) override {
    // Hard size cap first: hostile runaway inputs (e.g. a megabyte of
    // '{') are rejected before any buffer is interpreted.
    if (in.size() > kMaxJsonBytes) {
      if (error) *error = "size-cap-exceeded";
      if (field) field->clear();
      return false;
    }
    Parser ps;
    ps.p = in.data();
    ps.end = in.data() + in.size();
    Value root;
    if (!ps.ParseValue(&root, "", 0)) {
      if (error) *error = ps.error.empty() ? "malformed" : ps.error;
      if (field) *field = ps.field;
      return false;
    }
    ps.SkipWs();
    if (!ps.AtEnd()) {
      if (error) *error = "trailing-bytes";
      if (field) field->clear();
      return false;
    }
    std::string canon;
    Parser::Emit(root, &canon);
    if (out) *out = std::move(canon);
    return true;
  }
};

const char* MagicFor(CodecId id) {
  switch (id) {
    case CodecId::kProtobuf:
      return "PB01";
    case CodecId::kFlatBuffers:
      return "FB01";
    case CodecId::kCbor:
      return "CB01";
    case CodecId::kMsgPack:
      return "MP01";
    case CodecId::kJson:
      return "";
  }
  return "";
}

}  // namespace

bool CanonicalizeJson(std::string_view in, std::string* out,
                      std::string* error, std::string* field) {
  try {
    DefaultBackend b;
    std::string o, e, f;
    bool ok = b.Canonicalize(in, &o, &e, &f);
    if (out) *out = std::move(o);
    if (error) *error = std::move(e);
    if (field) *field = std::move(f);
    return ok;
  } catch (...) {
    if (error) *error = "internal-error";
    if (field) field->clear();
    return false;
  }
}

std::string Frame(CodecId id, const std::string& canonical) {
  if (id == CodecId::kJson) return canonical;  // kJson has no magic prefix.
  const char* magic = MagicFor(id);
  std::string wire;
  wire.append(magic, 4);
  wire.append(canonical);
  return wire;
}

bool Unframe(CodecId id, std::string_view bytes, std::string_view* body,
             std::string* error) {
  if (id == CodecId::kJson) { if (body) *body = bytes; return true; }
  const char* magic = MagicFor(id);
  if (bytes.size() < 4 || bytes.substr(0, 4) != std::string_view(magic, 4)) {
    if (error) *error = "bad-magic";
    return false;
  }
  if (body) *body = bytes.substr(4);
  return true;
}

}  // namespace codec_detail

std::unique_ptr<IJsonBackend> DefaultJsonBackend() {
  return std::unique_ptr<IJsonBackend>(new codec_detail::DefaultBackend());
}

namespace {

// Framed-codec helper shared by the protobuf/CBOR/MessagePack/FlatBuffers
// TUs' logic: canonicalize on encode, check the 4-byte magic and
// re-canonicalize on decode. Everything is exception-safe; any failure
// (including bad_alloc under hostile input) becomes a structured error.
std::string FramedEncode(CodecId id, const std::string& json) {
  try {
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(json, &canon, &err, &field)) return "";
    return codec_detail::Frame(id, canon);
  } catch (...) {
    return "";
  }
}

DecodeResult FramedDecode(CodecId id, std::string_view bytes) {
  try {
    if (bytes.size() > codec_detail::kMaxJsonBytes)
      return codec_detail::Fail("size-cap-exceeded");
    std::string_view body;
    std::string uerr;
    if (!codec_detail::Unframe(id, bytes, &body, &uerr))
      return codec_detail::Fail(std::move(uerr));
    std::string canon, err, field;
    if (!codec_detail::CanonicalizeJson(body, &canon, &err, &field))
      return codec_detail::Fail(std::move(err), std::move(field));
    DecodeResult r;
    r.ok = true;
    r.payload = std::move(canon);
    return r;
  } catch (...) {
    return codec_detail::Fail("internal-error");
  }
}

}  // namespace

std::string FlatBuffersCodec::encode(const std::string& json) {
  return FramedEncode(CodecId::kFlatBuffers, json);
}

DecodeResult FlatBuffersCodec::decode(std::string_view bytes) {
  return FramedDecode(CodecId::kFlatBuffers, bytes);
}

struct CodecRegistry::Impl {
  // Index by underlying CodecId value (kJson..kMsgPack are 0..4).
  std::unique_ptr<Codec> slots[5];
};

CodecRegistry::CodecRegistry() : impl_(new Impl()) {
  try {
    impl_->slots[static_cast<int>(CodecId::kJson)] =
        std::unique_ptr<Codec>(new JsonCodec());
    impl_->slots[static_cast<int>(CodecId::kProtobuf)] =
        std::unique_ptr<Codec>(new ProtobufCodec());
    impl_->slots[static_cast<int>(CodecId::kFlatBuffers)] =
        std::unique_ptr<Codec>(new FlatBuffersCodec());
    impl_->slots[static_cast<int>(CodecId::kCbor)] =
        std::unique_ptr<Codec>(new CborCodec());
    impl_->slots[static_cast<int>(CodecId::kMsgPack)] =
        std::unique_ptr<Codec>(new MsgPackCodec());
  } catch (...) {
    // slots stay null for entries not yet installed; get() reports the
    // gap as a structured failure path via exception-free lookup below.
  }
}

CodecRegistry::~CodecRegistry() = default;

void CodecRegistry::install(CodecId id, std::unique_ptr<Codec> c) {
  if (!impl_ || !c) return;
  int i = static_cast<int>(id);
  if (i < 0 || i >= 5) return;
  impl_->slots[i] = std::move(c);
}

Codec& CodecRegistry::get(CodecId id) {
  // All five slots are installed by the constructor; a moved-out slot can
  // only arise from a null install(), which install() refuses. The throw
  // below is unreachable in normal use and exists only to satisfy the
  // reference return for out-of-range ids.
  int i = static_cast<int>(id);
  if (!impl_ || i < 0 || i >= 5 || !impl_->slots[i])
    throw std::out_of_range("codec not installed");
  return *impl_->slots[i];
}

DecodeResult Validate(CodecId id, std::string_view bytes, size_t maxBytes) {
  try {
    if (bytes.size() > maxBytes) return codec_detail::Fail("size-cap-exceeded");
    CodecRegistry reg;
    Codec& c = reg.get(id);
    return c.decode(bytes);
  } catch (const std::exception& e) {
    return codec_detail::Fail("internal-error");
  } catch (...) {
    return codec_detail::Fail("internal-error");
  }
}

}  // namespace net
}  // namespace muse
