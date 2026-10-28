// T3 contract tests — FR-LAYOUT-006 / AC-LAYOUT-011 (TC-LAYOUT-011),
// constrained by NFR-LAYOUT-001/AC-LAYOUT-001 (pure C++, no JNI).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line (reproduced verbatim in unit-tests/T3.md):
//   `class layout::LayoutDelegate { public: virtual ~LayoutDelegate() = default;
//    virtual void measure(NodeId id, float availW, float availH) = 0;
//    virtual void arrange(NodeId id, Box bounds) = 0;
//    virtual bool handles(NodeId id) const = 0; };
//    void layout::registerDelegate(Solver& s, NodeId subtreeRoot, LayoutDelegate* d);
//    void layout::unregisterDelegate(Solver& s, NodeId subtreeRoot);`
// plus the plan Acceptance/Detail sentences: registration is a raw-pointer
// install (caller owns lifetime); `nullptr` delegate or unknown root returns
// `false`, never throws; call path is a direct virtual dispatch with no
// marshalling, no JNI, no heap alloc per call; double registration replaces;
// unregister restores the builtin backend.
//
// ASSUMPTION (bool return): the `Interface:` line spells both functions `void`,
// but the T3 Acceptance demands "unknown-root registration returns false" and
// the task restates "`nullptr` delegate or unknown root returns `false`".
// These cases bind to the Acceptance and call `registerDelegate` as returning
// `bool` (true = installed, false = rejected). If the coder implements the
// literal `void` signature, these call sites fail to compile — that is an
// interface dispute to route to the planner (GR-08 T1.10), not a test fix.
//
// ASSUMPTION (subtree walk): `Solver::solve` consults the delegate registry —
// a handled subtree skips the builtin backends and receives measure/arrange;
// an unhandled id (handles() false, or no delegate installed) uses the
// builtin backend. Tests observe this only through boxOf frames and stub
// call counts, never through solver internals.
//
// ASSUMPTION (arrange bounds): the `bounds` delivered to arrange() for a node
// equal the solved `Solver::boxOf(id)` frame for that node. Tests assert the
// forwarded box matches boxOf exactly.
//
// ASSUMPTION (allocation scope): "zero heap alloc per call" is asserted over
// the direct virtual-dispatch path — measure()/arrange() invoked on the
// delegate under a counting global operator new. Solver bookkeeping around
// a full solve() (box vectors, constraint ordering) is coder-owned and out
// of scope for this counter; full-path allocation accounting is reviewer work.
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "layout/delegate.h"
#include "layout/solver.h"

namespace {

// --- Allocation counter: counts heap allocations only while armed. ---
bool g_count_allocs = false;
int g_alloc_count = 0;

bool same(float a, float b) { return a == b; }

bool same_box(const layout::Box& got, float x, float y, float w, float h) {
  return same(got.x, x) && same(got.y, y) && same(got.w, w) && same(got.h, h);
}

bool same_box_box(const layout::Box& a, const layout::Box& b) {
  return same(a.x, b.x) && same(a.y, b.y) && same(a.w, b.w) && same(a.h, b.h);
}

// Counting stub delegate: records every virtual call in-process.
class CountingStub : public layout::LayoutDelegate {
 public:
  explicit CountingStub(bool handle_all = true) : handle_all_(handle_all) {}

  void measure(layout::NodeId id, float availW, float availH) override {
    ++measure_count;
    last_measured = id;
    last_avail_w = availW;
    last_avail_h = availH;
  }

  void arrange(layout::NodeId id, layout::Box bounds) override {
    ++arrange_count;
    last_arranged = id;
    last_bounds = bounds;
  }

  bool handles(layout::NodeId id) const override {
    if (!handle_all_) return false;
    return id != layout::kInvalidNodeId;
  }

  int measure_count = 0;
  int arrange_count = 0;
  layout::NodeId last_measured = layout::kInvalidNodeId;
  layout::NodeId last_arranged = layout::kInvalidNodeId;
  float last_avail_w = 0.0f;
  float last_avail_h = 0.0f;
  layout::Box last_bounds{0.0f, 0.0f, 0.0f, 0.0f};

