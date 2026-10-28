#include "net/graphql.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "net/sse.h"

namespace muse {
namespace net {
namespace {

// 1 MiB input cap: an exactly-1-MiB query is rejected as oversized.
const std::size_t kMaxQueryBytes = static_cast<std::size_t>(1) << 20;

// --- sha256 (compact, dependency-free; APQ hash only) ---------------------

uint32_t RotR(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

std::string Sha256Hex(const std::string& input) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string msg = input;
  const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8u;
  msg.push_back(static_cast<char>(0x80));
  while (msg.size() % 64 != 56) msg.push_back(static_cast<char>(0));
  for (int i = 7; i >= 0; --i) {
    msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));
  }
  for (std::size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4]))
              << 24) |
             (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 1]))
              << 16) |
             (static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 2]))
              << 8) |
             static_cast<uint32_t>(static_cast<unsigned char>(msg[off + i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
      const uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  char hex[65];
  for (int i = 0; i < 8; ++i) std::snprintf(hex + i * 8, 9, "%08x", h[i]);
  return std::string(hex, 64);
}

// --- small JSON helpers ----------------------------------------------------

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  char buf[7];
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string RawOrEmptyObject(const std::string& vars) {
  return vars.empty() ? "{}" : vars;
}

// Builds one operation payload; with hash_only the full query text is
// omitted (APQ first attempt carries only the sha256 hash).
std::string OperationPayload(const std::string& query, const std::string& vars,
                             bool hash_only) {
  std::string payload = "{";
  if (!hash_only) {
    payload += "\"query\":\"" + JsonEscape(query) + "\",";
  }
  payload += "\"variables\":" + RawOrEmptyObject(vars) + ",";
  payload += "\"extensions\":{\"persistedQuery\":{\"version\":1,";
  payload += "\"sha256Hash\":\"" + Sha256Hex(query) + "\"}}}";
  return payload;
}

bool HasErrors(const std::string& body) {
  return body.find("\"errors\"") != std::string::npos;
}

bool HasData(const std::string& body) {
  return body.find("\"data\"") != std::string::npos;
}

bool IsPersistedQueryNotFound(const std::string& body) {
  return body.find("PersistedQueryNotFound") != std::string::npos;
}

// Best-effort `"message":"..."` extraction; falls back to a fixed shape.
std::string ExtractGraphQLError(const std::string& body) {
  const std::size_t key = body.find("\"message\"");
  if (key != std::string::npos) {
    const std::size_t colon = body.find(':', key);
    if (colon != std::string::npos) {
      const std::size_t open = body.find('"', colon);
      if (open != std::string::npos) {
        std::string msg;
        for (std::size_t i = open + 1; i < body.size(); ++i) {
          if (body[i] == '\\' && i + 1 < body.size()) {
            msg += body[i + 1];
            ++i;
          } else if (body[i] == '"') {
            break;
          } else {
            msg += body[i];
          }
        }
        if (!msg.empty()) return "graphql: " + msg;
      }
    }
  }
  return "graphql: request returned errors";
}

Response PostOnce(const std::shared_ptr<ITransport>& transport,
                  const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& extra_headers,
                  bool* transport_failed) {
  *transport_failed = true;
  Response resp;
  if (transport == nullptr) return resp;
  Request r;
  r.method = "POST";
  r.url = url;
  r.headers.emplace_back("Content-Type", "application/json");
  for (const auto& h : extra_headers) r.headers.push_back(h);
  r.body = body;
  try {
    resp = transport->execute(r, ExecCtx{});
  } catch (...) {
    return resp;
  }
  *transport_failed = (resp.err != Error::kNone);
  return resp;
}

// Splits a top-level `[{...},{...}]` array into its element bodies.
std::vector<std::string> SplitTopLevelArray(const std::string& body) {
  std::vector<std::string> out;
  const std::size_t open = body.find('[');
  const std::size_t close = body.rfind(']');
  if (open == std::string::npos || close == std::string::npos || close <= open) {
    return out;
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  std::size_t start = std::string::npos;
  for (std::size_t i = open + 1; i < close; ++i) {
    const char c = body[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      if (depth == 0) start = i;
      ++depth;
    } else if (c == '}') {
      if (depth > 0) --depth;
      if (depth == 0 && start != std::string::npos) {
        out.push_back(body.substr(start, i - start + 1));
        start = std::string::npos;
      }
    }
  }
  return out;
}

uint64_t Fnv1a64(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

bool ParseWholeNumber(const std::string& s, long long* out) {
  if (s.empty()) return false;
  long long v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  *out = v;
  return true;
}

}  // namespace

GraphQLClient::Subscription::Subscription(std::vector<std::string> payloads,
                                          std::vector<std::string> ids,
                                          std::string terminal_error)
    : payloads_(std::move(payloads)),
      ids_(std::move(ids)),
      terminal_error_(std::move(terminal_error)) {
  // last_event_id_ starts empty: only next() deliveries advance it.
}

bool GraphQLClient::Subscription::next(Result* out) {
  try {
    if (cancelled_ || out == nullptr) return false;
    if (!terminal_error_.empty()) {
      out->data.clear();
      out->error = terminal_error_;
      return false;
    }
    if (pos_ >= payloads_.size()) return false;
    out->data = payloads_[pos_];
    out->error.clear();
    if (pos_ < ids_.size() && !ids_[pos_].empty()) {
      last_event_id_ = ids_[pos_];
    }
    ++pos_;
    return true;
  } catch (...) {
    return false;
  }
}

void GraphQLClient::Subscription::cancel() { cancelled_ = true; }

void GraphQLClient::Subscription::setLastEventId(std::string id) {
  last_event_id_ = std::move(id);
}

GraphQLClient::GraphQLClient(std::shared_ptr<ITransport> transport)
    : transport_(std::move(transport)) {}

std::string GraphQLClient::queryError(const std::string& query) {
  if (query.empty()) return "graphql: empty query";
  if (query.size() >= kMaxQueryBytes) return "graphql: query too large";
  return "";
}

GraphQLClient::Result GraphQLClient::executeSingle(const std::string& query,
                                    const std::string& vars) {
  Result r;
  const std::string invalid = queryError(query);
  if (!invalid.empty()) {
    r.error = invalid;
    return r;
  }
  if (transport_ == nullptr) {
    r.error = "graphql: null transport";
    return r;
  }
  if (apq_) {
    // APQ: sha256 hash first, full query only on PersistedQueryNotFound.
    bool failed = false;
    Response first =
        PostOnce(transport_, "/graphql", OperationPayload(query, vars, true),
                 {}, &failed);
    if (failed) {
      r.error = "graphql: transport error";
      return r;
    }
    if (!IsPersistedQueryNotFound(first.body)) {
      if (HasErrors(first.body)) {
        r.error = ExtractGraphQLError(first.body);
        return r;
      }
      if (HasData(first.body)) {
        r.data = first.body;
        return r;
      }
      r.error = "graphql: unexpected response";
      return r;
    }
    Response second =
        PostOnce(transport_, "/graphql", OperationPayload(query, vars, false),
                 {}, &failed);
    if (failed) {
      r.error = "graphql: transport error";
      return r;
    }
    if (HasErrors(second.body)) {
      r.error = ExtractGraphQLError(second.body);
      return r;
    }
    if (HasData(second.body)) {
      r.data = second.body;
      return r;
    }
    r.error = "graphql: unexpected response";
    return r;
  }
  bool failed = false;
  Response resp = PostOnce(transport_, "/graphql",
                           OperationPayload(query, vars, false), {}, &failed);
  if (failed) {
    r.error = "graphql: transport error";
    return r;
  }
  if (HasErrors(resp.body)) {
    r.error = ExtractGraphQLError(resp.body);
    return r;
  }
  if (HasData(resp.body)) {
    r.data = resp.body;
    return r;
  }
  r.error = "graphql: unexpected response";
  return r;
}

GraphQLClient::Result GraphQLClient::flushBatch() {
  // One transport call for every queued operation; results split back in
  // request order. Callers that buffered earlier already returned a
  // provisional success; the flushing call returns its own element.
  Result fallback;
  if (pending_.empty()) {
    fallback.error = "graphql: empty batch";
    return fallback;
  }
  const std::vector<Pending> batch = pending_;
  pending_.clear();
  if (transport_ == nullptr) {
    fallback.error = "graphql: null transport";
    return fallback;
  }
  std::string body = "[";
  for (std::size_t i = 0; i < batch.size(); ++i) {
    if (i > 0) body += ",";
    body += OperationPayload(batch[i].query, batch[i].vars, false);
  }
  body += "]";
  bool failed = false;
  Response resp = PostOnce(transport_, "/graphql", body, {}, &failed);
  if (failed) {
    fallback.error = "graphql: transport error";
    return fallback;
  }
  const std::vector<std::string> elements = SplitTopLevelArray(resp.body);
  if (elements.size() != batch.size()) {
    // Unsplitable batch body: success only when the whole body carries
    // data and no errors.
    if (HasErrors(resp.body)) {
      fallback.error = ExtractGraphQLError(resp.body);
      return fallback;
    }
    if (HasData(resp.body)) {
      fallback.data = resp.body;
      return fallback;
    }
    fallback.error = "graphql: unexpected batch response";
    return fallback;
  }
  Result mine;
  for (std::size_t i = 0; i < elements.size(); ++i) {
    Result e;
    if (HasErrors(elements[i])) {
      e.error = ExtractGraphQLError(elements[i]);
    } else {
      e.data = elements[i];
    }
    if (i + 1 == elements.size()) mine = e;
  }
  return mine;
}

GraphQLClient::Result GraphQLClient::query(std::string q, std::string vars) {
  try {
    const std::string invalid = queryError(q);
    if (!invalid.empty()) {
      Result r;
      r.error = invalid;
      return r;
    }
    if (batching_ && !apq_ && batch_max_ > 0) {
      pending_.push_back(Pending{q, vars});
      if (pending_.size() >= batch_max_) return flushBatch();
      // Queued: provisional success; the flushing call carries this
      // operation in the single coalesced execute.
      Result r;
      return r;
    }
    return executeSingle(q, vars);
  } catch (...) {
    Result r;
    r.error = "graphql: internal error";
    return r;
  }
}

GraphQLClient::Result GraphQLClient::mutate(std::string m, std::string vars) {
  try {
    return query(std::move(m), std::move(vars));
  } catch (...) {
    Result r;
    r.error = "graphql: internal error";
    return r;
  }
}

std::unique_ptr<GraphQLClient::Subscription> GraphQLClient::subscribe(
    std::string q, std::string vars) {
  try {
    if (transport_ == nullptr || q.empty() ||
        q.size() >= kMaxQueryBytes) {
      return std::unique_ptr<Subscription>(new Subscription(
          {},
          {},
          q.empty() ? "graphql: empty query" : "graphql: subscribe failed"));
    }
    Request r;
    r.method = "POST";
    r.url = "/graphql";
    r.headers.emplace_back("Accept", "text/event-stream");
    if (!last_event_id_.empty()) {
      r.headers.emplace_back("Last-Event-ID", last_event_id_);
    }
    r.body = OperationPayload(q, vars, false);
    Response resp;
    try {
      resp = transport_->execute(r, ExecCtx{});
    } catch (...) {
      return std::unique_ptr<Subscription>(
          new Subscription({}, {}, "graphql: transport error"));
    }
    if (resp.err != Error::kNone) {
      return std::unique_ptr<Subscription>(
          new Subscription({}, {}, "graphql: transport error"));
    }
    std::vector<std::string> payloads;
    std::vector<std::string> ids;
    for (const auto& f : detail::ParseSseFrames(resp.body)) {
      const std::string& id = std::get<2>(f);
      // Resume filtering: when resuming from a stored id, only events
      // after it are new; the replayed prefix is dropped so delivery
      // has zero duplicates.
      if (!last_event_id_.empty() && !id.empty()) {
        long long resume = 0;
        long long seen = 0;
        if (ParseWholeNumber(last_event_id_, &resume) &&
            ParseWholeNumber(id, &seen) && seen <= resume) {
          continue;
        }
      }
      payloads.push_back(std::get<1>(f));
      ids.push_back(id);
    }
    return std::unique_ptr<Subscription>(
        new Subscription(std::move(payloads), std::move(ids), ""));
  } catch (...) {
    return std::unique_ptr<Subscription>(
        new Subscription({}, {}, "graphql: subscribe failed"));
  }
}

void GraphQLClient::enableApq(bool enabled) { apq_ = enabled; }

void GraphQLClient::enableBatching(std::size_t max) {
  batching_ = (max > 0);
  batch_max_ = max;
}

void GraphQLClient::setLastEventId(std::string id) {
  last_event_id_ = std::move(id);
}

std::string GraphQLClient::cacheKey(const std::string& query,
                                    const std::string& vars) {
  try {
    char buf[17];
    const uint64_t h = Fnv1a64(query + '\x1f' + vars);
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return "gql:v1:" + std::string(buf);
  } catch (...) {
    return "gql:v1:error";
  }
}

std::string LastEventId(const GraphQLClient::Subscription& sub) {
  try {
    return sub.last_event_id_;
  } catch (...) {
    return "";
  }
}

}  // namespace net
}  // namespace muse
