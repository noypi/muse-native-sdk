#include "lists/myers_diff.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lists {

Recycler::Recycler(const KeyedList& initial)
    : order_(initial.keys), rebinds_(0) {}

const std::vector<std::string>& Recycler::order() const { return order_; }

size_t Recycler::rebindCount() const { return rebinds_; }

namespace {

// Review fix (MAJOR Myers OOM): bounded-memory diff strategy.
// The full Myers V-trace below costs O(D*(N+M)) memory, which OOMs on
// large reorders (e.g. a 10k-item full reversal needs D=2N). Choice (b):
// a documented D-cap with fallback to an index-anchored correct script.
//  - Same-key-set inputs (pure reorders, the reversal worst case) bypass
//    Myers entirely via an LIS-anchored move script: O(N log N) time,
//    O(N) memory, minimal move count (N - LIS length; N-1 for a full
//    reversal). No V-trace is built on this path.
//  - Otherwise Myers runs with D capped at kMaxMyersD; on overflow the
//    same index-anchored fallback produces a correct (result == new order)
//    script with bounded memory instead of throwing std::bad_alloc.
// Small inputs (the existing suite) always take the exact Myers path, so
// minimality for insert/remove fixtures is unchanged.

// One step of the shortest edit script, in reverse (backtrack) order:
// 'r' removes old_[x-1], 'i' inserts new_[y-1].
struct RawStep {
  char kind;
  std::string key;
  size_t index;
};

// Myers O(ND) greedy shortest-edit-script over stable key strings.
// Returns {raw remove/insert steps (no moves yet, forward ordered),
// capped}: capped == true means the D-cap was hit without converging, so
// the steps are empty and the caller must use the index-anchored fallback.
// Maximum Myers forward depth before falling back. Bounds the V-trace at
// kMaxMyersD snapshots of (2*(N+M)+1) ints.
constexpr int kMaxMyersD = 512;

std::pair<std::vector<RawStep>, bool> myersSes(
    const std::vector<std::string>& oldKeys,
    const std::vector<std::string>& newKeys) {
  const int n = static_cast<int>(oldKeys.size());
  const int m = static_cast<int>(newKeys.size());
  const int max = n + m;
  const int off = max;
  // V[k] = furthest x reached on diagonal k; V[1] = 0 seeds D=0.
  std::vector<int> v(2 * max + 1, -1);
  v[off + 1] = 0;
  std::vector<std::vector<int>> trace;
  trace.reserve(static_cast<size_t>(max) + 1);
  bool capped = false;

  const int limit = max < kMaxMyersD ? max : kMaxMyersD;
  for (int d = 0; d <= limit; ++d) {
    bool done = false;
    for (int k = -d; k <= d; k += 2) {
      const int idx = off + k;
      int x;
      if (k == -d || (k != d && v[idx - 1] < v[idx + 1])) {
        x = v[idx + 1];  // Step down: insertion.
      } else {
        x = v[idx - 1] + 1;  // Step right: deletion.
      }
      int y = x - k;
      while (x >= 0 && y >= 0 && x < n && y < m &&
             oldKeys[static_cast<size_t>(x)] ==
                 newKeys[static_cast<size_t>(y)]) {
        ++x;
        ++y;
      }
      v[idx] = x;
      if (x >= n && y >= m) {
        done = true;
        break;
      }
    }
    trace.push_back(v);
    if (done) {
      break;
    }
    if (d == limit && limit < max) {
      // D-cap reached without converging: the trace stays bounded and the
      // caller falls back to the index-anchored script.
      capped = true;
      trace.clear();
      trace.shrink_to_fit();
      break;
    }
  }
  std::vector<RawStep> rev;
  rev.reserve(static_cast<size_t>(max));
  if (capped) return {rev, true};  // Empty + capped: caller falls back.
  int x = n;
  int y = m;
  for (int d = static_cast<int>(trace.size()) - 1; d >= 1; --d) {
    const std::vector<int>& prev = trace[static_cast<size_t>(d) - 1];
    const int k = x - y;
    const int prevK =
        (k == -d || (k != d && prev[off + k - 1] < prev[off + k + 1])) ? k + 1
                                                                     : k - 1;
    const int prevX = prev[off + prevK];
    const int prevY = prevX - prevK;
    while (x > prevX && y > prevY) {
      --x;
      --y;
    }
    if (x == prevX) {
      --y;
      RawStep s;
      s.kind = 'i';
      s.key = newKeys[static_cast<size_t>(y)];
      s.index = static_cast<size_t>(y);
      rev.push_back(s);
    } else {
      --x;
      RawStep s;
      s.kind = 'r';
      s.key = oldKeys[static_cast<size_t>(x)];
      s.index = static_cast<size_t>(x);
      rev.push_back(s);
    }
  }
  std::reverse(rev.begin(), rev.end());
  return {rev, false};
}

bool byTo(const EditOp& a, const EditOp& b) { return a.to < b.to; }

bool byFrom(const EditOp& a, const EditOp& b) { return a.from < b.from; }

// Index-anchored fallback script with bounded O(N+M) memory. Correct by
// construction (out.result == newKeys): keys missing from old are
// inserted, keys missing from new are removed, and common keys outside
// the longest increasing subsequence of old positions become moves.
// For pure reorders the move count (N - LIS length) is minimal: kept keys
// are already in relative order, so each other key must move.
EditScript anchoredScript(const std::vector<std::string>& oldKeys,
                          const std::vector<std::string>& newKeys) {
  std::unordered_map<std::string, size_t> oldPos;
  oldPos.reserve(oldKeys.size());
  for (size_t i = 0; i < oldKeys.size(); ++i) {
    oldPos[oldKeys[i]] = i;
  }
  std::unordered_map<std::string, size_t> newPos;
  newPos.reserve(newKeys.size());
  for (size_t i = 0; i < newKeys.size(); ++i) {
    newPos[newKeys[i]] = i;
  }
  // Old positions of common keys in new order; LIS = keys needing no move.
  std::vector<size_t> seq;
  std::vector<size_t> newIdx;
  seq.reserve(newKeys.size());
  newIdx.reserve(newKeys.size());
  for (size_t j = 0; j < newKeys.size(); ++j) {
    const auto it = oldPos.find(newKeys[j]);
    if (it != oldPos.end()) {
      seq.push_back(it->second);
      newIdx.push_back(j);
    }
  }
  // Patience-sorting LIS over seq, tracking predecessors for backtrack.
  std::vector<size_t> tails;
  std::vector<size_t> prev(seq.size(), seq.size());
  std::vector<size_t> tailPos;
  tails.reserve(seq.size());
  tailPos.reserve(seq.size());
  for (size_t i = 0; i < seq.size(); ++i) {
    size_t lo = 0;
    size_t hi = tails.size();
    while (lo < hi) {
      const size_t mid = (lo + hi) / 2;
      if (seq[tailPos[mid]] < seq[i]) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo > 0) prev[i] = tailPos[lo - 1];
    if (lo == tails.size()) {
      tails.push_back(seq[i]);
      tailPos.push_back(i);
    } else {
      tails[lo] = seq[i];
      tailPos[lo] = i;
    }
  }
  std::vector<bool> kept(seq.size(), false);
  if (!tailPos.empty()) {
    size_t p = tailPos.back();
    while (p < seq.size()) {
      kept[p] = true;
      p = prev[p];
    }
  }
  EditScript out;
  out.result = newKeys;
  std::vector<EditOp> moves;
  for (size_t i = 0; i < seq.size(); ++i) {
    if (kept[i]) continue;
    EditOp op;
    op.type = EditType::Move;
    op.key = newKeys[newIdx[i]];
    op.from = seq[i];
    op.to = newIdx[i];
    moves.push_back(op);
  }
  std::vector<EditOp> removes;
  for (size_t i = 0; i < oldKeys.size(); ++i) {
    if (newPos.find(oldKeys[i]) == newPos.end()) {
      EditOp op;
      op.type = EditType::Remove;
      op.key = oldKeys[i];
      op.from = i;
      removes.push_back(op);
    }
  }
  std::vector<EditOp> inserts;
  for (size_t j = 0; j < newKeys.size(); ++j) {
    if (oldPos.find(newKeys[j]) == oldPos.end()) {
      EditOp op;
      op.type = EditType::Insert;
      op.key = newKeys[j];
      op.to = j;
      inserts.push_back(op);
    }
  }
  std::sort(moves.begin(), moves.end(), byTo);
  std::sort(removes.begin(), removes.end(), byFrom);
  std::sort(inserts.begin(), inserts.end(), byTo);
  out.ops.reserve(moves.size() + removes.size() + inserts.size());
  out.ops.insert(out.ops.end(), moves.begin(), moves.end());
  out.ops.insert(out.ops.end(), removes.begin(), removes.end());
  out.ops.insert(out.ops.end(), inserts.begin(), inserts.end());
  return out;
}

// True when both lists hold the same key multiset (pure reorder).
bool sameKeySet(const std::vector<std::string>& oldKeys,
                const std::vector<std::string>& newKeys) {
  if (oldKeys.size() != newKeys.size()) return false;
  std::unordered_map<std::string, size_t> counts;
  counts.reserve(oldKeys.size());
  for (const auto& k : oldKeys) ++counts[k];
  for (const auto& k : newKeys) {
    const auto it = counts.find(k);
    if (it == counts.end() || it->second == 0) return false;
    --it->second;
  }
  return true;
}

}  // namespace