 private:
  bool handle_all_;
};

// Minimal tree: flex root 100x50 with one flex child.
void build_pair(layout::Solver& solver, layout::NodeId& root,
                layout::NodeId& child) {
  root = solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(root, 100.0f, 50.0f, 0.0f);
  child = solver.addNode(layout::Backend::Flex, root);
  solver.setFlex(child, 100.0f, 50.0f, 0.0f);
}

}  // namespace

// Global heap-allocation hooks: count only while the guard flag is armed so
// unrelated startup/bookskeeping allocations are invisible.
void* operator new(std::size_t n) {
  if (g_count_allocs) ++g_alloc_count;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t n) {
  if (g_count_allocs) ++g_alloc_count;
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// The delegate surface is a direct virtual dispatch on pure C++ types: the
// member-function pointer types pin (NodeId, floats, Box) with no JNI handle,
// no string marshalling, no callback token in the hot signature.
static_assert(std::is_same<decltype(&layout::LayoutDelegate::measure),
                           void (layout::LayoutDelegate::*)(layout::NodeId,
                                                             float, float)>::value,
              "measure must dispatch directly on (NodeId, float, float)");
static_assert(std::is_same<decltype(&layout::LayoutDelegate::arrange),
                           void (layout::LayoutDelegate::*)(layout::NodeId,
                                                             layout::Box)>::value,
              "arrange must dispatch directly on (NodeId, Box)");
static_assert(std::is_same<decltype(&layout::LayoutDelegate::handles),
                           bool (layout::LayoutDelegate::*)(layout::NodeId) const>::value,
              "handles must dispatch directly on (NodeId)");
static_assert(std::has_virtual_destructor<layout::LayoutDelegate>::value,
              "LayoutDelegate must be deletable through the base pointer");

// Named case (AC-LAYOUT-011): registering a delegate at a subtree root drives
// that subtree through measure/arrange on solve.
static bool check_register_drives_subtree_through_delegate() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub stub;
  bool installed = layout::registerDelegate(solver, root, &stub);
  if (!installed) return false;
  solver.solve(root);
  if (stub.measure_count < 1) return false;
  if (stub.arrange_count < 1) return false;
  return true;
}

// Named case (AC-LAYOUT-011): the bounds forwarded to arrange() are exactly
// the solved frame for that node.
static bool check_arrange_receives_exact_solved_bounds() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub stub;
  if (!layout::registerDelegate(solver, root, &stub)) return false;
  solver.solve(root);
  if (stub.arrange_count < 1) return false;
  const layout::Box& solved = solver.boxOf(stub.last_arranged);
  return same_box_box(stub.last_bounds, solved);
}

// Named case (AC-LAYOUT-011): unregister restores the builtin backend —
// solving afterwards issues no delegate calls and still resolves frames.
static bool check_unregister_restores_builtin_backend() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub stub;
  if (!layout::registerDelegate(solver, root, &stub)) return false;
  solver.solve(root);
  if (stub.measure_count < 1) return false;
  layout::unregisterDelegate(solver, root);
  int measures_before = stub.measure_count;
  int arranges_before = stub.arrange_count;
  layout::Box box = solver.solve(root);
  if (!same_box(box, 0.0f, 0.0f, 100.0f, 50.0f)) return false;
  return stub.measure_count == measures_before &&
         stub.arrange_count == arranges_before;
}

// Named case (T3 Detail): double registration replaces — only the newest
// delegate receives calls.
static bool check_double_registration_replaces() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub first;
  CountingStub second;
  if (!layout::registerDelegate(solver, root, &first)) return false;
  if (!layout::registerDelegate(solver, root, &second)) return false;
  solver.solve(root);
  if (second.measure_count < 1) return false;
  return first.measure_count == 0 && first.arrange_count == 0;
}

// Named case (T3 Acceptance): registration at an unknown root returns false
// and never throws; the solver still solves with the builtin backend.
static bool check_unknown_root_registration_returns_false() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub stub;
  bool installed = false;
  try {
    installed = layout::registerDelegate(
        solver, static_cast<layout::NodeId>(9999), &stub);
  } catch (...) {
    return false;  // must never throw.
  }
  if (installed) return false;
  layout::Box box = solver.solve(root);
  return same_box(box, 0.0f, 0.0f, 100.0f, 50.0f) &&
         stub.measure_count == 0 && stub.arrange_count == 0;
}

