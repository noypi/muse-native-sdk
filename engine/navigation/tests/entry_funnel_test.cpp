// T9 contract tests — FR-NAV-010 / AC-NAV-029 (TC-NAV-029).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
//
// Binds to the plan T9 `Interface:` line (verbatim):
//   `FunnelResult routeEntry(const EntryIntent& in);`
// where `EntryIntent{source:string, uri:string, routeId:string}`
// (`source` ∈ launcher/deeplink/notification/service/onNewIntent) and
// `FunnelResult{ok:bool, routeId:string, deduped:bool, error:NavError}`.
//
// ASSUMPTION (free function / global state): the plan names an unqualified
// free function; tests call `routeEntry` in the global namespace. Dedup
// state is therefore process-global; each check below uses a distinct
// routeId so checks stay independent without shared mutable state.
// ASSUMPTION (empty route): an intent with an empty routeId is rejected
// (`ok=false`) per the deny-by-default precedent — no destination is ever
// created from a routeless intent. `error` is `NavError::NONE` on success.
// ASSUMPTION (dedup key): dedup is keyed on the full intent
// (source+uri+routeId); the same route reached from a *different* source is
// a distinct entry and is not deduped.
#include <cstdio>
#include <string>

#include "navigation/entry_funnel.h"
#include "navigation/graph.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

EntryIntent intent(const std::string& source, const std::string& route) {
  EntryIntent in;
  in.source = source;
  in.uri = "app://" + route;
  in.routeId = route;
  return in;
}

bool check_launcher_entry_resolves_without_duplicate() {
  FunnelResult r = routeEntry(intent("launcher", "t9-home-launcher"));
  return r.ok && !r.deduped && r.routeId == "t9-home-launcher" &&
         r.error == NavError::NONE;
}

bool check_deeplink_entry_resolves_without_duplicate() {
  FunnelResult r = routeEntry(intent("deeplink", "t9-detail-deeplink"));
  return r.ok && !r.deduped && r.routeId == "t9-detail-deeplink" &&
         r.error == NavError::NONE;
}

bool check_notification_entry_resolves_without_duplicate() {
  FunnelResult r = routeEntry(intent("notification", "t9-inbox-notif"));
  return r.ok && !r.deduped && r.routeId == "t9-inbox-notif" &&
         r.error == NavError::NONE;
}

bool check_service_entry_resolves_without_duplicate() {
  FunnelResult r = routeEntry(intent("service", "t9-sync-service"));
  return r.ok && !r.deduped && r.routeId == "t9-sync-service" &&
         r.error == NavError::NONE;
}

bool check_on_new_intent_entry_resolves_without_duplicate() {
  FunnelResult r = routeEntry(intent("onNewIntent", "t9-warm-newintent"));
  return r.ok && !r.deduped && r.routeId == "t9-warm-newintent" &&
         r.error == NavError::NONE;
}

bool check_repeat_intent_dedupes_instead_of_pushing() {
  EntryIntent in = intent("notification", "t9-repeat-dedup");
  FunnelResult first = routeEntry(in);
  if (!first.ok || first.deduped) return false;
  FunnelResult second = routeEntry(in);
  return second.ok && second.deduped &&
         second.routeId == "t9-repeat-dedup";
}

bool check_same_route_from_different_source_is_not_deduped() {
  FunnelResult a = routeEntry(intent("launcher", "t9-shared-route"));
  FunnelResult b = routeEntry(intent("deeplink", "t9-shared-route"));
  return a.ok && !a.deduped && b.ok && !b.deduped;
}

bool check_empty_route_rejected() {
  EntryIntent in;
  in.source = "deeplink";
  in.uri = "app://";
  in.routeId = "";
  FunnelResult r = routeEntry(in);
  return !r.ok && !r.deduped;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_launcher_entry_resolves_without_duplicate(),
              "launcher_entry_resolves_without_duplicate");
  EXPECT_TRUE(check_deeplink_entry_resolves_without_duplicate(),
              "deeplink_entry_resolves_without_duplicate");
  EXPECT_TRUE(check_notification_entry_resolves_without_duplicate(),
              "notification_entry_resolves_without_duplicate");
  EXPECT_TRUE(check_service_entry_resolves_without_duplicate(),
              "service_entry_resolves_without_duplicate");
  EXPECT_TRUE(check_on_new_intent_entry_resolves_without_duplicate(),
              "on_new_intent_entry_resolves_without_duplicate");
  EXPECT_TRUE(check_repeat_intent_dedupes_instead_of_pushing(),
              "repeat_intent_dedupes_instead_of_pushing");
  EXPECT_TRUE(check_same_route_from_different_source_is_not_deduped(),
              "same_route_from_different_source_is_not_deduped");
  EXPECT_TRUE(check_empty_route_rejected(), "empty_route_rejected");
  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures == 0 ? 0 : 1;
}