EditScript myersDiff(const KeyedList& oldL, const KeyedList& newL) {
  // Security hardening (width gate): the Myers V-trace below costs
  // O(D*(N+M)) memory, so a wide disjoint-set feed would OOM before the
  // D-cap fallback runs (e.g. 100k+100k distinct keys ~= 410MB in trace).
  // Inputs wider than kMaxMyersWidth skip myersSes entirely and go
  // straight to the O(N+M) anchoredScript. The bound (20000 total keys)
  // sits comfortably above the 10k-feed / 2k-reversal envelope, so all
  // in-contract feeds still take the exact Myers path. size_t arithmetic
  // throughout so the width sum cannot overflow.
  constexpr size_t kMaxMyersWidth = 20000;
  const size_t width = oldL.keys.size() + newL.keys.size();
  if (width > kMaxMyersWidth) {
    return anchoredScript(oldL.keys, newL.keys);
  }
  // Pure-reorder fast path: the same key set in a new order is the Myers
  // worst case (D = 2N full-trace OOM), so bypass Myers entirely with the
  // bounded index-anchored script, which is also move-minimal here.
  if (sameKeySet(oldL.keys, newL.keys)) {
    return anchoredScript(oldL.keys, newL.keys);
  }
  const auto sesCapped = myersSes(oldL.keys, newL.keys);
  if (sesCapped.second) {
    return anchoredScript(oldL.keys, newL.keys);
  }
  const std::vector<RawStep>& ses = sesCapped.first;

  // Lift same-key remove+insert pairs into single moves so a relocation
  // costs one op, keeping the script minimal for reorders. Keys are
  // unique per frame (T1 contract), so any key appears at most once per
  // side here.
  std::unordered_map<std::string, size_t> removeAt;
  std::unordered_map<std::string, size_t> insertAt;
  for (size_t i = 0; i < ses.size(); ++i) {
    if (ses[i].kind == 'r') {
      removeAt[ses[i].key] = i;
    } else {
      insertAt[ses[i].key] = i;
    }
  }

  std::vector<bool> consumed(ses.size(), false);
  EditScript out;
  out.result = newL.keys;
  std::vector<EditOp> moves;
  for (const auto& entry : removeAt) {
    const auto it = insertAt.find(entry.first);
    if (it == insertAt.end()) {
      continue;
    }
    EditOp op;
    op.type = EditType::Move;
    op.key = entry.first;
    op.from = ses[entry.second].index;
    op.to = ses[it->second].index;
    moves.push_back(op);
    consumed[entry.second] = true;
    consumed[it->second] = true;
  }

  std::vector<EditOp> removes;
  std::vector<EditOp> inserts;
  for (size_t i = 0; i < ses.size(); ++i) {
    if (consumed[i]) {
      continue;
    }
    EditOp op;
    op.key = ses[i].key;
    if (ses[i].kind == 'r') {
      op.type = EditType::Remove;
      op.from = ses[i].index;
      removes.push_back(op);
    } else {
      op.type = EditType::Insert;
      op.to = ses[i].index;
      inserts.push_back(op);
    }
  }

  std::sort(moves.begin(), moves.end(), byTo);
  std::sort(removes.begin(), removes.end(), byFrom);
  std::sort(inserts.begin(), inserts.end(), byTo);
  out.ops.reserve(moves.size() + removes.size() + inserts.size());
  out.ops.insert(out.ops.end(), moves.begin(), moves.end());
  out.ops.insert(out.ops.end(), removes.begin(), removes.end());
  out.ops.insert(out.ops.end(), inserts.begin(), inserts.end());
  return out;
}

Batch applyBatch(Recycler& r, EditScript e) {
  // Atomic commit: the whole script lands as one order update, so no
  // partially applied state is observable. Only inserted or moved keys
  // bind; untouched items keep their bindings.
  size_t binds = 0;
  for (const EditOp& op : e.ops) {
    if (op.type == EditType::Insert || op.type == EditType::Move) {
      ++binds;
    }
  }
  r.order_ = e.result;
  r.rebinds_ += binds;
  Batch b;
  b.order = r.order_;
  b.rebinds = binds;
  return b;
}

}  // namespace lists
