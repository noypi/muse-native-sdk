#pragma once

// Shared vocabulary types for the transport-agnostic HTTP surface
// (FR-NET-001 / AC-NET-002) and the client core (FR-NET-004 / AC-NET-007).
//
// One definition lives here so the shared contract test
// (engine/net/tests/transport_api_test.cpp, which hosts both the T1 and the
// T2 cases) binds to a single HttpClient / pool / interceptor surface:
// T2's headers (net/http_client.h, net/interceptor.h) re-export these names
// instead of redefining them.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace muse {
namespace net {

// Transport failure vs success. Only failure to execute maps to kTransport;
// an HTTP error status delivered by the transport is data (err == kNone).
enum class Error {
  kNone,
  kTransport,
};

// HTTP status code; int-convertible for comparison.
enum class StatusCode : int {
  kUnknown = 0,
  kOk = 200,
};

// Application-visible request shape (ASSUMPTION A-REQ).
struct Request {
  std::string method;
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

// Application-visible response shape (ASSUMPTION A-RESP): a delivered HTTP
// error status keeps err == kNone; only failure to execute is kTransport.
struct Response {
  StatusCode status = static_cast<StatusCode>(0);
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  Error err = Error::kNone;
};

// Per-call execution context (ASSUMPTION A-CTX). Carries the plan's T1
// detail (deadline plus consumed worker-pool / TLS-policy handles); the
// pool and the policy are owned elsewhere, so the handles stay opaque.
struct ExecCtx {
  std::uint64_t deadlineNs = 0;
  const void* workerPool = nullptr;
  const void* tlsPolicy = nullptr;
};

// Client construction options (ASSUMPTION A-CTX). Empty in this slice;
// timeout / policy injection lands with retry/resilience (T6/T7).
struct ClientConfig {};

// Connection-pool origin key (ASSUMPTION A-POOL): aggregate of
// {scheme, host, port}.
struct Origin {
  std::string scheme;
  std::string host;
  std::uint16_t port = 443;
};

// Pooled connection handle: movable value; validity is tracked by the pool.
struct PooledConn {
  Origin origin;
  std::uint64_t id = 0;
  bool valid = false;
};

// Pool seam (T2 interface line): bookkeeping over Origins, no sockets.
class IConnectionPool {
 public:
  virtual ~IConnectionPool() = default;
  virtual PooledConn acquire(const Origin& o) = 0;
  virtual void release(PooledConn c) = 0;
  virtual std::size_t openCount(const Origin& o) const = 0;
};

// Concrete pool (ASSUMPTION A-POOL): at most one open connection per
// origin; release() parks the connection idle so a later acquire() reuses
// it with openCount unchanged. Method definitions live in
// src/transport.cc (shared-file rule: one TU owns the ledger); the T2
// connection_pool.cc TU must not redefine them.
class ConnectionPool : public IConnectionPool {
 public:
  ConnectionPool();
  ~ConnectionPool() override;
  PooledConn acquire(const Origin& o) override;
  void release(PooledConn c) override;
  std::size_t openCount(const Origin& o) const override;

 private:
  struct Ledger;
  std::unique_ptr<Ledger> ledger_;
};

// Chain-continuation callable (ASSUMPTION A-NEXT): invokes the rest of the
// interceptor chain, or the transport at the terminal step.
using Next = std::function<Response(const Request&)>;

// Interceptor (T2 interface line): outermost-registered runs first.
// Short-circuit by returning a Response without calling next.
using Interceptor = std::function<Response(Request, Next)>;

// Streaming body chunk (ASSUMPTION A-STRM): opaque bytes as std::string;
// chunk concatenation equals the returned body.
using BodyChunk = std::string;

}  // namespace net
}  // namespace muse
