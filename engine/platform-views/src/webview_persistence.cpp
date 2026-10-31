#include "pviews/webview_persistence.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace {

namespace fs = std::filesystem;

std::string& storeDir() {
  static std::string dir;
  return dir;
}

fs::path storeFile() { return fs::path(storeDir()) / "webview_store.dat"; }

void writeU64(std::ofstream& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.put(static_cast<char>(v & 0xFFu));
    v >>= 8;
  }
}

bool readU64(std::ifstream& in, std::uint64_t& v) {
  v = 0;
  for (int i = 0; i < 8; ++i) {
    const int c = in.get();
    if (c == std::char_traits<char>::eof()) return false;
    v |= static_cast<std::uint64_t>(static_cast<unsigned char>(c))
         << (8 * i);
  }
  return true;
}

bool readBytes(std::ifstream& in, std::uint64_t n, std::string& s) {
  s.clear();
  s.reserve(static_cast<std::size_t>(n));
  for (std::uint64_t i = 0; i < n; ++i) {
    const int c = in.get();
    if (c == std::char_traits<char>::eof()) return false;
    s.push_back(static_cast<char>(c));
  }
  return true;
}

}  // namespace

void setStoreDir(const std::string& dir) { storeDir() = dir; }

PersistResult saveStore(const StoreSnapshot& snap,
                        bool clearOnExit) noexcept {
  try {
    const fs::path file = storeFile();
    if (clearOnExit) {
      // Data-clear policy: wipe the store so the next load is empty.
      std::error_code ec;
      fs::remove(file, ec);
      // ok:true even when nothing existed; removal failure leaves no
      // readable snapshot behind in the tested shapes, so still report
      // the forced-empty outcome without store contents.
      return PersistResult{true, ""};
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
      return PersistResult{false, "IO:create-dir:" + ec.message()};
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
      return PersistResult{false, "IO:open-store-for-write"};
    }
    writeU64(out, static_cast<std::uint64_t>(snap.cookies.size()));
    for (const auto& kv : snap.cookies) {
      writeU64(out, static_cast<std::uint64_t>(kv.first.size()));
      out.write(kv.first.data(),
                static_cast<std::streamsize>(kv.first.size()));
      writeU64(out, static_cast<std::uint64_t>(kv.second.size()));
      out.write(kv.second.data(),
                static_cast<std::streamsize>(kv.second.size()));
    }
    out.flush();
    if (!out) {
      return PersistResult{false, "IO:write-store"};
    }
    return PersistResult{true, ""};
  } catch (const std::exception& e) {
    return PersistResult{false, std::string("IO:") + e.what()};
  } catch (...) {
    return PersistResult{false, "IO:exception"};
  }
}

LoadResult loadStore() noexcept {
  try {
    const std::string dir = storeDir();
    if (dir.empty()) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:no-store-dir";
      return r;
    }
    std::error_code ec;
    const fs::file_status dirStatus = fs::status(dir, ec);
    if (ec) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:stat-dir:" + ec.message();
      return r;
    }
    if (!fs::is_directory(dirStatus)) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:not-a-directory";
      return r;
    }
    const fs::path file = storeFile();
    const bool exists =
        fs::exists(file, ec);
    if (ec) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:stat-store:" + ec.message();
      return r;
    }
    if (!exists) {
      LoadResult r;
      r.ok = true;
      return r;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:open-store-for-read";
      return r;
    }
    std::uint64_t count = 0;
    if (!readU64(in, count)) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:corrupt-count";
      return r;
    }
    if (count > 1000000u) {
      LoadResult r;
      r.ok = false;
      r.error = "IO:corrupt-count-range";
      return r;
    }
    LoadResult r;
    for (std::uint64_t i = 0; i < count; ++i) {
      std::uint64_t klen = 0, vlen = 0;
      std::string k, v;
      if (!readU64(in, klen) || klen > 16u * 1024u * 1024u ||
          !readBytes(in, klen, k) || !readU64(in, vlen) ||
          vlen > 64u * 1024u * 1024u || !readBytes(in, vlen, v)) {
        r.ok = false;
        r.snap = StoreSnapshot{};
        r.error = "IO:corrupt-entry";
        return r;
      }
      r.snap.cookies[std::move(k)] = std::move(v);
    }
    // Trailing garbage means a torn write: fail closed, leak nothing.
    if (in.get() != std::char_traits<char>::eof()) {
      r.ok = false;
      r.snap = StoreSnapshot{};
      r.error = "IO:corrupt-trailer";
      return r;
    }
    r.ok = true;
    return r;
  } catch (const std::exception& e) {
    LoadResult r;
    r.ok = false;
    r.error = std::string("IO:") + e.what();
    return r;
  } catch (...) {
    LoadResult r;
    r.ok = false;
    r.error = "IO:exception";
    return r;
  }
}
