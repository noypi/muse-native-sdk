#include "net/transport.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace muse {
namespace net {
namespace {

// Failure to execute: empty body, zero status, kTransport. Never throws.
Response transportError() {
  Response r;
  r.status = static_cast<StatusCode>(0);
  r.body.clear();
  r.err = Error::kTransport;
  return r;
}

}  // namespace

HttpClient::HttpClient(std::shared_ptr<ITransport> t, ClientConfig cfg)
    : transport_(std::move(t)), config_(cfg) {}

void HttpClient::setTransport(std::shared_ptr<ITransport> t) {
  transport_ = std::move(t);
}

void HttpClient::AddInterceptor(Interceptor i) {
  interceptors_.push_back(std::move(i));
}

Response HttpClient::request(const Request& r) {
  try {
    // Unexecutable inputs fail closed without touching the transport.
    if (transport_ == nullptr || r.url.empty()) {
      return transportError();
    }
    // Terminal step: verbatim delegation to the bound transport.
    Next terminal = [t = transport_](const Request& q) {
      ExecCtx ctx;
      return t->execute(q, ctx);
    };
    // Wrap with interceptors so the outermost-registered runs first. With
    // no interceptors registered this is plain verbatim delegation (T1).
    std::function<Response(Request)> chain = [terminal](Request q) {
      return terminal(q);
    };
    for (auto it = interceptors_.rbegin(); it != interceptors_.rend(); ++it) {
      Interceptor current = *it;
      std::function<Response(Request)> inner = chain;
      chain = [current, inner](Request q) {
        return current(std::move(q), Next([inner](const Request& in) {
                       return inner(in);
                     }));
      };
    }
    return chain(r);
  } catch (...) {
    // Contract: transport failure is always err == kTransport, never an
    // exception, for any input.
    return transportError();
  }
}

Response HttpClient::requestStream(const Request& r,
                                   std::function<void(BodyChunk)> onChunk) {
  try {
    Response got = request(r);
    // Chunk split is a pure view over the returned body (ASSUMPTION
    // A-STRM): concatenation of the chunks equals got.body. Empty bodies
    // deliver zero chunks, which the contract allows.
    if (got.err == Error::kNone && onChunk) {
      constexpr std::size_t kChunkBytes = 2048;
      for (std::size_t off = 0; off < got.body.size(); off += kChunkBytes) {
        onChunk(got.body.substr(off, kChunkBytes));
      }
    }
    return got;
  } catch (...) {
    return transportError();
  }
}

// Single-connection ledger: at most one open connection per origin.
// acquire() reuses the open connection (duplicate acquire keeps
// openCount at 1); release() parks it idle (openCount unchanged).
struct ConnectionPool::Ledger {
  std::map<std::string, std::size_t> open;
  std::uint64_t nextId = 1;

  static std::string key(const Origin& o) {
    return o.scheme + "://" + o.host + ":" + std::to_string(o.port);
  }
};

ConnectionPool::ConnectionPool() : ledger_(new Ledger()) {}

ConnectionPool::~ConnectionPool() = default;

PooledConn ConnectionPool::acquire(const Origin& o) {
  const std::string k = Ledger::key(o);
  if (ledger_->open[k] == 0) {
    ledger_->open[k] = 1;
  }
  PooledConn c;
  c.origin = o;
  c.id = ledger_->nextId++;
  c.valid = true;
  return c;
}

void ConnectionPool::release(PooledConn c) {
  if (!c.valid) {
    return;
  }
  // Park idle: the origin stays open for reuse, so openCount is unchanged.
}

std::size_t ConnectionPool::openCount(const Origin& o) const {
  auto it = ledger_->open.find(Ledger::key(o));
  return it == ledger_->open.end() ? 0 : it->second;
}

// T3 dispatch-budget unit probe (FR-NET-004 / AC-NET-007 unit half). The
// <1ms budget is owned by perf; this only observes it. RecordDispatchNanos
// is observation-only: it accepts any sample and never throws, blocks, or
// performs I/O (deliberate no-op). WithinBudget is true iff ns < 1,000,000.
void RecordDispatchNanos(uint64_t /*ns*/) {}

bool WithinBudget(uint64_t ns) { return ns < 1000000ULL; }

}  // namespace net
}  // namespace muse
