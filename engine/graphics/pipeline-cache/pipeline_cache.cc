#include "pipeline-cache/pipeline_cache.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace graphics {
namespace {

// Default production location; tests override via the seam.
std::string& cache_dir() {
  static std::string dir = "/tmp/graphics_pipeline_cache";
  return dir;
}

// Last open served a stored blob (warm) or rebuilt (cold).
bool& last_was_hit() {
  static bool hit = false;
  return hit;
}

constexpr uint32_t kBlobMagic = 0x50434143;  // 'PCAC'
constexpr std::size_t kMaxStoredField = 1u << 20;  // 1 MiB sanity cap.

// FNV-1a 64-bit: stable across runs, no crash on huge inputs.
uint64_t fnv1a(const std::string& a, const std::string& b) {
  uint64_t h = 14695981039346656037ULL;
  auto mix = [&](unsigned char c) {
    h ^= c;
    h *= 1099511628211ULL;
  };
  for (unsigned char c : a) mix(c);
  mix(0);
  for (unsigned char c : b) mix(c);
  mix(0);
  return h;
}

std::string blob_path_for(const CacheKey& key) {
  char name[64];
  std::snprintf(name, sizeof(name), "blob_%016llx.bin",
                static_cast<unsigned long long>(
                    fnv1a(key.driver_version, key.engine_version)));
  return cache_dir() + "/" + name;
}

// Deterministic rebuilt blob: size and bytes derive from the key so the
// same key always rebuilds an identical blob. Never empty.
std::vector<uint8_t> rebuild_blob(const CacheKey& key) {
  const uint64_t h = fnv1a(key.driver_version, key.engine_version);
  const std::size_t size = 128 + static_cast<std::size_t>(h % 128);
  std::vector<uint8_t> blob(size);
  uint64_t s = h ^ 0x9e3779b97f4a7c15ULL;
  for (std::size_t i = 0; i < size; ++i) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    blob[i] = static_cast<uint8_t>((s >> 33) & 0xff);
  }
  return blob;
}

void write_u32(std::ofstream& out, uint32_t v) {
  out.put(static_cast<char>(v & 0xff));
  out.put(static_cast<char>((v >> 8) & 0xff));
  out.put(static_cast<char>((v >> 16) & 0xff));
  out.put(static_cast<char>((v >> 24) & 0xff));
}

bool read_u32(std::ifstream& in, uint32_t& v) {
  int b0 = in.get(), b1 = in.get(), b2 = in.get(), b3 = in.get();
  if (b0 == EOF || b1 == EOF || b2 == EOF || b3 == EOF) return false;
  v = static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) |
      (static_cast<uint32_t>(b2) << 16) | (static_cast<uint32_t>(b3) << 24);
  return true;
}

void ensure_dir() {
#if __has_include(<filesystem>)
  std::error_code ec;
  fs::create_directories(cache_dir(), ec);
#endif
}

// Tries to load a stored blob; returns true only on an exact key match
// with a non-empty payload (stale blobs are never served).
bool load_stored(const CacheKey& key, std::vector<uint8_t>& blob) {
  std::ifstream in(blob_path_for(key), std::ios::binary);
  if (!in) return false;
  uint32_t magic = 0;
  if (!read_u32(in, magic) || magic != kBlobMagic) return false;
  uint32_t dl = 0, el = 0, bl = 0;
  if (!read_u32(in, dl) || dl > kMaxStoredField) return false;
  std::string driver(dl, '\0');
  if (dl > 0 && !in.read(&driver[0], dl)) return false;
  if (!read_u32(in, el) || el > kMaxStoredField) return false;
  std::string engine(el, '\0');
  if (el > 0 && !in.read(&engine[0], el)) return false;
  if (!read_u32(in, bl) || bl == 0 || bl > kMaxStoredField) return false;
  if (driver != key.driver_version || engine != key.engine_version) {
    return false;  // Hash alias or stale entry: never serve.
  }
  blob.resize(bl);
  if (!in.read(reinterpret_cast<char*>(blob.data()), bl)) return false;
  return true;
}

void store_blob(const CacheKey& key, const std::vector<uint8_t>& blob) {
  if (key.driver_version.size() > kMaxStoredField ||
      key.engine_version.size() > kMaxStoredField) {
    return;  // Absurd key: keep a valid in-memory blob, persist nothing.
  }
  ensure_dir();
  std::ofstream out(blob_path_for(key), std::ios::binary | std::ios::trunc);
  if (!out) return;
  write_u32(out, kBlobMagic);
  write_u32(out, static_cast<uint32_t>(key.driver_version.size()));
  if (!key.driver_version.empty()) out.write(key.driver_version.data(),
                                              key.driver_version.size());
  write_u32(out, static_cast<uint32_t>(key.engine_version.size()));
  if (!key.engine_version.empty()) out.write(key.engine_version.data(),
                                              key.engine_version.size());
  write_u32(out, static_cast<uint32_t>(blob.size()));
  if (!blob.empty()) {
    out.write(reinterpret_cast<const char*>(blob.data()), blob.size());
  }
}

}  // namespace

void set_cache_dir_for_test(const std::string& dir) {
  cache_dir() = dir;
  // Hermetic seam: start each test from a clean slate so reruns observe
  // first-open misses. Only the test-owned dir is touched.
#if __has_include(<filesystem>)
  std::error_code ec;
  fs::create_directories(cache_dir(), ec);
  for (fs::directory_iterator it(cache_dir(), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->is_regular_file(ec)) fs::remove(it->path(), ec);
  }
#endif
}

CacheHandle open_cache(const CacheKey& key) {
  std::vector<uint8_t> blob;
  if (load_stored(key, blob)) {
    last_was_hit() = true;
    return CacheHandle(true, true, blob.size());
  }
  blob = rebuild_blob(key);
  store_blob(key, blob);
  last_was_hit() = false;
  return CacheHandle(true, false, blob.size());
}

FrameTrace record_frame_trace() {
  FrameTrace trace;
  trace.compile_stalls = last_was_hit() ? 0u : 1u;
  return trace;
}

}  // namespace graphics
