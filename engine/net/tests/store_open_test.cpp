// Batch 2 contract tests — FR-NET-006 / AC-NET-009 (TC-NET-009).
// Covers T3 (KvStore + EncryptedStore + FsApi, no-JNI hot path).
// Hermetic: C++17 + committed net headers only; no network, no env/config
// reads, no live services, no real JNI/Keystore/SQLCipher linkage. Key
// material comes from in-process fake providers; the cipher is the
// production seam's fake (coder-owned); FsApi paths live under a unique
// /tmp subdir created and removed by the test itself.
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// NOTE: reuses `muse::net::IKeyProvider` via `net/data_layer.h` — this file
// must NOT redefine it (ODR). The fakes below implement the T3-shaped
// `getKey`/`invalidated` members named by the plan's T3 Interface line; see
// T3.md Conflicts (C-T3-keyprovider-gap): T1's committed IKeyProvider is an
// empty seam and the coder must extend it with those members.
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "net/data_layer.h"
#include "net/stores.h"

namespace {

// --- Fakes (in-process only; no I/O, no threads, no env) --------------------

// Counting fake key provider behind the IKeyProvider seam. Records every
// getKey call so the hot-path test can assert zero provider calls after
// open; scripted invalidated flag drives the fail-closed cases.
class FakeKeys : public muse::net::IKeyProvider {
 public:
  FakeKeys(std::vector<uint8_t> key, bool invalid = false)
      : key_(std::move(key)), invalid_(invalid) {}

  bool getKey(std::vector<uint8_t>& out) override {
    ++getKeyCalls;
    if (invalid_) return false;
    out = key_;
    return true;
  }
  bool invalidated() const override { return invalid_; }

  void setInvalidated(bool v) { invalid_ = v; }
  void resetCalls() { getKeyCalls = 0; }

  int getKeyCalls = 0;

 private:
  std::vector<uint8_t> key_;
  bool invalid_;
};

std::shared_ptr<FakeKeys> makeKeys(bool invalid = false) {
  return std::make_shared<FakeKeys>(
      std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                           0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
      invalid);
}

// Unique scratch dir for FsApi cases; removed by the test when done.
std::string scratchDir() {
  static int seq = 0;
  return "/tmp/muse_net_store_open_test_" + std::to_string(++seq);
}

}  // namespace

// --- KvStore ----------------------------------------------------------------

// Named case: put then get round-trips the value.
static bool check_kv_put_get_round_trip() {
  muse::net::KvStore kv;
  if (!kv.put("theme", "dark")) return false;
  std::string v;
  if (!kv.get("theme", &v)) return false;
  return v == "dark";
}

// Named case: get of a missing key returns false and leaves output untouched.
static bool check_kv_missing_key_fails_closed() {
  muse::net::KvStore kv;
  std::string v = "sentinel";
  if (kv.get("no-such-key", &v)) return false;
  return v == "sentinel";
}

// Named case: put overwrites an existing key; get observes the latest value.
static bool check_kv_put_overwrites() {
  muse::net::KvStore kv;
  if (!kv.put("k", "v1")) return false;
  if (!kv.put("k", "v2")) return false;
  std::string v;
  if (!kv.get("k", &v)) return false;
  return v == "v2";
}

// Named case: empty key and empty value round-trip (boundary).
static bool check_kv_empty_key_and_value() {
  muse::net::KvStore kv;
  if (!kv.put("", "")) return false;
  std::string v = "sentinel";
  if (!kv.get("", &v)) return false;
  return v.empty();
}

// Named case: binary-opaque values (embedded NUL) round-trip exactly.
static bool check_kv_binary_value_round_trip() {
  muse::net::KvStore kv;
  const std::string blob("a\0b\0c", 5);
  if (!kv.put("blob", blob)) return false;
  std::string v;
  if (!kv.get("blob", &v)) return false;
  return v == blob;
}

// --- EncryptedStore ----------------------------------------------------------

// Named case: put then get round-trips through encryption.
static bool check_enc_put_get_round_trip() {
  muse::net::EncryptedStore enc(makeKeys());
  if (!enc.put("token", "s3cret")) return false;
  std::string v;
  if (!enc.get("token", &v)) return false;
  return v == "s3cret";
}

// Named case: get of a missing key returns false and yields no plaintext.
static bool check_enc_missing_key_fails_closed() {
  muse::net::EncryptedStore enc(makeKeys());
  std::string v = "sentinel-plaintext";
  if (enc.get("no-such-key", &v)) return false;
  return v == "sentinel-plaintext";
}

// Named case: hot path performs zero provider calls after open. Construction
// may resolve the key once; after resetting the counter, put/get must not
// touch the provider (no JNI on the hot path).
static bool check_enc_hot_path_zero_provider_calls() {
  auto keys = makeKeys();
  muse::net::EncryptedStore enc(keys);
  keys->resetCalls();
  if (!enc.put("k", "v")) return false;
  std::string v;
  if (!enc.get("k", &v)) return false;
  if (v != "v") return false;
  return keys->getKeyCalls == 0;
}

