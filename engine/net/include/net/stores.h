#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "net/data_layer.h"

namespace muse::net {

// In-memory MMAP-KV seam fake: plain map behind the KvStore surface.
class KvStore {
 public:
  KvStore() = default;

  bool put(std::string k, std::string v);
  bool get(const std::string& k, std::string* v) const;

 private:
  mutable std::unordered_map<std::string, std::string> map_;
};

// Encrypted store: key bytes resolved once at construction (open) and cached;
// hot-path put/get performs no getKey calls. Any invalidated state fails
// closed without mutating the caller's output. Cipher is a reversible XOR
// seam fake over the cached key (observable behavior only).
class EncryptedStore {
 public:
  explicit EncryptedStore(std::shared_ptr<IKeyProvider> kp);

  bool put(const std::string& k, const std::string& v);
  bool get(const std::string& k, std::string* v) const;

 private:
  std::shared_ptr<IKeyProvider> kp_;
  std::vector<uint8_t> key_;
  bool has_key_ = false;
  mutable std::unordered_map<std::string, std::string> blobs_;
};

// Filesystem API over caller-supplied paths.
class FsApi {
 public:
  FsApi() = default;

  bool writeFile(const std::string& p, std::string_view d);
  bool readFile(const std::string& p, std::string* d) const;
};

}  // namespace muse::net
