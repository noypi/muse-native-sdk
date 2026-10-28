# Sanitizer + hardening build matrix (NFR-CORE-005, AC-CORE-006/027).
#
# Consumed as `include(Sanitizers)` + `muse_apply_sanitizers(<target>)`;
# `MUSE_SANITIZER` selects the sanitizer (`none|asan|hwasan|mte`,
# default `none`). Flags are applied per-target only — this module never
# mutates global CMAKE_CXX_FLAGS, so non-engine targets are unaffected.
#
# GWP-ASan profile builds: `MUSE_ENABLE_GWP_ASAN` (default OFF) opts a
# target into guarded allocation sampling for profile builds. Sampling
# default: Sample_Malloc_Using_GWP_ASan on ~1/1000 profile-build
# allocations (tune via allocator flags at runtime); debug builds keep
# it OFF so ASan/HWASan remain the deterministic tool.

set(MUSE_SANITIZER "none" CACHE STRING "Sanitizer selection (none|asan|hwasan|mte)")
set_property(CACHE MUSE_SANITIZER PROPERTY STRINGS none asan hwasan mte)

# GWP-ASan sampling toggle for profile builds (AC-CORE-027). OFF by
# default; enable on profile-build targets that want sampled guard pages
# instead of full ASan instrumentation.
option(MUSE_ENABLE_GWP_ASAN "Enable GWP-ASan sampled allocation guards (profile builds)" OFF)

# Applies the hardening baseline plus the selected sanitizer to <target>.
# HWASan/MTE/CFI are gated on compiler+platform support and fail with a
# documented diagnostic on unsupported combos; the baseline
# (-fstack-protector-strong, -D_FORTIFY_SOURCE=3) always applies.
function(muse_apply_sanitizers target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "muse_apply_sanitizers: target '${target}' does not exist")
  endif()

  # Baseline hardening on every configured target.
  target_compile_options(${target} PRIVATE -fstack-protector-strong)
  target_link_options(${target} PRIVATE -fstack-protector-strong)
  target_compile_definitions(${target} PRIVATE _FORTIFY_SOURCE=3)

  if(MUSE_SANITIZER STREQUAL "none")
    return()
  endif()

  if(MUSE_SANITIZER STREQUAL "asan")
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address)
    return()
  endif()

  if(MUSE_SANITIZER STREQUAL "hwasan")
    # HWASan requires Clang on AArch64 Linux/Android.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "MUSE_SANITIZER=hwasan requires Clang (got ${CMAKE_CXX_COMPILER_ID}); use -DMUSE_SANITIZER=asan on GCC hosts")
    endif()
    if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR ANDROID_ABI MATCHES "arm64"))
      message(FATAL_ERROR "MUSE_SANITIZER=hwasan requires an AArch64 target (got processor '${CMAKE_SYSTEM_PROCESSOR}'); use -DMUSE_SANITIZER=asan on x86_64 hosts")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=hwaddress -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=hwaddress)
    return()
  endif()

  if(MUSE_SANITIZER STREQUAL "mte")
    # MTE requires Clang targeting Armv8.5+ with the memtag extension.
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "MUSE_SANITIZER=mte requires Clang (got ${CMAKE_CXX_COMPILER_ID}); MTE is only supported on Clang AArch64 toolchains")
    endif()
    if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR ANDROID_ABI MATCHES "arm64"))
      message(FATAL_ERROR "MUSE_SANITIZER=mte requires an AArch64 target (got processor '${CMAKE_SYSTEM_PROCESSOR}'); MTE needs Armv8.5-A+memtag hardware (see engine/core/safety/MTE_QA_PROCEDURE.md)")
    endif()
    target_compile_options(${target} PRIVATE -march=armv8.5-a+memtag -fsanitize=memtag -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -march=armv8.5-a+memtag -fsanitize=memtag)
    return()
  endif()

  message(FATAL_ERROR "MUSE_SANITIZER must be one of none|asan|hwasan|mte (got '${MUSE_SANITIZER}')")
endfunction()

# CFI helper: call `muse_apply_cfi(<target>)` to opt in where allowed.
# CFI requires Clang + LTO and whole-program visibility; it degrades to
# a warning (never a silent no-op) when the toolchain cannot honor it,
# and it is intentionally separate so the plugin ABI surface can
# consume it later without coupling to the sanitizer selection above.
function(muse_apply_cfi target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "muse_apply_cfi: target '${target}' does not exist")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING "muse_apply_cfi: CFI requires Clang (got ${CMAKE_CXX_COMPILER_ID}); skipping -fsanitize=cfi for target '${target}'")
    return()
  endif()
  target_compile_options(${target} PRIVATE -flto -fsanitize=cfi -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -flto -fsanitize=cfi)
endfunction()
