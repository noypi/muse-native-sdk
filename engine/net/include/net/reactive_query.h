#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace muse::net {

// Injected frame clock seam: test drives frame ids explicitly; no vsync.
struct IFrameClock {
  virtual uint64_t frameId() const = 0;
  virtual ~IFrameClock() = default;
};

// Row-change notifier with per-frame coalescing. notifyChanged delivers the
// current frame id synchronously to every subscriber, at most once per frame.
class ReactiveQuery {
 public:
  using Cb = std::function<void(uint64_t frame)>;

  explicit ReactiveQuery(std::shared_ptr<IFrameClock> c);

  uint64_t notifyChanged();
  void subscribe(Cb cb);

 private:
  std::shared_ptr<IFrameClock> clock_;
  std::vector<Cb> subs_;
  bool has_delivered_ = false;
  uint64_t last_frame_ = 0;
};

}  // namespace muse::net
