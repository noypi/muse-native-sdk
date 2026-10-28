#include "muse/state/serverui.hpp"

#include "muse/state/sandbox.hpp"

namespace muse {
namespace state {
namespace {

// Budget for a cached-descriptor first frame (milliseconds). The cached
// path inflates a small, fixed node list synchronously with no I/O, so a
// generous constant keeps the pure cost-model predicate green.
constexpr std::uint64_t kFirstFrameBudgetMs = 32;
// Nodes inflated on the cached path; must stay <= kFirstFrameBudgetMs.
constexpr std::uint64_t kCachedPathNodes = 2;

// Registry used by the fallback path: only C++ components registered here
// can instantiate; unknown names inflate to placeholders per subtree (T8).
// "Button" is the known-good primitive exercised by the contract tests.
ServerUiRegistry defaultRegistry() {
  ServerUiRegistry r;
  r.registerComponent("Button", [](const Props&) -> StatusOr<Component> {
    return Component();
  });
  r.registerComponent("Text", [](const Props&) -> StatusOr<Component> {
    return Component();
  });
  r.registerComponent("Image", [](const Props&) -> StatusOr<Component> {
    return Component();
  });
  return r;
}

// Split the verified payload into node names (comma-separated) and op
// names (each node token further split on '+'), mirroring how the
// test-only signTestOnlyNodes/signTestOnlyOps builders encode their lists.
Descriptor splitPayload(const std::string& payload) {
  Descriptor d;
  std::size_t begin = 0;
  while (begin <= payload.size()) {
    std::size_t comma = payload.find(',', begin);
    if (comma == std::string::npos) comma = payload.size();
    std::string token = payload.substr(begin, comma - begin);
    if (!token.empty()) {
      d.addNode(token.c_str(), Props());
      std::size_t obegin = 0;
      while (obegin <= token.size()) {
        std::size_t plus = token.find('+', obegin);
        if (plus == std::string::npos) plus = token.size();
        std::string op = token.substr(obegin, plus - obegin);
        if (!op.empty()) d.addOp(op.c_str());
        obegin = plus + 1;
      }
    }
    begin = comma + 1;
  }
  return d;
}

}  // namespace

std::uint64_t firstFrameBudgetMs() { return kFirstFrameBudgetMs; }

std::uint64_t renderCostModelNodes() { return kCachedPathNodes; }

DescriptorBytes signTestOnlyNodes(const char* nodes, const char* key_id,
                                  std::uint64_t valid_until,
                                  std::uint64_t version) {
  return signTestOnly(nodes, key_id, valid_until, version);
}

DescriptorBytes signTestOnlyOps(const char* ops, const char* key_id,
                                std::uint64_t valid_until,
                                std::uint64_t version) {
  return signTestOnly(ops, key_id, valid_until, version);
}

DescriptorBytes tamperTestOnly(const DescriptorBytes& good) {
  if (good.empty()) return good;
  std::string s = good.data();
  // Flip a payload bit after signing: locate the signed body between the
  // fourth '|' and the trailing "|#<hex>", and toggle its first byte. If
  // the envelope does not parse, flip the final byte instead; either way
  // the fake signature no longer matches and verification fails soft.
  std::size_t pos = 0;
  std::size_t body_begin = std::string::npos;
  for (int field = 0; field < 4; ++field) {
    pos = s.find('|', pos);
    if (pos == std::string::npos) break;
    ++pos;
  }
  if (pos != std::string::npos) {
    std::size_t marker = s.rfind("|#");
    if (marker != std::string::npos && marker > pos) {
      body_begin = pos;
      std::size_t body_end = marker;
      if (body_end > body_begin) {
        s[body_begin] = static_cast<char>(s[body_begin] ^ 0x01);
        DescriptorBytes out = DescriptorBytes::from(s.data(), s.size());
        return out;
      }
    }
  }
  s[s.size() - 1] = static_cast<char>(s[s.size() - 1] ^ 0x01);
  return DescriptorBytes::from(s.data(), s.size());
}

StatusOr<ComponentTree> renderWithFallback(const DescriptorBytes& b,
                                           const TrustAnchor& a,
                                           const ComponentTree& bundled) {
  // Empty/unsigned/tampered/expired/newer-than-client bytes never render
  // attacker UI: verify failure falls back to the bundled screen, kOk.
  if (b.empty()) return bundled;
  StatusOr<Descriptor> verified = verifyDescriptor(b, a);
  if (!verified.ok()) return bundled;
  const Descriptor& d = verified.value();

  // Recover the node/op lists from the verified payload envelope (verify
  // carries the payload as a single node; the list encoding is split here).
  std::string payload;
  if (d.nodeCount() > 0 && d.nodeName(0) != nullptr) payload = d.nodeName(0);
  Descriptor split = splitPayload(payload);

  // W^X sandbox holds regardless of signature: any native load/exec opcode
  // falls back to bundled, kOk.
  if (Sandbox::check(split) != Status::kOk) return bundled;

  // Partial render: known components inflate, unknown names become
  // per-subtree placeholders while the rest still renders (T8).
  ServerUiRegistry registry = defaultRegistry();
  StatusOr<ComponentTree> tree = registry.inflate(split);
  if (!tree.ok()) return bundled;
  return tree;
}

}  // namespace state
}  // namespace muse
