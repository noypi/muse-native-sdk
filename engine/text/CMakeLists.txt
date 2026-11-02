cmake_minimum_required(VERSION 3.16)
project(muse_text CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# T3 (vendor-d3, FR-DEPS-008 AC-DEPS-019): in-tree static D3 sub-builds
# (HarfBuzz 15.0.0 + FreeType 2.15.0 + ICU 79.1) for the host lane and the
# Android NDK lane. Sources resolve via $MUSE_NATIVE_THIRD_PARTY_DIR (sibling
# of the repo checkout, outside git); pins live in
# tools/deps/third_party_pins.json and are enforced by
# toolchain/cli/thirdPartyEnv.js. The vendored trees are consumed read-only
# and every sub-build lands out-of-source under CMAKE_BINARY_DIR — never
# in-source, never into the repo. There is intentionally no
# system-libs lookup here: hermetic static objects only.
option(MUSE_TEXT_WITH_D3 "Build muse_text against in-tree static D3 (HarfBuzz/FreeType/ICU)" ON)
if(MUSE_TEXT_WITH_D3)
  if(NOT DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} OR "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
    message(FATAL_ERROR "engine/text: MUSE_NATIVE_THIRD_PARTY_DIR is not set (in-tree D3 sub-builds need the shared third-party directory). Example: export MUSE_NATIVE_THIRD_PARTY_DIR=<shared-workspace>/third-party")
  endif()
  set(MUSE_NATIVE_THIRD_PARTY_DIR "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}")
  set(MUSE_HARFBUZZ_SRC "${MUSE_NATIVE_THIRD_PARTY_DIR}/harfbuzz-15.0.0")
  set(MUSE_FREETYPE_SRC "${MUSE_NATIVE_THIRD_PARTY_DIR}/freetype-2.15.0")
  set(MUSE_ICU_SRC "${MUSE_NATIVE_THIRD_PARTY_DIR}/icu-79.1/source")
  foreach(_muse_d3_src IN ITEMS "${MUSE_HARFBUZZ_SRC}" "${MUSE_FREETYPE_SRC}" "${MUSE_ICU_SRC}")
    if(NOT IS_DIRECTORY "${_muse_d3_src}")
      message(FATAL_ERROR "engine/text: D3 source dir missing: ${_muse_d3_src} (check MUSE_NATIVE_THIRD_PARTY_DIR; example: export MUSE_NATIVE_THIRD_PARTY_DIR=<shared-workspace>/third-party)")
    endif()
  endforeach()

  # FreeType 2.15.0 first: the vendored tree configures itself from its own
  # include/freetype/config/ftoption.h toggles, the modules list
  # (include/freetype/config/ftmodule.h generated from modules.cfg) and the
  # src/base/ftsystem.c system interface (SD8) — no invented flags, only the
  # real FT_DISABLE_* options from the vendored CMakeLists. FT_DISABLE_HARFBUZZ
  # breaks the HarfBuzz<->FreeType configure cycle and keeps the lane hermetic
  # (no system harfbuzz picked up); the compression backends stay off so no
  # system zlib/bzip2/png/brotli sneaks into the static objects.
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "T3: FreeType without HarfBuzz helper (breaks the HB<->FT configure cycle)" FORCE)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "T3: no system zlib in the static objects" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "T3: no system bzip2 in the static objects" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "T3: no system libpng in the static objects" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "T3: no system brotli in the static objects" FORCE)
  add_subdirectory("${MUSE_FREETYPE_SRC}" "${CMAKE_BINARY_DIR}/d3/freetype")

  # HarfBuzz 15.0.0: -DHB_HAVE_ICU=OFF is the verified upstream default (SD6);
  # HB_HAVE_FREETYPE interop flips ON automatically because TARGET freetype
  # already exists (see the vendored CMakeLists "if (TARGET freetype)").
  # HB_BUILD_UTILS stays OFF (upstream default, kept); the optional
  # subset/raster/vector/gpu modules default ON upstream and are turned OFF —
  # the engine text stack needs none of them.
  set(HB_HAVE_ICU OFF CACHE BOOL "T3: HarfBuzz without ICU unicode functions (SD6 verified default)" FORCE)
  set(HB_BUILD_UTILS OFF CACHE BOOL "T3: no harfbuzz utils (needs cairo/glib)" FORCE)
  set(HB_BUILD_SUBSET OFF CACHE BOOL "T3: no harfbuzz-subset (unused by the engine)" FORCE)
  set(HB_BUILD_RASTER OFF CACHE BOOL "T3: no harfbuzz-raster (unused by the engine)" FORCE)
  set(HB_BUILD_VECTOR OFF CACHE BOOL "T3: no harfbuzz-vector (unused by the engine)" FORCE)
  set(HB_BUILD_GPU OFF CACHE BOOL "T3: no harfbuzz-gpu (unused by the engine)" FORCE)
  set(HB_BUILD_GPU_DEMO OFF CACHE STRING "T3: no hb-gpu demo tool" FORCE)
  add_subdirectory("${MUSE_HARFBUZZ_SRC}" "${CMAKE_BINARY_DIR}/d3/harfbuzz")

  # ICU 79.1: host-tools-first split (SD7). The host tools build
  # (runConfigureICU) runs first in its own out-of-source dir; the target
  # build then configures --with-cross-build=<ABS host build dir> with
  # --enable-static --disable-shared --with-data-packaging=archive (archive
  # mode needs the app to point at the data at runtime — T4 wires the
  # u_setDataDirectory/udata_setCommonData call site; until then the static
  # libicudata archive ships inside the link) and -DU_CHARSET_IS_UTF8=1,
  # always out-of-source, never in-source. Without the host-tools script the
  # target configure cannot be staged, so it fails closed here.
  if(NOT EXISTS "${MUSE_ICU_SRC}/runConfigureICU")
    message(FATAL_ERROR "engine/text: ICU host-tools script missing: ${MUSE_ICU_SRC}/runConfigureICU — cannot stage the host/cross-build split (re-extract the icu4c tarball per tools/deps/third_party_pins.json)")
  endif()
  include(ExternalProject)
  set(MUSE_ICU_HOST_DIR "${CMAKE_BINARY_DIR}/d3/icu-host")
  set(MUSE_ICU_TARGET_DIR "${CMAKE_BINARY_DIR}/d3/icu-target")
  set(MUSE_ICU_INSTALL_DIR "${CMAKE_BINARY_DIR}/d3/icu-install")
  file(MAKE_DIRECTORY "${MUSE_ICU_INSTALL_DIR}/include")
  file(MAKE_DIRECTORY "${MUSE_ICU_INSTALL_DIR}/lib")
  set(MUSE_ICU_UTF8_FLAGS "-DU_CHARSET_IS_UTF8=1")
  if(ANDROID)
    # Android NDK lane: the host tools build with the host compiler, then the
    # target build cross-configures against the NDK clang for ANDROID_ABI.
    if(ANDROID_ABI STREQUAL "arm64-v8a")
      set(_muse_icu_triplet "aarch64-linux-android")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
      set(_muse_icu_triplet "armv7a-linux-androideabi")
    elseif(ANDROID_ABI STREQUAL "x86_64")
      set(_muse_icu_triplet "x86_64-linux-android")
    elseif(ANDROID_ABI STREQUAL "x86")
      set(_muse_icu_triplet "i686-linux-android")
    else()
      message(FATAL_ERROR "engine/text: no ICU cross-build triplet for ANDROID_ABI '${ANDROID_ABI}' (expected arm64-v8a, armeabi-v7a, x86_64, or x86)")
    endif()
    string(REGEX MATCH "[0-9]+" _muse_icu_api "${ANDROID_PLATFORM}")
    if(_muse_icu_api STREQUAL "")
      set(_muse_icu_api "29")
    endif()
    if(NOT DEFINED ANDROID_TOOLCHAIN_ROOT OR ANDROID_TOOLCHAIN_ROOT STREQUAL "")
      get_filename_component(_muse_ndk_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
      get_filename_component(ANDROID_TOOLCHAIN_ROOT "${_muse_ndk_bin}" DIRECTORY)
    endif()
    ExternalProject_Add(icu_host
      SOURCE_DIR "${MUSE_ICU_SRC}"
      BINARY_DIR "${MUSE_ICU_HOST_DIR}"
      CONFIGURE_COMMAND sh "${MUSE_ICU_SRC}/runConfigureICU" Linux
        "--prefix=${MUSE_ICU_HOST_DIR}"
        --enable-static --disable-shared --with-data-packaging=archive
        "CFLAGS=${MUSE_ICU_UTF8_FLAGS}" "CXXFLAGS=${MUSE_ICU_UTF8_FLAGS}"
      BUILD_COMMAND make -j
      INSTALL_COMMAND ""
      BUILD_BYPRODUCTS "${MUSE_ICU_HOST_DIR}/lib/libicuuc.a"
    )
    ExternalProject_Add(icu_target
      DEPENDS icu_host
      SOURCE_DIR "${MUSE_ICU_SRC}"
      BINARY_DIR "${MUSE_ICU_TARGET_DIR}"
      CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        "CC=${ANDROID_TOOLCHAIN_ROOT}/bin/${_muse_icu_triplet}${_muse_icu_api}-clang"
        "CXX=${ANDROID_TOOLCHAIN_ROOT}/bin/${_muse_icu_triplet}${_muse_icu_api}-clang++"
        "CFLAGS=${MUSE_ICU_UTF8_FLAGS}" "CXXFLAGS=${MUSE_ICU_UTF8_FLAGS}"
        sh "${MUSE_ICU_SRC}/configure"
        "--prefix=${MUSE_ICU_INSTALL_DIR}"
        "--host=${_muse_icu_triplet}"
        "--with-cross-build=${MUSE_ICU_HOST_DIR}"
        --enable-static --disable-shared --with-data-packaging=archive
      BUILD_COMMAND make -j
      INSTALL_COMMAND make install
      BUILD_BYPRODUCTS
        "${MUSE_ICU_INSTALL_DIR}/lib/libicuuc.a"
        "${MUSE_ICU_INSTALL_DIR}/lib/libicui18n.a"
        "${MUSE_ICU_INSTALL_DIR}/lib/libicudata.a"
    )
  else()
    # Host lane: single native build (runConfigureICU host-tools shape),
    # installed to the in-build prefix; the target IS the host.
    ExternalProject_Add(icu_host
      SOURCE_DIR "${MUSE_ICU_SRC}"
      BINARY_DIR "${MUSE_ICU_HOST_DIR}"
      CONFIGURE_COMMAND sh "${MUSE_ICU_SRC}/runConfigureICU" Linux
        "--prefix=${MUSE_ICU_INSTALL_DIR}"
        --enable-static --disable-shared --with-data-packaging=archive
        "CFLAGS=${MUSE_ICU_UTF8_FLAGS}" "CXXFLAGS=${MUSE_ICU_UTF8_FLAGS}"
      BUILD_COMMAND make -j
      INSTALL_COMMAND make install
      BUILD_BYPRODUCTS
        "${MUSE_ICU_INSTALL_DIR}/lib/libicuuc.a"
        "${MUSE_ICU_INSTALL_DIR}/lib/libicui18n.a"
        "${MUSE_ICU_INSTALL_DIR}/lib/libicudata.a"
    )
  endif()
  add_library(icuuc_static STATIC IMPORTED GLOBAL)
  add_library(icui18n_static STATIC IMPORTED GLOBAL)
  add_library(icudata_static STATIC IMPORTED GLOBAL)
  set_target_properties(icuuc_static PROPERTIES
    IMPORTED_LOCATION "${MUSE_ICU_INSTALL_DIR}/lib/libicuuc.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUSE_ICU_INSTALL_DIR}/include")
  set_target_properties(icui18n_static PROPERTIES
    IMPORTED_LOCATION "${MUSE_ICU_INSTALL_DIR}/lib/libicui18n.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUSE_ICU_INSTALL_DIR}/include")
  set_target_properties(icudata_static PROPERTIES
    IMPORTED_LOCATION "${MUSE_ICU_INSTALL_DIR}/lib/libicudata.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUSE_ICU_INSTALL_DIR}/include")
  if(ANDROID)
    add_dependencies(icuuc_static icu_target)
    add_dependencies(icui18n_static icu_target)
    add_dependencies(icudata_static icu_target)
  else()
    add_dependencies(icuuc_static icu_host)
    add_dependencies(icui18n_static icu_host)
    add_dependencies(icudata_static icu_host)
  endif()
endif()

add_library(muse_text STATIC
  src/shaper.cpp
  src/font_discovery.cpp
  # T2 (FR-TEXT-016): production Android NDK lookup TU. Compiled on every
  # build; contributes the NDK-backed SystemFontEnumerator only when
  # MUSE_TEXT_ANDROID_NDK is defined (com.muse.host device build), so host
  # builds keep the stub enumerator and need no NDK headers.
  src/font_discovery_android.cpp
  src/layout_cache.cpp
  src/bidi.cpp
  src/emoji.cpp
  src/stylus.cpp
  src/rich_buffer.cpp
  src/code_mode.cpp
  src/font_scale.cpp
  src/scale_layout.cpp
  src/scale_config.cpp
  src/glyph_atlas.cpp
  ink/ink_stroke_pipeline.cpp
  ink/palm_rejector.cpp
  ink/handwriting_committer.cpp
  edit/selection_controller.cpp
  edit/edit_undo.cpp
  edit/plain_exporter.cpp
)
target_include_directories(muse_text PUBLIC include)
target_compile_options(muse_text PRIVATE -Wall -Wextra -Wpedantic)

# T3: muse_text links the in-tree static D3 objects (harfbuzz + freetype +
# icuuc/icui18n/icudata). ICU recommends UTF-8 charset handling
# (-DU_CHARSET_IS_UTF8=1, same flag the ICU sub-builds use); with
# --with-data-packaging=archive the data archive rides inside libicudata and
# the app points at it at runtime via u_setDataDirectory/udata_setCommonData
# (T4 call site; MUSE_ICU_DATA_DIR records the packaged location for it).
if(MUSE_TEXT_WITH_D3)
  add_dependencies(muse_text icuuc_static icui18n_static icudata_static)
  target_link_libraries(muse_text PUBLIC harfbuzz freetype icui18n_static icuuc_static icudata_static)
  target_compile_definitions(muse_text PUBLIC U_CHARSET_IS_UTF8=1)
  set(MUSE_ICU_DATA_DIR "${MUSE_ICU_INSTALL_DIR}/share/icu/79.1" CACHE PATH
      "T3: packaged ICU data dir (T4 app call: u_setDataDirectory/udata_setCommonData)")
endif()

add_executable(jni_free_path_test tests/jni_free_path_test.cpp)
target_link_libraries(jni_free_path_test PRIVATE muse_text)
target_compile_options(jni_free_path_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(shaping_jni_test tests/shaping_jni_test.cpp)
target_link_libraries(shaping_jni_test PRIVATE muse_text)
target_compile_options(shaping_jni_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(system_fonts_test tests/system_fonts_test.cpp)
target_link_libraries(system_fonts_test PRIVATE muse_text)
target_compile_options(system_fonts_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(layout_cache_test tests/layout_cache_test.cpp)
target_link_libraries(layout_cache_test PRIVATE muse_text)
target_compile_options(layout_cache_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(harfbuzz_parity_test tests/harfbuzz_parity_test.cpp)
target_link_libraries(harfbuzz_parity_test PRIVATE muse_text)
target_compile_options(harfbuzz_parity_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(script_coverage_test tests/script_coverage_test.cpp)
target_link_libraries(script_coverage_test PRIVATE muse_text)
target_compile_options(script_coverage_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(emoji_test tests/emoji_test.cpp)
target_link_libraries(emoji_test PRIVATE muse_text)
target_compile_options(emoji_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(font_fallback_test tests/font_fallback_test.cpp)
target_link_libraries(font_fallback_test PRIVATE muse_text)
target_compile_options(font_fallback_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(uax_conformance_test tests/uax_conformance_test.cpp)
target_link_libraries(uax_conformance_test PRIVATE muse_text)
target_compile_options(uax_conformance_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(fallback_chain_test tests/fallback_chain_test.cpp)
target_link_libraries(fallback_chain_test PRIVATE muse_text)
target_compile_options(fallback_chain_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(downloadable_font_test tests/downloadable_font_test.cpp)
target_link_libraries(downloadable_font_test PRIVATE muse_text)
target_compile_options(downloadable_font_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(emoji_cluster_test tests/emoji_cluster_test.cpp)
target_link_libraries(emoji_cluster_test PRIVATE muse_text)
target_compile_options(emoji_cluster_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(script_shaping_test tests/script_shaping_test.cpp)
target_link_libraries(script_shaping_test PRIVATE muse_text)
target_compile_options(script_shaping_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(variable_font_test tests/variable_font_test.cpp)
target_link_libraries(variable_font_test PRIVATE muse_text)
target_compile_options(variable_font_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(font_subset_guard_test tests/font_subset_guard_test.cpp)
target_link_libraries(font_subset_guard_test PRIVATE muse_text)
target_compile_options(font_subset_guard_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(golden_harness_test tests/golden_harness_test.cpp)
target_link_libraries(golden_harness_test PRIVATE muse_text)
target_compile_options(golden_harness_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(font_pipeline_test tests/font_pipeline_test.cpp)
target_link_libraries(font_pipeline_test PRIVATE muse_text)
target_compile_options(font_pipeline_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(stylus_samples_test tests/stylus_samples_test.cpp)
target_link_libraries(stylus_samples_test PRIVATE muse_text)
target_compile_options(stylus_samples_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(stylus_axes_test ink/tests/stylus_axes_test.cpp)
target_link_libraries(stylus_axes_test PRIVATE muse_text)
target_compile_options(stylus_axes_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(palm_rejection_test ink/tests/palm_rejection_test.cpp)
target_link_libraries(palm_rejection_test PRIVATE muse_text)
target_compile_options(palm_rejection_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(handwriting_test ink/tests/handwriting_test.cpp)
target_link_libraries(handwriting_test PRIVATE muse_text)
target_compile_options(handwriting_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(selection_parity_test edit/tests/selection_parity_test.cpp)
target_link_libraries(selection_parity_test PRIVATE muse_text)
target_compile_options(selection_parity_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(undo_test edit/tests/undo_test.cpp)
target_link_libraries(undo_test PRIVATE muse_text)
target_compile_options(undo_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(plain_degrade_test edit/tests/plain_degrade_test.cpp)
target_link_libraries(plain_degrade_test PRIVATE muse_text)
target_compile_options(plain_degrade_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gesture_script_test edit/tests/gesture_script_test.cpp)
target_link_libraries(gesture_script_test PRIVATE muse_text)
target_compile_options(gesture_script_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(rich_buffer_test tests/rich_buffer_test.cpp)
target_link_libraries(rich_buffer_test PRIVATE muse_text)
target_compile_options(rich_buffer_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(nonlinear_scale_test tests/nonlinear_scale_test.cpp)
target_link_libraries(nonlinear_scale_test PRIVATE muse_text)
target_compile_options(nonlinear_scale_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(scale_remeasure_test tests/scale_remeasure_test.cpp)
target_link_libraries(scale_remeasure_test PRIVATE muse_text)
target_compile_options(scale_remeasure_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(scale_config_test tests/scale_config_test.cpp)
target_link_libraries(scale_config_test PRIVATE muse_text)
target_compile_options(scale_config_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(scale_churn_test tests/scale_churn_test.cpp)
target_link_libraries(scale_churn_test PRIVATE muse_text)
target_compile_options(scale_churn_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pipeline_acceptance acceptance/pipeline_acceptance.cpp)
target_link_libraries(pipeline_acceptance PRIVATE muse_text)
target_compile_options(pipeline_acceptance PRIVATE -Wall -Wextra -Wpedantic)

add_executable(glyph_atlas_test tests/glyph_atlas_test.cpp)
target_link_libraries(glyph_atlas_test PRIVATE muse_text)
target_compile_options(glyph_atlas_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME jni_free_path_test COMMAND jni_free_path_test)
add_test(NAME shaping_jni_test COMMAND shaping_jni_test)
add_test(NAME system_fonts_test COMMAND system_fonts_test)
add_test(NAME layout_cache_test COMMAND layout_cache_test)
add_test(NAME harfbuzz_parity_test COMMAND harfbuzz_parity_test)
add_test(NAME script_coverage_test COMMAND script_coverage_test)
add_test(NAME emoji_test COMMAND emoji_test)
add_test(NAME font_fallback_test COMMAND font_fallback_test)
add_test(NAME uax_conformance_test COMMAND uax_conformance_test)
add_test(NAME fallback_chain_test COMMAND fallback_chain_test)
add_test(NAME downloadable_font_test COMMAND downloadable_font_test)
add_test(NAME emoji_cluster_test COMMAND emoji_cluster_test)
add_test(NAME script_shaping_test COMMAND script_shaping_test)
add_test(NAME variable_font_test COMMAND variable_font_test)
add_test(NAME font_subset_guard_test COMMAND font_subset_guard_test)
add_test(NAME golden_harness_test COMMAND golden_harness_test)
add_test(NAME font_pipeline_test COMMAND font_pipeline_test)
add_test(NAME stylus_samples_test COMMAND stylus_samples_test)
add_test(NAME stylus_axes_test COMMAND stylus_axes_test)
add_test(NAME palm_rejection_test COMMAND palm_rejection_test)
add_test(NAME handwriting_test COMMAND handwriting_test)
add_test(NAME selection_parity_test COMMAND selection_parity_test)
add_test(NAME undo_test COMMAND undo_test)
add_test(NAME plain_degrade_test COMMAND plain_degrade_test)
add_test(NAME gesture_script_test COMMAND gesture_script_test)
add_test(NAME rich_buffer_test COMMAND rich_buffer_test)
add_test(NAME nonlinear_scale_test COMMAND nonlinear_scale_test)
add_test(NAME scale_remeasure_test COMMAND scale_remeasure_test)
add_test(NAME scale_config_test COMMAND scale_config_test)
add_test(NAME scale_churn_test COMMAND scale_churn_test)
add_test(NAME glyph_atlas_test COMMAND glyph_atlas_test)
add_test(NAME pipeline_acceptance COMMAND pipeline_acceptance)
