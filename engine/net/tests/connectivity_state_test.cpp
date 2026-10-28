// Batch 3 contract tests — FR-NET-013 / AC-NET-026 (TC-NET-026).
// Covers T6 (connectivity state: captive-portal validated signal, metered
// prefetch suppression, cleartext blocking via injected
// INetworkSecurityConfig; ConnectivityManager faked via onPlatformEvent).
// Hermetic: C++17 + committed net headers only; no network, no env/config
// reads, no live services, no Android framework. Thresholds arrive as
// injected NetInfo values; the security config is an in-process fake.
// Binds ONLY to the T6 `Interface:` line; never to implementation.
#include <cstdio>
#include <string>
#include <vector>

#include "net/connectivity.h"
#include "net/transport_policy.h"

namespace {

// --- Fakes (in-process only; no I/O, no threads, no env) ---------------------

class FakeSecurityConfig : public muse::net::INetworkSecurityConfig {
 public:
  explicit FakeSecurityConfig(std::vector<std::string> allowed)
      : allowed_(std::move(allowed)) {}

  bool cleartextPermitted(const std::string& domain) const override {
    for (const auto& entry : allowed_) {
      if (entry == domain) return true;
    }
    return false;
  }

 private:
  std::vector<std::string> allowed_;
};

muse::net::NetInfo makeInfo(muse::net::NetState state, bool metered,
                            bool vpn) {
  muse::net::NetInfo info;
  info.state = state;
  info.metered = metered;
  info.vpn = vpn;
  return info;
}

muse::net::Origin makeOrigin(const std::string& scheme,
                             const std::string& host) {
  muse::net::Origin origin;
  origin.scheme = scheme;
  origin.host = host;
  return origin;
}

// --- Signal emission ---------------------------------------------------------

// Named case: captive-portal event emits kOnlineUnvalidated to the listener.
static bool check_captive_portal_emits_unvalidated() {
  muse::net::ConnectivityMonitor monitor;
  std::vector<muse::net::NetInfo> seen;
  monitor.setListener([&seen](muse::net::NetInfo info) { seen.push_back(info); });
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOnlineUnvalidated, false, false));
  if (seen.size() != 1) return false;
  return seen[0].state == muse::net::NetState::kOnlineUnvalidated;
}

// Named case: offline event reaches the listener as kOffline.
static bool check_offline_event_emits_offline() {
  muse::net::ConnectivityMonitor monitor;
  std::vector<muse::net::NetInfo> seen;
  monitor.setListener([&seen](muse::net::NetInfo info) { seen.push_back(info); });
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOffline, false, false));
  if (seen.size() != 1) return false;
  return seen[0].state == muse::net::NetState::kOffline;
}

// Named case: validated event reaches the listener as kOnlineValidated.
static bool check_validated_event_emits_validated() {
  muse::net::ConnectivityMonitor monitor;
  std::vector<muse::net::NetInfo> seen;
  monitor.setListener([&seen](muse::net::NetInfo info) { seen.push_back(info); });
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOnlineValidated, false, false));
  if (seen.size() != 1) return false;
  return seen[0].state == muse::net::NetState::kOnlineValidated;
}

// Named case: every platform event is forwarded in order.
static bool check_events_forwarded_in_order() {
  muse::net::ConnectivityMonitor monitor;
  std::vector<muse::net::NetInfo> seen;
  monitor.setListener([&seen](muse::net::NetInfo info) { seen.push_back(info); });
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOffline, false, false));
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOnlineUnvalidated, false, false));
  monitor.onPlatformEvent(
      makeInfo(muse::net::NetState::kOnlineValidated, false, false));
  if (seen.size() != 3) return false;
  return seen[0].state == muse::net::NetState::kOffline &&
         seen[1].state == muse::net::NetState::kOnlineUnvalidated &&
         seen[2].state == muse::net::NetState::kOnlineValidated;
}

// --- Prefetch suppression ----------------------------------------------------

// Named case: validated + unmetered allows prefetch.
static bool check_validated_unmetered_allows_prefetch() {
  muse::net::ConnectivityMonitor monitor;
  return monitor.shouldPrefetch(
      makeInfo(muse::net::NetState::kOnlineValidated, false, false));
}

// Named case: metered toggle suppresses prefetch even when validated.
static bool check_metered_suppresses_prefetch() {
  muse::net::ConnectivityMonitor monitor;
  return !monitor.shouldPrefetch(
      makeInfo(muse::net::NetState::kOnlineValidated, true, false));
}

// Named case: unvalidated (captive portal) never allows prefetch.
static bool check_unvalidated_denies_prefetch() {
  muse::net::ConnectivityMonitor monitor;
  if (monitor.shouldPrefetch(
          makeInfo(muse::net::NetState::kOnlineUnvalidated, false, false)))
    return false;
  return !monitor.shouldPrefetch(
      makeInfo(muse::net::NetState::kOnlineUnvalidated, true, false));
}

// Named case: offline never allows prefetch.
static bool check_offline_denies_prefetch() {
  muse::net::ConnectivityMonitor monitor;
  if (monitor.shouldPrefetch(
          makeInfo(muse::net::NetState::kOffline, false, false)))
    return false;
  return !monitor.shouldPrefetch(
      makeInfo(muse::net::NetState::kOffline, true, false));
}

// --- Cleartext blocking ------------------------------------------------------

// Named case: a cleartext-denied domain is blocked.
static bool check_cleartext_denied_domain_blocked() {
  muse::net::ConnectivityMonitor monitor;
  FakeSecurityConfig config({"allowed.test"});
  return !monitor.isCleartextAllowed(makeOrigin("http", "denied.test"),
                                     config);
}

// Named case: an allowed domain passes.
static bool check_cleartext_allowed_domain_passes() {
  muse::net::ConnectivityMonitor monitor;
  FakeSecurityConfig config({"allowed.test"});
  return monitor.isCleartextAllowed(makeOrigin("http", "allowed.test"),
                                    config);
}

// Named case: empty allow-list blocks every domain (deny-by-default).
static bool check_cleartext_empty_allowlist_blocks_all() {
  muse::net::ConnectivityMonitor monitor;
  FakeSecurityConfig config({});
  return !monitor.isCleartextAllowed(makeOrigin("http", "any.test"), config);
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_captive_portal_emits_unvalidated);
  RUN_CASE(check_offline_event_emits_offline);
  RUN_CASE(check_validated_event_emits_validated);
  RUN_CASE(check_events_forwarded_in_order);
  RUN_CASE(check_validated_unmetered_allows_prefetch);
  RUN_CASE(check_metered_suppresses_prefetch);
  RUN_CASE(check_unvalidated_denies_prefetch);
  RUN_CASE(check_offline_denies_prefetch);
  RUN_CASE(check_cleartext_denied_domain_blocked);
  RUN_CASE(check_cleartext_allowed_domain_passes);
  RUN_CASE(check_cleartext_empty_allowlist_blocks_all);
  if (failures > 0) {
    std::printf("connectivity_state_test: %d failing\n", failures);
    return 1;
  }
  std::printf("connectivity_state_test: all passing\n");
  return 0;
}
