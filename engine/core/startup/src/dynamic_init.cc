// T2 dynamic-initializer scan (NFR-CORE-004, AC-CORE-031).
//
// Walks `object_dir` recursively and inspects each startup-path object
// file's bytes (extension gate: .o/.obj/.a/.so, intentional — mirrors
// tools/check_dynamic_init.py so both surfaces share the same scope)
// for ELF dynamic-initializer markers (init-array/ctors sections and the
// compiler-generated global-init symbols). One
// `DYNAMIC_INIT:<file>:<symbol>` line per offending file goes to stdout.
// Empty directories scan zero objects and return 0; null paths,
// missing/unreadable directories, and I/O failures return nonzero without
// throwing (fail-closed per the FR-PKG-005 shape).
//
// NOTE: marker strings are assembled at runtime from split fragments so
// this translation unit's own object file never contains a contiguous
// marker byte sequence (a naive substring scan of the build tree must
// stay clean on the checker itself).

#include "core/startup/dynamic_init.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace muse {
namespace startup {
namespace {

// Cap per-file read so an unexpectedly large artifact cannot exhaust
// memory. An over-cap file fails closed (return 2, never clean) because
// a truncated tail could hide a violation; the scanner must never pass
// a file it could not fully read.
constexpr std::size_t kMaxScanBytes = 8u * 1024u * 1024u;

// Marker patterns built from fragments at runtime (see file NOTE).
std::vector<std::string> markerPatterns() {
  std::vector<std::string> out;
  out.emplace_back(std::string(".init") + "_array");
  out.emplace_back(std::string(".preinit") + "_array");
  out.emplace_back(std::string(".ct") + "ors");
  out.emplace_back(std::string("_GLOBAL__") + "sub_I_");
  out.emplace_back(std::string("__static_initialization") +
                   "_and_destruction_");
  out.emplace_back(std::string("__do_global_") + "dtors_aux");
  return out;
}

// True when `path` is a regular file. A file is scanned only when it is
// both a regular file AND a startup-path object (see `isObjectFile`
// below); non-object sidecars are out of scope by design.
bool isRegularFile(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  bool ok = std::filesystem::is_regular_file(path, ec);
  return !ec && ok;
}

// Scan scope is intentionally gated to startup-path object extensions
// (.o/.obj/.a/.so, ASCII case-insensitive), matching
// tools/check_dynamic_init.py::_iter_object_files, so both surfaces
// cannot drift apart. Intentional extension gate (startup-path objects
// only): mirrors the
// `.o`/`.obj`/`.a`/`.so` set in tools/check_dynamic_init.py. Compared
// ASCII case-insensitively, like the Python check.
bool isObjectFile(const std::filesystem::path& path) noexcept {
  std::string ext = path.extension().string();
  for (char& c : ext) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c + ('a' - 'A'));
    }
  }
  return ext == ".o" || ext == ".obj" || ext == ".a" || ext == ".so";
}

}  // namespace

int check_dynamic_initializers(const char* object_dir) noexcept {
  if (object_dir == nullptr) {
    return 2;
  }
  try {
    std::error_code ec;
    const std::filesystem::path root(object_dir);
    const std::filesystem::file_status st =
        std::filesystem::status(root, ec);
    if (ec || !std::filesystem::is_directory(st)) {
      return 2;
    }

    const std::vector<std::string> markers = markerPatterns();
    int violations = 0;

    std::filesystem::recursive_directory_iterator it(root, ec);
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
      return 2;
    }
    for (; it != end; it.increment(ec)) {
      if (ec) {
        return 2;
      }
      if (!isRegularFile(it->path())) {
        continue;
      }
      if (!isObjectFile(it->path())) {
        continue;
      }
      std::ifstream file(it->path(), std::ios::binary);
      if (!file) {
        return 2;
      }
      std::string contents;
      contents.reserve(65536);
      char chunk[65536];
      std::size_t total = 0;
      while (file) {
        file.read(chunk, sizeof(chunk));
        const std::streamsize got = file.gcount();
        if (got <= 0) {
          break;
        }
        if (total + static_cast<std::size_t>(got) > kMaxScanBytes) {
          std::fprintf(stderr, "DYNAMIC_INIT_TRUNCATED:%s:over-cap\n",
                       it->path().string().c_str());
          return 2;
        }
        contents.append(chunk, static_cast<std::size_t>(got));
        total += static_cast<std::size_t>(got);
      }
      if (file.bad()) {
        return 2;
      }
      for (const std::string& marker : markers) {
        if (contents.find(marker) != std::string::npos) {
          std::string name = it->path().filename().string();
          if (name.empty()) {
            name = it->path().string();
          }
          std::printf("DYNAMIC_INIT:%s:%s\n", name.c_str(),
                      marker.c_str());
          ++violations;
          break;
        }
      }
    }
    return violations > 0 ? 1 : 0;
  } catch (...) {
    return 2;
  }
}

}  // namespace startup
}  // namespace muse
