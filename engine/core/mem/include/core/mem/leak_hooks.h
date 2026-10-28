#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#if !defined(CORE_MEM_LEAK_HOOKS)
#define CORE_MEM_LEAK_HOOKS 1
#endif

namespace core {
namespace mem {

#if CORE_MEM_LEAK_HOOKS

class LeakHooks {
 public:
  struct Entry {
    void* p;
    uint32_t subsys;
    uint32_t phase;
  };

  LeakHooks() = default;

  void record(void* p, uint32_t subsys, uint32_t phase) noexcept;
  void release(void* p) noexcept;
  size_t survivors(Entry* out, size_t n) const noexcept;

 private:
  std::vector<Entry> entries_;
};

#else

class LeakHooks {
 public:
  struct Entry {
    void* p;
    uint32_t subsys;
    uint32_t phase;
  };

  void record(void* /*p*/, uint32_t /*subsys*/, uint32_t /*phase*/) noexcept {}
  void release(void* /*p*/) noexcept {}
  size_t survivors(Entry* /*out*/, size_t /*n*/) const noexcept { return 0; }
};

#endif

}  // namespace mem
}  // namespace core
