#include "net/stores.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace muse::net {
namespace {

std::string XorCrypt(const std::string& data,
                     const std::vector<uint8_t>& key) {
  std::string out = data;
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<char>(static_cast<uint8_t>(out[i]) ^
                               key[i % key.size()]);
  }
  return out;
}

}  // namespace

bool KvStore::put(std::string k, std::string v) {
  map_[std::move(k)] = std::move(v);
  return true;
}

bool KvStore::get(const std::string& k, std::string* v) const {
  if (v == nullptr) return false;
  const auto it = map_.find(k);
  if (it == map_.end()) return false;
  *v = it->second;
  return true;
}

EncryptedStore::EncryptedStore(std::shared_ptr<IKeyProvider> kp)
    : kp_(std::move(kp)) {
  // Resolve the key once at open; hot-path put/get never calls getKey.
  if (kp_ && !kp_->invalidated()) {
    std::vector<uint8_t> key;
    if (kp_->getKey(key) && !key.empty()) {
      key_ = std::move(key);
      has_key_ = true;
    }
  }
}

bool EncryptedStore::put(const std::string& k, const std::string& v) {
  if (!kp_ || kp_->invalidated() || !has_key_) return false;
  blobs_[k] = XorCrypt(v, key_);
  return true;
}

bool EncryptedStore::get(const std::string& k, std::string* v) const {
  if (v == nullptr) return false;
  if (!kp_ || kp_->invalidated() || !has_key_) return false;
  const auto it = blobs_.find(k);
  if (it == blobs_.end()) return false;
  *v = XorCrypt(it->second, key_);
  return true;
}

bool FsApi::writeFile(const std::string& p, std::string_view d) {
  if (p.empty()) return false;
  std::error_code ec;
  const auto parent = std::filesystem::path(p).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
  }
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  if (!d.empty()) {
    out.write(d.data(), static_cast<std::streamsize>(d.size()));
    if (!out) return false;
  }
  return true;
}

bool FsApi::readFile(const std::string& p, std::string* d) const {
  if (d == nullptr || p.empty()) return false;
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  if (in.bad()) return false;
  *d = ss.str();
  return true;
}

}  // namespace muse::net
