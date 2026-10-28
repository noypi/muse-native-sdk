#include "navigation/result_channel.h"

void ResultHost::registerDestination(
    const std::string& routeId, ParamType type,
    std::function<void(const TypedParam&)> callback) {
  Entry e;
  e.type = type;
  e.callback = std::move(callback);
  destinations_[routeId] = std::move(e);
}

ResultDelivery deliverResult(ResultHost& h, const std::string& toRoute,
                             const TypedParam& value) {
  auto it = h.destinations_.find(toRoute);
  if (it == h.destinations_.end()) {
    return ResultDelivery{false, ResultError::TARGET_GONE};
  }
  if (it->second.type != value.type) {
    return ResultDelivery{false, ResultError::TYPE_MISMATCH};
  }
  if (it->second.callback) {
    it->second.callback(value);
  }
  return ResultDelivery{true, ResultError::NONE};
}

void onPopped(ResultHost& h, const std::string& routeId) {
  h.destinations_.erase(routeId);
}