// Named case: invalidated key fails closed — get returns false and yields no
// plaintext (fail-closed, data untouched, no silent re-key per plan A3).
static bool check_enc_invalidated_key_get_fails_closed_no_plaintext() {
  auto keys = makeKeys();
  muse::net::EncryptedStore enc(keys);
  if (!enc.put("token", "s3cret")) return false;
  keys->setInvalidated(true);
  std::string v = "sentinel-plaintext";
  if (enc.get("token", &v)) return false;
  return v == "sentinel-plaintext";
}

// Named case: a store opened with an already-invalidated provider fails
// closed on get (construction-time invalidation; Q-NET-002 assumption A3).
static bool check_enc_open_invalidated_get_fails_closed() {
  muse::net::EncryptedStore enc(makeKeys(/*invalid=*/true));
  std::string v = "sentinel-plaintext";
  if (enc.get("anything", &v)) return false;
  return v == "sentinel-plaintext";
}

// Named case: distinct keys decrypt independently (one key's ciphertext does
// not leak into another key's read).
static bool check_enc_keys_isolated() {
  muse::net::EncryptedStore enc(makeKeys());
  if (!enc.put("a", "alpha")) return false;
  if (!enc.put("b", "beta")) return false;
  std::string va, vb;
  if (!enc.get("a", &va)) return false;
  if (!enc.get("b", &vb)) return false;
  return va == "alpha" && vb == "beta";
}

// --- FsApi ------------------------------------------------------------------

// Named case: writeFile then readFile round-trips content.
static bool check_fs_write_read_round_trip() {
  muse::net::FsApi fs;
  const std::string dir = scratchDir();
  const std::string path = dir + "/note.txt";
  if (!fs.writeFile(path, "hello")) return false;
  std::string d;
  if (!fs.readFile(path, &d)) return false;
  const bool ok = (d == "hello");
  std::remove(path.c_str());
  std::remove(dir.c_str());
  return ok;
}

// Named case: readFile of a missing path returns false, no crash.
static bool check_fs_missing_read_fails_closed() {
  muse::net::FsApi fs;
  std::string d = "sentinel";
  if (fs.readFile("/tmp/muse_net_store_open_test_no_such_dir_no_such_file",
                  &d))
    return false;
  return d == "sentinel";
}

// Named case: writeFile overwrites existing content.
static bool check_fs_write_overwrites() {
  muse::net::FsApi fs;
  const std::string dir = scratchDir();
  const std::string path = dir + "/note.txt";
  if (!fs.writeFile(path, "one")) return false;
  if (!fs.writeFile(path, "two")) return false;
  std::string d;
  if (!fs.readFile(path, &d)) return false;
  const bool ok = (d == "two");
  std::remove(path.c_str());
  std::remove(dir.c_str());
  return ok;
}

// Named case: empty content and binary-opaque content round-trip (boundary).
static bool check_fs_empty_and_binary_round_trip() {
  muse::net::FsApi fs;
  const std::string dir = scratchDir();
  const std::string p1 = dir + "/empty.bin";
  const std::string p2 = dir + "/blob.bin";
  const std::string blob("x\0y\0z", 5);
  if (!fs.writeFile(p1, std::string_view{})) return false;
  if (!fs.writeFile(p2, std::string_view(blob))) return false;
  std::string d1 = "sentinel", d2;
  if (!fs.readFile(p1, &d1)) return false;
  if (!fs.readFile(p2, &d2)) return false;
  const bool ok = d1.empty() && d2 == blob;
  std::remove(p1.c_str());
  std::remove(p2.c_str());
  std::remove(dir.c_str());
  return ok;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_kv_put_get_round_trip);
  RUN_CASE(check_kv_missing_key_fails_closed);
  RUN_CASE(check_kv_put_overwrites);
  RUN_CASE(check_kv_empty_key_and_value);
  RUN_CASE(check_kv_binary_value_round_trip);
  RUN_CASE(check_enc_put_get_round_trip);
  RUN_CASE(check_enc_missing_key_fails_closed);
  RUN_CASE(check_enc_hot_path_zero_provider_calls);
  RUN_CASE(check_enc_invalidated_key_get_fails_closed_no_plaintext);
  RUN_CASE(check_enc_open_invalidated_get_fails_closed);
  RUN_CASE(check_enc_keys_isolated);
  RUN_CASE(check_fs_write_read_round_trip);
  RUN_CASE(check_fs_missing_read_fails_closed);
  RUN_CASE(check_fs_write_overwrites);
  RUN_CASE(check_fs_empty_and_binary_round_trip);
  if (failures > 0) {
    std::printf("store_open_test: %d failing\n", failures);
    return 1;
  }
  std::printf("store_open_test: all passing\n");
  return 0;
}
