#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace muse::net {

// Seams consumed as interfaces only (worker pool + key material stay outside
// this slice). Minimal abstract shapes so DataLayerConfig compiles and null
// seams are tolerable on the open/isOpen/schemaVersion paths. T3's stores must
// reuse these declarations (include this header) rather than redefining them.
struct IExecutor {
  virtual ~IExecutor() = default;
};

struct IKeyProvider {
  virtual bool getKey(std::vector<uint8_t>& out) = 0;
  virtual bool invalidated() const = 0;
  virtual ~IKeyProvider() = default;
};

class DataLayer {
 public:
  struct DataLayerConfig {
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<IKeyProvider> keys;
  };

  explicit DataLayer(DataLayerConfig cfg);
  ~DataLayer() = default;

  DataLayer(const DataLayer&) = delete;
  DataLayer& operator=(const DataLayer&) = delete;

  // Pure-C++ open path; no JS handle in any signature (zero-JS).
  // Fail-closed: empty or oversized paths return false without mutating
  // observable state and never throw.
  bool open(const std::string& path);
  bool isOpen() const;
  uint64_t schemaVersion() const;

 private:
  DataLayerConfig cfg_;
  bool open_ = false;
  uint64_t version_ = 0;
  std::string path_;
};

}  // namespace muse::net
