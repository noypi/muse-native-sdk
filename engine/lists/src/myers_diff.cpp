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

// One step of the shortest edit script, in reverse (backtrack) order:
// 'r' removes old_[x-1], 'i' inserts new_[y-1].
struct RawStep {
  char kind;
  std::string key;
  size_t index;
};

// Myers O(ND) greedy shortest-edit-script over stable key strings.
// Returns raw remove/insert steps (no moves yet), forward ordered.
std::vector<RawStep> myersSes(const std::vector<std::string>& oldKeys,
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

  for (int d = 0; d <= max; ++d) {
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
  }

  std::vector<RawStep> rev;
  rev.reserve(static_cast<size_t>(max));
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
  return rev;
}

bool byTo(const EditOp& a, const EditOp& b) { return a.to < b.to; }

bool byFrom(const EditOp& a, const EditOp& b) { return a.from < b.from; }

}  // namespace

EditScript myersDiff(const KeyedList& oldL, const KeyedList& newL) {
  const std::vector<RawStep> ses = myersSes(oldL.keys, newL.keys);

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
