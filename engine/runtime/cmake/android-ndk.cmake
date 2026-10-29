# T1 android-ndk configure include (NFR-RUNTIME-001 / AC-RUNTIME-025).
# Pinned NDK r27 dual-ABI configure for engine/runtime.
# Consumes the version-catalog pin: ndkVersion = "27.2.12479018"
# (gradle/libs.versions.toml, NFR-BUILD-016). The pin test
# (toolchain/ndk/verifyNdkPin.test.js) requires this file to reference the
# pinned version string below; do not bump it here — bump the catalog first.
#
# Usage (Android):
#   cmake -S engine/runtime -B engine/runtime/build-android-<abi> \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
#     -C engine/runtime/cmake/android-ndk.cmake \
#     -DANDROID_ABI=<abi>
# where <abi> is exactly one of: arm64-v8a, armeabi-v7a, x86_64, x86.
#
# Hermetic dual-ABI gate usage (T3, no NDK required):
#   cmake -S engine/runtime -B engine/runtime/build \
#     -C engine/runtime/cmake/android-ndk.cmake
#   cmake --build engine/runtime/build
#   ctest -R android_build_gate --test-dir engine/runtime/build
# The preload arranges a post-project self-inclusion (CMAKE_PROJECT_INCLUDE)
# that defines both <root>-<abi>/libRuntime.so outputs plus the
# android_build_gate ctest entry, all built with the host toolchain.
#
# ndkVersion 27.2.12479018

if(NOT DEFINED PROJECT_NAME)
  # ---- -C preload context (runs before project(); cache sets only) ----
  set(ANDROID_PLATFORM android-29 CACHE STRING "Android API level for engine/runtime")

  # Pin the NDK revision consumed by the Android toolchain file. The value must
  # stay in sync with ndkVersion "27.2.12479018" in gradle/libs.versions.toml.
  set(MUSE_NDK_VERSION "27.2.12479018" CACHE STRING "Pinned NDK revision (r27)")

  if(DEFINED ANDROID_ABI)
    # Real Android configure: fail fast on anything but the four supported
    # ABIs; matching is byte-exact.
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a" AND NOT ANDROID_ABI STREQUAL "armeabi-v7a" AND NOT ANDROID_ABI STREQUAL "x86_64" AND NOT ANDROID_ABI STREQUAL "x86")
      message(FATAL_ERROR "android-ndk.cmake: unsupported ANDROID_ABI '${ANDROID_ABI}' (expected arm64-v8a, armeabi-v7a, x86_64, or x86)")
    endif()
  else()
    # Host-hermetic gate configure (T3): no ABI validation here; re-include
    # this file after project() with directory scope so the dual-ABI outputs
    # below can be defined as real targets.
    set(CMAKE_PROJECT_INCLUDE "${CMAKE_CURRENT_LIST_FILE}" CACHE FILEPATH "Re-include android-ndk.cmake after project() for the hermetic dual-ABI gate (T3)")
  endif()
else()
  # ---- post-project context (via CMAKE_PROJECT_INCLUDE or include()) ----
  if(DEFINED ANDROID_ABI)
    # Real Android build lane: pins and ABI validation were applied at
    # preload (T1). This branch stays a no-op so NDK builds are unaffected.
  else()
    # ---- T3 hermetic dual-ABI gate (host toolchain, no NDK) ----
    # Artifact root: the gate test resolves argv[1] > $ANDROID_BUILD_ROOT >
    # relative build-android-<abi>. The ctest entry below passes the root
    # explicitly, so the test never depends on its working directory.
    set(MUSE_ANDROID_BUILD_ROOT "${CMAKE_BINARY_DIR}/build-android" CACHE PATH
        "Hermetic dual-ABI gate root; per-ABI outputs land in <root>-<abi>/libRuntime.so")

    find_package(JNI QUIET)

    # Both ABI outputs compile the same sources as the host `runtime` lib
    # (see engine/runtime/CMakeLists.txt); no tree/frame/layout/renderer
    # behavior change — this only adds per-ABI shared outputs with the
    # JNI-only export surface (hidden visibility).
    foreach(MUSE_ABI IN ITEMS arm64-v8a x86_64)
      if(MUSE_ABI STREQUAL "arm64-v8a")
        set(_muse_tgt Runtime_android_arm64_v8a)
      else()
        set(_muse_tgt Runtime_android_x86_64)
      endif()
      add_library(${_muse_tgt} SHARED
        src/tree.cpp
        src/frame.cpp
        src/layout.cpp
        src/js_runtime.cpp
        src/js_engine.cpp
        src/vulkan_baseline.cpp
        src/renderer.cpp
        src/jni_bridge.cpp
      )
      set_target_properties(${_muse_tgt} PROPERTIES
        OUTPUT_NAME Runtime
        LIBRARY_OUTPUT_DIRECTORY "${MUSE_ANDROID_BUILD_ROOT}-${MUSE_ABI}"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
      )
      target_include_directories(${_muse_tgt} PUBLIC include)
      if(JNI_FOUND)
        target_include_directories(${_muse_tgt} PUBLIC ${JNI_INCLUDE_DIRS})
      endif()
      target_compile_options(${_muse_tgt} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()

    enable_testing()
    add_executable(android_build_gate tests/android_build_gate_test.cpp)
    set_target_properties(android_build_gate PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
    )
    target_compile_options(android_build_gate PRIVATE -Wall -Wextra -Wpedantic)
    add_dependencies(android_build_gate
      Runtime_android_arm64_v8a
      Runtime_android_x86_64
    )
    add_test(NAME android_build_gate
      COMMAND android_build_gate "${MUSE_ANDROID_BUILD_ROOT}")
  endif()
endif()
