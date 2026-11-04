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
  # Re-entrancy guard: a vendored sub-build below calls project(), which
  # re-fires CMAKE_PROJECT_INCLUDE and would re-enter this file (double
  # add_library / double add_subdirectory). Normal variables inherit into
  # the subdir scope, so the nested entry sees the guard and returns.
  if(_MUSE_ANDROID_NDK_POST_DONE)
    return()
  endif()
  set(_MUSE_ANDROID_NDK_POST_DONE TRUE)
  if(DEFINED ANDROID_ABI)
    # Real Android build lane: pins and ABI validation were applied at
    # preload (T1). This branch stays a no-op so NDK builds are unaffected.
    #
    # T3 (vendor-d3) D3 lane: the Android static D3 objects (harfbuzz +
    # freetype + icu, built from $MUSE_NATIVE_THIRD_PARTY_DIR) come from the
    # engine/text in-tree sub-builds, configured with this same NDK toolchain
    # for the same ANDROID_ABI:
    #   cmake -S engine/text -B engine/text/build-android-<abi> \
    #     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    #     -DANDROID_ABI=<abi> -DANDROID_PLATFORM=android-29
    #   cmake --build engine/text/build-android-<abi>
    # The host-tools-first ICU split (--with-cross-build=<ABS host build>,
    # --enable-static --disable-shared, --with-data-packaging=archive) runs
    # inside that configure; the Android `Runtime` SHARED target gains the
    # resulting D3 static objects in T4. No third-party sources live in the
    # repo — only pins (tools/deps/third_party_pins.json) plus this wiring.
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
    #
    # The input TU set mirrors MUSE_INPUT_SOURCES (single-sourced in
    # CMakeLists.txt; local fallback when this file is included before it is
    # defined, e.g. via CMAKE_PROJECT_INCLUDE which runs before the rest of
    # the project body). The pacing TUs mirror the real-Android `Runtime`
    # lane (presenter.cpp's device lane calls pacing::selectIntendedFrameRate
    # / tryQueueFrame), with the repo root on the include path for the
    # pacing headers' own "engine/..." includes.
    if(NOT DEFINED MUSE_INPUT_SOURCES)
      set(MUSE_INPUT_SOURCES
        ../input/src/event_model.cpp
        ../input/src/hit_test.cpp
        ../input/src/gesture_arena.cpp
        ../input/src/tap_dispatch.cpp
      )
    endif()
    # FR-DEPS-013 (mandatory Yoga, fail-closed): the gate targets solve
    # through the same Yoga seam as prod (no in-tree fallback). Wired
    # inline here because this branch runs via CMAKE_PROJECT_INCLUDE
    # before the CMakeLists body defines _muse_apply_yoga; the body
    # reuses the same yogacore target (NOT TARGET guard), so exactly one
    # sub-build exists per configure. Missing snapshot fails configure.
    # The sub-build lives outside the ABI loop (one yogacore for both gate
    # targets); its project() call re-fires CMAKE_PROJECT_INCLUDE, which
    # the re-entrancy guard above absorbs.
    if(NOT DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} OR "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
      message(FATAL_ERROR "MUSE Yoga: MUSE_NATIVE_THIRD_PARTY_DIR is not set (vendored snapshot yoga-3.2.1 is required). Example: export MUSE_NATIVE_THIRD_PARTY_DIR=<shared-workspace>/third-party")
    endif()
    set(_muse_gate_yoga_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/yoga-3.2.1")
    if(NOT IS_DIRECTORY "${_muse_gate_yoga_src}/yoga")
      message(FATAL_ERROR "MUSE Yoga: vendored snapshot missing: ${_muse_gate_yoga_src}/yoga (extract yoga-v3.2.1.tar.gz from the pinned third-party dir; see tools/deps/third_party_pins.json)")
    endif()
    if(NOT TARGET yogacore)
      add_subdirectory("${_muse_gate_yoga_src}/yoga" "${CMAKE_BINARY_DIR}/yoga-yogacore")
    endif()
    foreach(MUSE_ABI IN ITEMS arm64-v8a x86_64)
      if(MUSE_ABI STREQUAL "arm64-v8a")
        set(_muse_tgt Runtime_android_arm64_v8a)
      else()
        set(_muse_tgt Runtime_android_x86_64)
      endif()
      add_library(${_muse_tgt} SHARED        src/tree.cpp
        src/frame.cpp
        src/layout.cpp
        src/yoga_layout.cpp
        src/layout_seam.cpp
        src/js_runtime.cpp
        src/js_engine.cpp
        src/vulkan_baseline.cpp
        src/renderer.cpp
        src/jni_bridge.cpp
        src/input_chain.cpp
        src/gallery_present.cpp
        src/presenter.cpp
        ../text/src/glyph_raster_orientation.cpp
        ${MUSE_INPUT_SOURCES}
        ../graphics/pacing/frame_rate.cc
        ../graphics/pacing/render_ahead.cc
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
      target_include_directories(${_muse_tgt} PRIVATE ../input/include)
      target_include_directories(${_muse_tgt} PRIVATE ../text/include)
      # Pacing TUs self-include as "engine/graphics/pacing/..." (mirrors the
      # real-Android Runtime lane in CMakeLists.txt). This file lives in
      # engine/runtime/cmake, so the repo root is three levels up.
      target_include_directories(${_muse_tgt} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../..)
      # FR-DEPS-013 (mandatory Yoga, fail-closed): the gate targets solve
      # through the same Yoga seam as prod (no in-tree fallback). Wired
      # inline here because this branch runs via CMAKE_PROJECT_INCLUDE
      # before the CMakeLists body defines _muse_apply_yoga; the body
      # reuses the same yogacore target (NOT TARGET guard), so exactly one
      # sub-build exists per configure. Missing snapshot fails configure.
      if(NOT DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} OR "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
        message(FATAL_ERROR "MUSE Yoga: MUSE_NATIVE_THIRD_PARTY_DIR is not set (vendored snapshot yoga-3.2.1 is required). Example: export MUSE_NATIVE_THIRD_PARTY_DIR=<shared-workspace>/third-party")
      endif()
      set(_muse_gate_yoga_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/yoga-3.2.1")
      if(NOT IS_DIRECTORY "${_muse_gate_yoga_src}/yoga")
        message(FATAL_ERROR "MUSE Yoga: vendored snapshot missing: ${_muse_gate_yoga_src}/yoga (extract yoga-v3.2.1.tar.gz from the pinned third-party dir; see tools/deps/third_party_pins.json)")
      endif()
      target_include_directories(${_muse_tgt} PRIVATE "${_muse_gate_yoga_src}")
      target_compile_definitions(${_muse_tgt} PRIVATE MUSE_HAVE_YOGA=1)
      target_link_libraries(${_muse_tgt} PRIVATE yogacore)
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