// Named case (T3 Detail): a nullptr delegate returns false and never throws;
// the builtin backend keeps working.
static bool check_nullptr_registration_returns_false() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  bool installed = false;
  try {
    installed = layout::registerDelegate(solver, root, nullptr);
  } catch (...) {
    return false;  // must never throw.
  }
  if (installed) return false;
  layout::Box box = solver.solve(root);
  return same_box(box, 0.0f, 0.0f, 100.0f, 50.0f);
}

// Named case (T3 Detail): unregistering an unknown root never throws.
static bool check_unregister_unknown_root_never_throws() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  try {
    layout::unregisterDelegate(solver, static_cast<layout::NodeId>(9999));
  } catch (...) {
    return false;
  }
  return true;
}

// Named case: a delegate whose handles() is false for every id never receives
// calls — the builtin backend solves the tree instead.
static bool check_handles_false_skips_delegate() {
  layout::Solver solver;
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId child = layout::kInvalidNodeId;
  build_pair(solver, root, child);
  CountingStub stub(/*handle_all=*/false);
  if (!layout::registerDelegate(solver, root, &stub)) return false;
  layout::Box box = solver.solve(root);
  if (!same_box(box, 0.0f, 0.0f, 100.0f, 50.0f)) return false;
  return stub.measure_count == 0 && stub.arrange_count == 0;
}

// Named case (AC-LAYOUT-011 zero-cost): direct virtual dispatch performs no
// heap allocation per call — measure()+arrange() under a counting operator
// new report zero allocations.
static bool check_direct_dispatch_allocates_nothing() {
  CountingStub stub;
  layout::Box bounds{10.0f, 20.0f, 30.0f, 40.0f};
  g_alloc_count = 0;
  g_count_allocs = true;
  stub.measure(static_cast<layout::NodeId>(0), 100.0f, 50.0f);
  stub.arrange(static_cast<layout::NodeId>(0), bounds);
  volatile bool h =
      stub.handles(static_cast<layout::NodeId>(0));  // keep the call live.
  g_count_allocs = false;
  if (!h) return false;
  if (stub.measure_count != 1 || stub.arrange_count != 1) return false;
  return g_alloc_count == 0;
}

// Named case: raw-pointer install — the caller owns lifetime. Destroying the
// solver while a stack-owned delegate is installed, and deleting a delegate
// through the base pointer, must both be safe (no ownership taken).
static bool check_caller_owns_delegate_lifetime() {
  {
    layout::Solver solver;
    layout::NodeId root = layout::kInvalidNodeId;
    layout::NodeId child = layout::kInvalidNodeId;
    build_pair(solver, root, child);
    CountingStub stub;
    if (!layout::registerDelegate(solver, root, &stub)) return false;
    solver.solve(root);
  }  // solver destroyed while the stack stub outlives it.
  layout::LayoutDelegate* owned = new CountingStub();
  layout::NodeId some = static_cast<layout::NodeId>(7);
  owned->measure(some, 1.0f, 1.0f);
  owned->arrange(some, layout::Box{0.0f, 0.0f, 1.0f, 1.0f});
  if (!owned->handles(some)) {
    delete owned;
    return false;
  }
  delete owned;  // virtual dtor through the base pointer.
  return true;
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_register_drives_subtree_through_delegate);
  RUN_CASE(check_arrange_receives_exact_solved_bounds);
  RUN_CASE(check_unregister_restores_builtin_backend);
  RUN_CASE(check_double_registration_replaces);
  RUN_CASE(check_unknown_root_registration_returns_false);
  RUN_CASE(check_nullptr_registration_returns_false);
  RUN_CASE(check_unregister_unknown_root_never_throws);
  RUN_CASE(check_handles_false_skips_delegate);
  RUN_CASE(check_direct_dispatch_allocates_nothing);
  RUN_CASE(check_caller_owns_delegate_lifetime);
  if (failures > 0) {
    std::printf("delegate_test: %d failing\n", failures);
    return 1;
  }
  std::printf("delegate_test: all passing\n");
  return 0;
}
