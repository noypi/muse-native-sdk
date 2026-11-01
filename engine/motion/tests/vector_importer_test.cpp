// Batch 1 contract test — T2 (FR-MOTION-017 / AC-MOTION-052).
// Binds ONLY to the T2 `Interface:` line from plan.md; never to implementation.
// Hermetic: C++17 + motion headers only; inline byte payloads, no network,
// no env/config reads, no filesystem, no live services, no wall clock,
// no randomness. Follows the engine/motion convention (plain main(),
// RUN_CASE macro, printf PASS/FAIL, nonzero exit on failure;
// cf. tests/asset_import_test.cpp).
//
// Expected red: this TU does not compile until the coder publishes
// `engine/motion/include/motion/vector_importer.h`
// (+ src/vector_importer.cpp) with the exact plan interface and registers
// `vector_importer_test` in `engine/motion/CMakeLists.txt`
// (`motion/vector_importer.h: No such file or directory`).
//
// ASSUMPTION (decodable-bytes proxy): the plan names no fixture bytes, so any
// non-empty payload of printable bytes is expected to import as kOk for its
// declared Lottie/Rive format, empty bytes import as kUnsupported, and
// payloads containing non-text bytes import as kDecodeFailed — mirroring the
// `motion/asset.h` decoding proxy. Coder-owned decoders may legitimately
// reject these placeholders; ONLY the byte-content expectations reconcile in
// that case, never the error-shape / no-throw / import-time expectations.
// ASSUMPTION (GraphNode shape): the plan names `motion::GraphNode`; the
// existing graph seam exposes `motion::Node`. These tests bind to the plan
// name `motion::GraphNode` and only require it to be default-constructible,
// copyable, and returned by value; if the coder aliases it to `motion::Node`
// (per the plan Detail "reuse `graph.h` shapes"), only the spelling
// reconciles, never the behavior.
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "motion/vector_importer.h"

namespace {

std::vector<uint8_t> bytesOf(const char* s) {
  std::string t(s);
  return std::vector<uint8_t>(t.begin(), t.end());
}

// --- T2 public interface (plan verbatim) ------------------------------------
// `motion::GraphNode VectorImporter::import(AssetFormat f,
// const std::vector<uint8_t>& bytes, ImportError* err)` — converts
// Lottie/Rive bytes into engine graph nodes at import time (no stepping);
// error shape: `ImportError { kOk, kUnsupported, kDecodeFailed }` via
// out-param, never throws.

bool check_import_error_enumerator_spelling() {
  ImportError ok = ImportError::kOk;
  ImportError unsupported = ImportError::kUnsupported;
  ImportError failed = ImportError::kDecodeFailed;
  (void)ok;
  (void)unsupported;
  (void)failed;
  return true;
}

bool check_signature_returns_graph_node_by_value() {
  static_assert(
      std::is_same<decltype(std::declval<VectorImporter&>().import(
                               AssetFormat::kLottie,
                               std::declval<const std::vector<uint8_t>&>(),
                               static_cast<ImportError*>(nullptr))),
                   motion::GraphNode>::value,
      "VectorImporter::import must return motion::GraphNode by value");
  static_assert(std::is_default_constructible<motion::GraphNode>::value,
                "motion::GraphNode must be default-constructible");
  return true;
}

bool check_import_lottie_returns_ok() {
  VectorImporter importer;
  ImportError err = ImportError::kDecodeFailed;
  try {
    motion::GraphNode node =
        importer.import(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}"), &err);
    (void)node;
  } catch (...) {
    return false;
  }
  return err == ImportError::kOk;
}

bool check_import_rive_returns_ok() {
  VectorImporter importer;
  ImportError err = ImportError::kDecodeFailed;
  try {
    motion::GraphNode node =
        importer.import(AssetFormat::kRive, bytesOf("RIVE-DATA"), &err);
    (void)node;
  } catch (...) {
    return false;
  }
  return err == ImportError::kOk;
}

bool check_import_empty_bytes_returns_unsupported() {
  VectorImporter importer;
  ImportError err = ImportError::kOk;
  try {
    motion::GraphNode node =
        importer.import(AssetFormat::kLottie, std::vector<uint8_t>{}, &err);
    (void)node;
  } catch (...) {
    return false;
  }
  return err == ImportError::kUnsupported;
}

bool check_import_nontext_bytes_returns_decode_failed() {
  VectorImporter importer;
  ImportError err = ImportError::kOk;
  try {
    std::vector<uint8_t> raw{0x00, 0x01, 0xFF, 0xFE};
    motion::GraphNode node =
        importer.import(AssetFormat::kRive, raw, &err);
    (void)node;
  } catch (...) {
    return false;
  }
  return err == ImportError::kDecodeFailed;
}

bool check_import_never_throws_lottie() {
  VectorImporter importer;
  ImportError err = ImportError::kOk;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (void)importer.import(AssetFormat::kLottie, bytesOf("{\"v\":\"x\"}"), &err);
  } catch (...) {
    return false;
  }
  return true;
#else
  (void)importer.import(AssetFormat::kLottie, bytesOf("{\"v\":\"x\"}"), &err);
  return true;
#endif
}

bool check_import_never_throws_empty() {
  VectorImporter importer;
  ImportError err = ImportError::kOk;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (void)importer.import(AssetFormat::kRive, std::vector<uint8_t>{}, &err);
  } catch (...) {
    return false;
  }
  return true;
#else
  (void)importer.import(AssetFormat::kRive, std::vector<uint8_t>{}, &err);
  return true;
#endif
}

bool check_import_is_per_instance() {
  // Importer holds per-instance state: two importers importing the same
  // payload agree on the outcome (no shared mutable import state).
  VectorImporter a;
  VectorImporter b;
  ImportError errA = ImportError::kDecodeFailed;
  ImportError errB = ImportError::kDecodeFailed;
  try {
    (void)a.import(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}"), &errA);
    (void)b.import(AssetFormat::kLottie, bytesOf("{\"v\":\"lottie\"}"), &errB);
  } catch (...) {
    return false;
  }
  return errA == ImportError::kOk && errB == ImportError::kOk;
}

}  // namespace

// --- harness (convention copied from tests/asset_import_test.cpp) -------------

static bool check_harness_entry_reached() { return true; }

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_import_error_enumerator_spelling);
  RUN_CASE(check_signature_returns_graph_node_by_value);
  RUN_CASE(check_import_lottie_returns_ok);
  RUN_CASE(check_import_rive_returns_ok);
  RUN_CASE(check_import_empty_bytes_returns_unsupported);
  RUN_CASE(check_import_nontext_bytes_returns_decode_failed);
  RUN_CASE(check_import_never_throws_lottie);
  RUN_CASE(check_import_never_throws_empty);
  RUN_CASE(check_import_is_per_instance);
  if (failures > 0) {
    std::printf("vector_importer_test: %d failing\n", failures);
    return 1;
  }
  std::printf("vector_importer_test: all passing\n");
  return 0;
}
