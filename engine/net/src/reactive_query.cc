#include "net/reactive_query.h"

namespace muse::net {

ReactiveQuery::ReactiveQuery(std::shared_ptr<IFrameClock> c)
    : clock_(std::move(c)) {}

uint64_t ReactiveQuery::notifyChanged() {
  const uint64_t frame = clock_->frameId();
  if (has_delivered_ && last_frame_ == frame) {
    return frame;
  }
  has_delivered_ = true;
  last_frame_ = frame;
  for (const auto& cb : subs_) {
    cb(frame);
  }
  return frame;
}

void ReactiveQuery::subscribe(Cb cb) { subs_.push_back(std::move(cb)); }

}  // namespace muse::net
