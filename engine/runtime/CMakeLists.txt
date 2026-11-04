cmake_minimum_required(VERSION 3.16)
project(muse_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single source for the consume-only engine/input TUs (review MINOR): the
# host `runtime` lib, the real-Android `Runtime` shared lib, and the
# `muse_input_units` hermetic helper all compile this exact set (engine/input
# itself stays untouched — consumed, never modified). Add a TU here, not in
# three places; the NDK hermetic gate (cmake/android-ndk.cmake) mirrors it
# via the same variable with a local fallback.
set(MUSE_INPUT_SOURCES
  ../input/src/event_model.cpp
  ../input/src/hit_test.cpp
  ../input/src/gesture_arena.cpp
  ../input/src/tap_dispatch.cpp
)

add_library(runtime
  src/tree.cpp
  src/content_registry.cpp
  src/frame.cpp
  src/layout.cpp
  src/yoga_layout.cpp
  src/layout_seam.cpp
  src/js_runtime.cpp
  src/js_engine.cpp
  src/vulkan_baseline.cpp
  src/renderer.cpp
  src/jni_bridge.cpp
  src/gallery_present.cpp
  src/host.cpp
  src/input.cpp
  src/input_chain.cpp
  src/ime_inset.cpp
  src/text.cpp
  src/text_field.cpp
  src/presenter.cpp
)
target_include_directories(runtime PUBLIC include)
target_include_directories(runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../input/include)
target_compile_options(runtime PRIVATE -Wall -Wextra -Wpedantic)

# BUG-M3-FIDELITY-012 T7: device-side provenance decider. Bake engine-sha
# (git rev-parse --short HEAD) + gallery_present content-sha
# (git hash-object --short of src/gallery_present.cpp) at configure time as
# string macros for the runtime target (and the Runtime shared target below).
# Non-git exports / absent git fall back to "unknown" and never fail
# configure. Compile-definitions route only (no generated header).
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE MUSE_ENGINE_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT MUSE_ENGINE_GIT_SHA)
  set(MUSE_ENGINE_GIT_SHA "unknown")
endif()
execute_process(
  COMMAND git hash-object src/gallery_present.cpp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE MUSE_GALLERY_PRESENT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT MUSE_GALLERY_PRESENT_SHA)
  set(MUSE_GALLERY_PRESENT_SHA "unknown")
endif()
target_compile_definitions(runtime PRIVATE
  "ENGINE_GIT_SHA=\"${MUSE_ENGINE_GIT_SHA}\""
  "GALLERY_PRESENT_SHA=\"${MUSE_GALLERY_PRESENT_SHA}\""
)

# BUG-M3-FIDELITY-005 T5: gallery emission shapes through the engine text
# stack (shapeRunWithFallback over the T2 chain, RichBuffer-owned text, T3
# atlas, measureLine sizing), so the text TUs gallery_present.cpp consumes
# compile into the static runtime (engine/text itself is untouched —
# consumed, never modified).
target_include_directories(runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../text/include)
target_sources(runtime PRIVATE
  ../text/src/shaper.cpp
  ../text/src/layout_cache.cpp
  ../text/src/font_discovery.cpp
  ../text/src/font_discovery_android.cpp
  ../text/src/glyph_atlas.cpp
  ../text/src/glyph_raster_orientation.cpp
  ../text/src/raster_blend_orientation.cpp
  ../text/src/rich_buffer.cpp
)

# BUG-TEXT-008 t5r4: the shipped `runtime` graph compiles its own copies of
# the text TUs above (engine/text itself is consumed, never modified), so the
# orientation guard ships here too — without this TU the device raster path
# never executes GlyphUploadPreservesAdvanceDirection (PR #258 wired it only
# into muse_text; libRuntime.so sizes stayed byte-identical).

# BUG-M3-FIDELITY-005 T6: Swapchain rasterization of ShapedText runs consumes
# the slug_msdf placement + tile_raster CPU mirror directly (consume-only:
# engine/graphics/vector stays untouched — compiled in, like engine/input).
# The slug_msdf.cc TU self-includes as "engine/graphics/vector/...", so the
# repo root rides the private include path.
target_sources(runtime PRIVATE
  ../graphics/vector/slug_msdf.cc
  ../graphics/vector/tile_raster.cc
)
target_include_directories(runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# BUG-NAV-005: the generic router-invoke JNI seam in jni_bridge.cpp calls the
# live RouterBridge::invoke commit gate (engine/navigation itself is
# untouched — consumed, never modified). Compile the minimal closure
# (bridge + graph + snapshot + schema + typed routes + trie; std-only,
# hermetic, no engine) into the static runtime, mirroring the engine/text
# and engine/input consumption above. No navigation header reaches the
# public runtime include dir; no route literals live in JNI (TC-NAV-047).
set(MUSE_NAVIGATION_SOURCES
  ../navigation/src/router_bridge.cpp
  ../navigation/src/graph.cpp
  ../navigation/src/snapshot.cpp
  ../navigation/src/state_schema.cpp
  ../navigation/src/typed_routes.cpp
  ../navigation/src/trie.cpp
)
target_include_directories(runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../navigation/include)
target_sources(runtime PRIVATE ${MUSE_NAVIGATION_SOURCES})

# BUG-TEXT-007 runtime NDK fix: the text TUs consumed above (shaper.cpp
# T9) include <ft2build.h> + <hb.h> directly, but this project never runs
# the engine/text in-tree D3 sub-builds, so no freetype/harfbuzz target
# propagates those include dirs here. Mirror the engine/text mechanism:
# resolve the vendored trees via $MUSE_NATIVE_THIRD_PARTY_DIR (never a
# hardcoded absolute path) and expose the headers to the text TUs.
# Guarded: a lane without the env/mirror builds exactly as before.
if(DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} AND NOT "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
  set(_muse_runtime_freetype_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/freetype-2.15.0")
  set(_muse_runtime_harfbuzz_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/harfbuzz-15.0.0")
  if(IS_DIRECTORY "${_muse_runtime_freetype_src}/include")
    target_include_directories(runtime PRIVATE "${_muse_runtime_freetype_src}/include")
  endif()
  if(IS_DIRECTORY "${_muse_runtime_harfbuzz_src}/src")
    target_include_directories(runtime PRIVATE "${_muse_runtime_harfbuzz_src}/src")
  endif()
endif()

# BUG-M3-FIDELITY-007 T5: the valid-env touch dispatch body in jni_bridge.cpp
# drives ResolveTapTarget/ClaimTap/FireTapHandler (input_chain.cpp) over the
# consume-only engine/input units, so their TUs compile into the static
# runtime (engine/input itself is untouched — consumed, never modified).
target_sources(runtime PRIVATE ${MUSE_INPUT_SOURCES})

# FR-DEPS-008 D3: real HarfBuzz/ICU/FreeType stack behind the text seams.
# Admitted allow-list libs (specs-cpp-dependencies.md Notes D3); no new
# pins, no unadmitted libs. Resolution order per FR-DEPS-009 locked setup:
# MUSE_TEXT_D3_ROOT (content-keyed per-ABI mirror, unversioned dev names)
# first, then the lane's system runtime copies (versioned names — no dev
# symlinks assumed). Guarded: a lane with no D3 artifacts builds exactly as
# before (reference backend in shaper.cpp); configure never fails here.
# Host lanes may use the multiarch system paths below; the ANDROID lane
# uses the locked mirror only and never host paths.
if(ANDROID)
  set(_muse_d3_hints ${MUSE_TEXT_D3_ROOT} ${MUSE_TEXT_D3_ROOT}/lib)
  set(_muse_d3_paths "")
else()
  set(_muse_d3_hints ${MUSE_TEXT_D3_ROOT} ${MUSE_TEXT_D3_ROOT}/lib)
  set(_muse_d3_paths
    /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu
    /lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu
    /lib/arm-linux-gnueabihf /usr/lib/arm-linux-gnueabihf
    /lib/i386-linux-gnu /usr/lib/i386-linux-gnu)
endif()
set(_muse_d3_devnames harfbuzz icuuc freetype)
set(_muse_d3_runtimenames libharfbuzz.so.0 libicuuc.so.76 libfreetype.so.6)
set(MUSE_TEXT_D3_LIBS "")
foreach(_muse_d3_dev IN LISTS _muse_d3_devnames)
  unset(_muse_d3_found CACHE)
  find_library(_muse_d3_found NAMES ${_muse_d3_dev}
    HINTS ${_muse_d3_hints})
  if(NOT _muse_d3_found)
    list(FIND _muse_d3_devnames ${_muse_d3_dev} _muse_d3_idx)
    list(GET _muse_d3_runtimenames ${_muse_d3_idx} _muse_d3_rt)
    find_file(_muse_d3_found NAMES ${_muse_d3_rt}
      HINTS ${_muse_d3_hints}
      PATHS ${_muse_d3_paths})
    unset(_muse_d3_idx)
    unset(_muse_d3_rt)
  endif()
  if(_muse_d3_found)
    list(APPEND MUSE_TEXT_D3_LIBS "${_muse_d3_found}")
  endif()
endforeach()
unset(_muse_d3_found CACHE)
if(MUSE_TEXT_D3_LIBS AND NOT ANDROID)
  # -Wl,--push-state/--no-as-needed keeps the DT_NEEDED entries (and the
  # weak-probe relocations in engine/text/src/shaper.cpp) even though the
  # text TUs reference the D3 entry points weakly until the locked headers
  # land; --pop-state restores the lane default for later libs.
  target_link_libraries(runtime PRIVATE
    "-Wl,--push-state,--no-as-needed"
    ${MUSE_TEXT_D3_LIBS}
    "-Wl,--pop-state")
endif()

# FR-DEPS-013 T6: Yoga headers behind the LayoutEngine seam. Resolves the
# vendored snapshot via $MUSE_NATIVE_THIRD_PARTY_DIR (never a hardcoded
# absolute path) and exposes it only to the seam adapter TU
# (src/yoga_layout.cpp — the single production TU that may name Yoga
# headers, per TC-DEPS-049). Guarded: a lane without the snapshot builds
# exactly as before (in-tree interim, YogaAvailable()==false); configure
# never fails here.
if(DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} AND NOT "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
  set(_muse_yoga_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/yoga-3.2.1")
  if(IS_DIRECTORY "${_muse_yoga_src}/yoga")
    set_source_files_properties(src/yoga_layout.cpp PROPERTIES
      INCLUDE_DIRECTORIES "${_muse_yoga_src}")
  endif()
endif()

# BUG-TEXT-007 NDK link fix (static lane): test executables link
# libruntime.a whose shaper.o (T9) strongly references FT_Init_FreeType /
# hb_ft_font_create / hb_buffer_* etc., but nothing linked the vendored
# static archives — `ld.lld: error: undefined symbol` x7. The shared
# `Runtime` target below already resolves exact-path `.a` archives under
# ${MUSE_TEXT_D3_ROOT}/lib; the static `runtime` never did (the shared-lib
# block above is host-only by `AND NOT ANDROID`). Mirror the exact-path
# resolution here: ${MUSE_TEXT_D3_ROOT}/lib when set, else the conventional
# per-ABI mirror staged from the vendored tree
# (`<build>/d3-mirror-<ABI>/lib`, sibling of this OUTDIR). Guarded:
# --start-group/--end-group so inter-archive order (hb->ft, i18n->uc->data)
# cannot fail the single-pass static link; a lane with no archives links
# exactly as before and configure never fails.
if(ANDROID)
  set(_muse_static_d3_root "")
  if(DEFINED MUSE_TEXT_D3_ROOT AND NOT "${MUSE_TEXT_D3_ROOT}" STREQUAL "")
    set(_muse_static_d3_root "${MUSE_TEXT_D3_ROOT}")
  elseif(DEFINED ANDROID_ABI AND NOT "${ANDROID_ABI}" STREQUAL "")
    set(_muse_static_d3_root "${CMAKE_BINARY_DIR}/../d3-mirror-${ANDROID_ABI}")
  endif()
  set(_muse_static_d3_names
    harfbuzz freetype icuuc icudata icui18n)
  set(MUSE_RUNTIME_STATIC_D3_LIBS "")
  if(NOT "${_muse_static_d3_root}" STREQUAL "")
    foreach(_muse_static_d3 IN LISTS _muse_static_d3_names)
      set(_muse_static_d3_archive "${_muse_static_d3_root}/lib/lib${_muse_static_d3}.a")
      if(EXISTS "${_muse_static_d3_archive}")
        list(APPEND MUSE_RUNTIME_STATIC_D3_LIBS "${_muse_static_d3_archive}")
      endif()
    endforeach()
  endif()
  if(MUSE_RUNTIME_STATIC_D3_LIBS)
    target_link_libraries(runtime PRIVATE
      "-Wl,--start-group"
      ${MUSE_RUNTIME_STATIC_D3_LIBS}
      "-Wl,--end-group")
  endif()
endif()

# T4 pacing link fix (Android-configured static lane): under the Android
# toolchain __ANDROID__ is defined, so presenter.cpp's device lane calls
# pacing::selectIntendedFrameRate/tryQueueFrame and every hermetic test
# executable linking the static runtime needs the objects. Host lane
# (no ANDROID toolchain) is untouched — its presenter.o never references
# these symbols.
if(ANDROID)
  target_sources(runtime PRIVATE
    ../graphics/pacing/frame_rate.cc
    ../graphics/pacing/render_ahead.cc
  )
  target_include_directories(runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
endif()

# FR-RUNTIME-011 T4 (Android-configured static lane): under the Android
# toolchain __ANDROID__ is defined, so jni_bridge.cpp's device lane
# (RenderLocked) calls PresentFrameWithFallback (egl_blit.cpp) and
# PresentSkiaFrameWithFallback (present_skia.cpp), and every hermetic test
# executable linking the static runtime needs the objects. Host lane
# (no ANDROID toolchain) is untouched — its jni_bridge.o never references
# these symbols. Mirrors the pacing fix above and the shared Runtime
# target, which already compiles both TUs.
if(ANDROID)
  target_sources(runtime PRIVATE
    src/egl_blit.cpp
    ../graphics/skia/present_skia.cpp
  )
endif()

find_package(JNI)
if(JNI_FOUND)
  target_include_directories(runtime PUBLIC ${JNI_INCLUDE_DIRS})
endif()

# Gallery bring-up: jni_bridge references ANativeWindow/log on Android, so
# every consumer of the static runtime (including the hermetic test
# executables the Android lane also builds) links the NDK android and log
# libs. Host lane (no ANDROID toolchain) is untouched.
if(ANDROID)
  find_library(MUSE_ANDROID_LIB_STATIC android)
  find_library(MUSE_LOG_LIB_STATIC log)
  if(MUSE_ANDROID_LIB_STATIC)
    target_link_libraries(runtime PRIVATE ${MUSE_ANDROID_LIB_STATIC})
  endif()
  if(MUSE_LOG_LIB_STATIC)
    target_link_libraries(runtime PRIVATE ${MUSE_LOG_LIB_STATIC})
  endif()
endif()

# Stage 5 F1: real-Android lane — shared Runtime -> libRuntime.so in
# CMAKE_BINARY_DIR. Host lane untouched (DEFINED ANDROID_ABI guard);
# NOT TARGET Runtime guard avoids double-define under the -C hermetic
# preload. Minimal 8-file JNI set mirrors the hermetic gate.
if(DEFINED ANDROID_ABI AND NOT TARGET Runtime)
  add_library(Runtime SHARED
    src/tree.cpp
    src/content_registry.cpp
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
    src/text_field.cpp
    src/gallery_present.cpp
    src/presenter.cpp
    ../graphics/vector/slug_msdf.cc
    ../text/src/shaper.cpp
    ../text/src/layout_cache.cpp
    ../text/src/font_discovery.cpp
    ../text/src/font_discovery_android.cpp
    ../text/src/glyph_atlas.cpp
    ../text/src/glyph_raster_orientation.cpp
    ../text/src/raster_blend_orientation.cpp
    ../text/src/rich_buffer.cpp
    ../graphics/vector/tile_raster.cc
    ../graphics/skia/present_skia.cpp
    ${MUSE_INPUT_SOURCES}
    ../graphics/pacing/frame_rate.cc
    ../graphics/pacing/render_ahead.cc
    # BUG-NAV-005 (shared lane): live RouterBridge commit gate behind the
    # generic invoke JNI seam — same consume-only closure as the static
    # runtime above (engine/navigation untouched, never modified).
    ${MUSE_NAVIGATION_SOURCES}
  )
  set_target_properties(Runtime PROPERTIES
    OUTPUT_NAME Runtime
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
  )
  target_include_directories(Runtime PUBLIC include)
  target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../input/include)
  target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../text/include)
  # BUG-NAV-005 (shared lane): navigation headers for the consume-only
  # RouterBridge closure above (PRIVATE only; never public).
  target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../navigation/include)
  # T4 pacing link fix: frame_rate.cc/render_ahead.cc self-include as
  # "engine/graphics/pacing/...", so the repo root must be on the include
  # path (mirrors the T4 pinning test's `-I .`).
  target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  # BUG-TEXT-007 runtime NDK fix (shared lane): same vendored D3 headers
  # for the text TUs compiled into Runtime (mirrors the static lane
  # above; $MUSE_NATIVE_THIRD_PARTY_DIR mechanism, guarded, no hardcode).
  if(DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} AND NOT "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
    set(_muse_shared_freetype_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/freetype-2.15.0")
    set(_muse_shared_harfbuzz_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/harfbuzz-15.0.0")
    if(IS_DIRECTORY "${_muse_shared_freetype_src}/include")
      target_include_directories(Runtime PRIVATE "${_muse_shared_freetype_src}/include")
    endif()
    if(IS_DIRECTORY "${_muse_shared_harfbuzz_src}/src")
      target_include_directories(Runtime PRIVATE "${_muse_shared_harfbuzz_src}/src")
    endif()
  endif()
  if(JNI_FOUND)
    target_include_directories(Runtime PUBLIC ${JNI_INCLUDE_DIRS})
  endif()
  target_compile_options(Runtime PRIVATE -Wall -Wextra -Wpedantic)
  # BUG-M3-FIDELITY-012 T7: same provenance macros for the shared target.
  target_compile_definitions(Runtime PRIVATE
    "ENGINE_GIT_SHA=\"${MUSE_ENGINE_GIT_SHA}\""
    "GALLERY_PRESENT_SHA=\"${MUSE_GALLERY_PRESENT_SHA}\""
  )
  # Gallery bring-up: ANativeWindow raster + logcat diagnostics live in
  # jni_bridge/gallery_present, so the shared target links the NDK android
  # and log libs (Android lane only; host lane untouched).
  if(ANDROID)
    find_library(MUSE_ANDROID_LIB android)
    find_library(MUSE_LOG_LIB log)
    if(MUSE_ANDROID_LIB)
      target_link_libraries(Runtime PRIVATE ${MUSE_ANDROID_LIB})
    endif()
    if(MUSE_LOG_LIB)
      target_link_libraries(Runtime PRIVATE ${MUSE_LOG_LIB})
    endif()
  endif()
  # FR-DEPS-008/009 T4 (vendor-d3): the Android `Runtime` SHARED target
  # links the vendored D3 static objects from the T3 sub-builds
  # (engine/text in-tree static harfbuzz + freetype + icu archives under
  # MUSE_TEXT_D3_ROOT), never the shared find_library mirror above.
  # Static-only posture: exact-path `.a` archive resolution (EXISTS, no
  # find_library search), so no system shared lib can slip onto this lane
  # regardless of the NDK toolchain's find-root modes. A lane with no D3
  # archives links nothing (reference backend in shaper.cpp); configure
  # never fails here. Host `runtime` lane untouched (its own block above
  # is unchanged).
  set(_muse_runtime_d3_static_names
    harfbuzz freetype icuuc icudata icui18n)
  set(MUSE_RUNTIME_D3_STATIC_LIBS "")
  foreach(_muse_d3_static IN LISTS _muse_runtime_d3_static_names)
    set(_muse_d3_static_archive "${MUSE_TEXT_D3_ROOT}/lib/lib${_muse_d3_static}.a")
    if(NOT EXISTS "${_muse_d3_static_archive}")
      set(_muse_d3_static_archive "${MUSE_TEXT_D3_ROOT}/lib${_muse_d3_static}.a")
    endif()
    if(EXISTS "${_muse_d3_static_archive}")
      list(APPEND MUSE_RUNTIME_D3_STATIC_LIBS "${_muse_d3_static_archive}")
    endif()
  endforeach()
  if(MUSE_RUNTIME_D3_STATIC_LIBS)
    # T7: whole-archive so the vendored D3 objects (HarfBuzz/FreeType/ICU)
    # define their entry points in the shared link. Archive-member extraction
    # alone pulls nothing: shaper.cpp references D3 only through weak probes
    # (hb_shape/FT_Init_FreeType/u_getVersion, null on hermetic builds), and
    # weak-undefined refs do not extract archive members — leaving the probes
    # weak-undefined (`w` in nm -D) instead of defined (`T`). Whole-archive
    # pins the DT entries and the nm-visible symbols the proof checks.
    target_link_libraries(Runtime PRIVATE
      "-Wl,--push-state,--whole-archive"
      ${MUSE_RUNTIME_D3_STATIC_LIBS}
      "-Wl,--no-whole-archive,--pop-state")
  else()
    # BUG-TEXT-007 T15-leg3 link fix: the engine-android lane configures this
    # project standalone (no MUSE_TEXT_D3_ROOT passed), so the archive block
    # above is empty and the Runtime shared link fails with undefined
    # FT_Init_FreeType/hb_* (shaper.cpp T9 ResolveRealFont/ShapeWithHarfBuzz).
    # Fallback: in-tree vendored sub-builds (same mechanism/flags as
    # engine/text/CMakeLists.txt T3, hermetic via $MUSE_NATIVE_THIRD_PARTY_DIR,
    # no downloads) providing the `freetype` + `harfbuzz` targets for this
    # lane only. ICU intentionally not sub-built here: the leg3 undefined
    # set is FT/HB only. Guarded: missing env/sources or pre-existing
    # targets keep the reference-backend behavior; configure never fails.
    if(DEFINED ENV{MUSE_NATIVE_THIRD_PARTY_DIR} AND NOT "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}" STREQUAL "")
      set(_muse_rt_ft_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/freetype-2.15.0")
      set(_muse_rt_hb_src "$ENV{MUSE_NATIVE_THIRD_PARTY_DIR}/harfbuzz-15.0.0")
      if(IS_DIRECTORY "${_muse_rt_ft_src}" AND IS_DIRECTORY "${_muse_rt_hb_src}")
        if(NOT TARGET freetype)
          set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "runtime lane: FreeType without HarfBuzz helper" FORCE)
          set(FT_DISABLE_ZLIB ON CACHE BOOL "runtime lane: no system zlib" FORCE)
          set(FT_DISABLE_BZIP2 ON CACHE BOOL "runtime lane: no system bzip2" FORCE)
          set(FT_DISABLE_PNG ON CACHE BOOL "runtime lane: no system libpng" FORCE)
          set(FT_DISABLE_BROTLI ON CACHE BOOL "runtime lane: no system brotli" FORCE)
          add_subdirectory("${_muse_rt_ft_src}" "${CMAKE_BINARY_DIR}/d3-runtime/freetype")
        endif()
        if(NOT TARGET harfbuzz)
          set(HB_HAVE_ICU OFF CACHE BOOL "runtime lane: HarfBuzz without ICU" FORCE)
          set(HB_BUILD_UTILS OFF CACHE BOOL "runtime lane: no hb utils" FORCE)
          set(HB_BUILD_SUBSET OFF CACHE BOOL "runtime lane: no hb-subset" FORCE)
          set(HB_BUILD_RASTER OFF CACHE BOOL "runtime lane: no hb-raster" FORCE)
          set(HB_BUILD_VECTOR OFF CACHE BOOL "runtime lane: no hb-vector" FORCE)
          set(HB_BUILD_GPU OFF CACHE BOOL "runtime lane: no hb-gpu" FORCE)
          set(HB_BUILD_GPU_DEMO OFF CACHE STRING "runtime lane: no hb-gpu demo" FORCE)
          add_subdirectory("${_muse_rt_hb_src}" "${CMAKE_BINARY_DIR}/d3-runtime/harfbuzz")
        endif()
        if(TARGET freetype AND TARGET harfbuzz)
          target_link_libraries(Runtime PRIVATE freetype harfbuzz)
          message(STATUS "MUSE D3: vendored in-tree freetype+harfbuzz sub-builds linked into Runtime (no MUSE_TEXT_D3_ROOT archives)")
        else()
          message(STATUS "MUSE D3: no vendored static archives under '${MUSE_TEXT_D3_ROOT}' (MUSE_RUNTIME_D3_STATIC_LIBS empty); linking reference backend")
        endif()
      else()
        message(STATUS "MUSE D3: no vendored static archives under '${MUSE_TEXT_D3_ROOT}' (MUSE_RUNTIME_D3_STATIC_LIBS empty); linking reference backend")
      endif()
    else()
      message(STATUS "MUSE D3: no vendored static archives under '${MUSE_TEXT_D3_ROOT}' (MUSE_RUNTIME_D3_STATIC_LIBS empty); linking reference backend")
    endif()
  endif()
endif()
# BUG-TEXT-007 T15-leg4 link fix (static lane): every test executable links
# the static `runtime` whose shaper.o (T9) strongly references
# FT_Init_FreeType/hb_* — propagate the same vendored targets there once so
# all test links resolve (render_smoke_test, gallery_registry_lifetime_test,
# jni_boundary_test, ...). Static-PRIVATE deps propagate to the final test
# links; host lanes and the shared Runtime lane above are untouched.
# Guarded: a lane without the in-tree sub-builds links exactly as before.
if(ANDROID AND TARGET freetype AND TARGET harfbuzz)
  target_link_libraries(runtime PRIVATE freetype harfbuzz)
endif()

add_executable(instantiate_test tests/instantiate_test.cpp)
target_link_libraries(instantiate_test PRIVATE runtime)
target_compile_options(instantiate_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME instantiate_test COMMAND instantiate_test)

add_executable(invalidation_test tests/invalidation_test.cpp)
target_link_libraries(invalidation_test PRIVATE runtime)
target_compile_options(invalidation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME invalidation_test COMMAND invalidation_test)

add_executable(idle_frames_test tests/idle_frames_test.cpp)
target_link_libraries(idle_frames_test PRIVATE runtime)
target_compile_options(idle_frames_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME idle_frames_test COMMAND idle_frames_test)

add_executable(layout_test tests/layout_test.cpp)
target_link_libraries(layout_test PRIVATE runtime)
target_compile_options(layout_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME layout_test COMMAND layout_test)

add_executable(relayout_scope_test tests/relayout_scope_test.cpp)
target_link_libraries(relayout_scope_test PRIVATE runtime)
target_compile_options(relayout_scope_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME relayout_scope_test COMMAND relayout_scope_test)

add_executable(layout_to_draw_test tests/layout_to_draw_test.cpp)
target_link_libraries(layout_to_draw_test PRIVATE runtime)
target_compile_options(layout_to_draw_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME layout_to_draw_test COMMAND layout_to_draw_test)

add_executable(js_abi_isolation_test tests/js_abi_isolation_test.cpp)
target_link_libraries(js_abi_isolation_test PRIVATE runtime)
target_compile_options(js_abi_isolation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_abi_isolation_test COMMAND js_abi_isolation_test)

add_executable(js_execution_test tests/js_execution_test.cpp)
target_link_libraries(js_execution_test PRIVATE runtime)
target_compile_options(js_execution_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_execution_test COMMAND js_execution_test)

add_executable(vulkan_baseline_test tests/vulkan_baseline_test.cpp)
target_link_libraries(vulkan_baseline_test PRIVATE runtime)
target_compile_options(vulkan_baseline_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME vulkan_baseline_test COMMAND vulkan_baseline_test)

add_executable(request_frame_test tests/request_frame_test.cpp)
target_link_libraries(request_frame_test PRIVATE runtime)
target_compile_options(request_frame_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME request_frame_test COMMAND request_frame_test)

add_executable(continuous_mode_test tests/continuous_mode_test.cpp)
target_link_libraries(continuous_mode_test PRIVATE runtime)
target_compile_options(continuous_mode_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME continuous_mode_test COMMAND continuous_mode_test)

add_executable(js_lifecycle_test tests/js_lifecycle_test.cpp)
target_link_libraries(js_lifecycle_test PRIVATE runtime)
target_compile_options(js_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME js_lifecycle_test COMMAND js_lifecycle_test)

add_executable(frame_budget_telemetry_test tests/frame_budget_telemetry_test.cpp)
target_link_libraries(frame_budget_telemetry_test PRIVATE runtime)
target_compile_options(frame_budget_telemetry_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME frame_budget_telemetry_test COMMAND frame_budget_telemetry_test)

add_executable(render_smoke_test tests/render_smoke_test.cpp)
target_link_libraries(render_smoke_test PRIVATE runtime)
target_compile_options(render_smoke_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME render_smoke_test COMMAND render_smoke_test)

add_executable(text_metrics_test tests/text_metrics_test.cpp)
target_link_libraries(text_metrics_test PRIVATE runtime)
target_compile_options(text_metrics_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME text_metrics_test COMMAND text_metrics_test)

add_executable(text_editing_test tests/text_editing_test.cpp)
target_link_libraries(text_editing_test PRIVATE runtime)
target_compile_options(text_editing_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME text_editing_test COMMAND text_editing_test)

add_executable(ime_inset_test tests/ime_inset_test.cpp)
target_link_libraries(ime_inset_test PRIVATE runtime)
target_compile_options(ime_inset_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ime_inset_test COMMAND ime_inset_test)

add_executable(host_lifecycle_test tests/host_lifecycle_test.cpp)
target_link_libraries(host_lifecycle_test PRIVATE runtime)
target_compile_options(host_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME host_lifecycle_test COMMAND host_lifecycle_test)

add_executable(touch_dispatch_test tests/touch_dispatch_test.cpp)
target_include_directories(touch_dispatch_test PRIVATE ../input/include)
target_link_libraries(touch_dispatch_test PRIVATE runtime muse_input_units)
target_compile_options(touch_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME touch_dispatch_test COMMAND touch_dispatch_test)

# BUG-M3-FIDELITY-007 batch 3 (T5): component-handler dispatch +
# write-through one-frame repaint halves of the device tap chain.
add_executable(tap_handler_repaint_test tests/tap_handler_repaint_test.cpp)
target_include_directories(tap_handler_repaint_test PRIVATE ../input/include)
target_link_libraries(tap_handler_repaint_test PRIVATE runtime muse_input_units)
target_compile_options(tap_handler_repaint_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tap_handler_repaint_test COMMAND tap_handler_repaint_test)

# BUG-M3-FIDELITY-007 T5 lifetime re-audit (security MEDIUM: ToGallery waiter
# UAF — registry lock released before use; fix: counted lifetime). Entry-shape
# pins for the registry-guarding JNI entries (null-env fail-closed halves);
# the live-handle + waiter halves are JVM/device-only (ASSUMPTION → T7).
add_executable(gallery_registry_lifetime_test tests/gallery_registry_lifetime_test.cpp)
target_link_libraries(gallery_registry_lifetime_test PRIVATE runtime)
target_compile_options(gallery_registry_lifetime_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_registry_lifetime_test COMMAND gallery_registry_lifetime_test)

add_executable(config_change_test tests/config_change_test.cpp)
target_link_libraries(config_change_test PRIVATE runtime)
target_compile_options(config_change_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME config_change_test COMMAND config_change_test)

add_executable(gap_buffer_test tests/gap_buffer_test.cpp)
target_link_libraries(gap_buffer_test PRIVATE runtime)
target_compile_options(gap_buffer_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gap_buffer_test COMMAND gap_buffer_test)

add_executable(alooper_bridge_test tests/alooper_bridge_test.cpp)
target_link_libraries(alooper_bridge_test PRIVATE runtime)
target_compile_options(alooper_bridge_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME alooper_bridge_test COMMAND alooper_bridge_test)

add_executable(composition_roundtrip_test tests/composition_roundtrip_test.cpp)
target_link_libraries(composition_roundtrip_test PRIVATE runtime)
target_compile_options(composition_roundtrip_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME composition_roundtrip_test COMMAND composition_roundtrip_test)

add_executable(ime_session_test tests/ime_session_test.cpp)
target_link_libraries(ime_session_test PRIVATE runtime)
target_compile_options(ime_session_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ime_session_test COMMAND ime_session_test)

add_executable(inset_animation_test tests/inset_animation_test.cpp)
target_link_libraries(inset_animation_test PRIVATE runtime)
target_compile_options(inset_animation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME inset_animation_test COMMAND inset_animation_test)

add_executable(autofill_test tests/autofill_test.cpp)
target_link_libraries(autofill_test PRIVATE runtime)
target_compile_options(autofill_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME autofill_test COMMAND autofill_test)

add_executable(keyboard_animation_test tests/keyboard_animation_test.cpp)
target_link_libraries(keyboard_animation_test PRIVATE runtime)
target_compile_options(keyboard_animation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME keyboard_animation_test COMMAND keyboard_animation_test)

add_executable(cursor_rect_test tests/cursor_rect_test.cpp)
target_link_libraries(cursor_rect_test PRIVATE runtime)
target_compile_options(cursor_rect_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME cursor_rect_test COMMAND cursor_rect_test)

add_executable(clipboard_test tests/clipboard_test.cpp)
target_link_libraries(clipboard_test PRIVATE runtime)
target_compile_options(clipboard_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME clipboard_test COMMAND clipboard_test)

add_executable(ime_depth_test tests/ime_depth_test.cpp)
target_link_libraries(ime_depth_test PRIVATE runtime)
target_compile_options(ime_depth_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ime_depth_test COMMAND ime_depth_test)

add_executable(jni_boundary_test tests/jni_boundary_test.cpp)
target_link_libraries(jni_boundary_test PRIVATE runtime)
target_compile_options(jni_boundary_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME jni_boundary_test COMMAND jni_boundary_test)

add_executable(surface_attach_test tests/surface_attach_test.cpp)
target_link_libraries(surface_attach_test PRIVATE runtime)
target_compile_options(surface_attach_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME surface_attach_test COMMAND surface_attach_test)

add_executable(swapchain_surface_test tests/swapchain_surface_test.cpp)
target_link_libraries(swapchain_surface_test PRIVATE runtime)
target_compile_options(swapchain_surface_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME swapchain_surface_test COMMAND swapchain_surface_test)

add_executable(present_frame_test tests/present_frame_test.cpp)
target_link_libraries(present_frame_test PRIVATE runtime)
target_compile_options(present_frame_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME present_frame_test COMMAND present_frame_test)

add_executable(swapchain_lifecycle_test tests/swapchain_lifecycle_test.cpp)
target_link_libraries(swapchain_lifecycle_test PRIVATE runtime)
target_compile_options(swapchain_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME swapchain_lifecycle_test COMMAND swapchain_lifecycle_test)

add_executable(paced_present_test tests/paced_present_test.cpp)
target_link_libraries(paced_present_test PRIVATE runtime)
target_compile_options(paced_present_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME paced_present_test COMMAND paced_present_test)

add_executable(gallery_present_test tests/gallery_present_test.cpp)
target_link_libraries(gallery_present_test PRIVATE runtime)
target_compile_options(gallery_present_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_present_test COMMAND gallery_present_test)

add_executable(gallery_text_decode_test tests/gallery_text_decode_test.cpp)
target_link_libraries(gallery_text_decode_test PRIVATE runtime)
target_compile_options(gallery_text_decode_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_text_decode_test COMMAND gallery_text_decode_test)

# BUG-RUNTIME-015 batch 2 (FR-RUNTIME-014): engine fail-closed gates
# (TC-051 unknown kind, TC-052 theme sentinel, TC-053 unreadable descriptor,
# TC-055 red blocks re-baseline). TC-054 stays the shell negated-grep scan
# (no C++ TU by construction).
add_executable(unknown_kind_fail_test tests/unknown_kind_fail_test.cpp)
target_link_libraries(unknown_kind_fail_test PRIVATE runtime)
target_compile_options(unknown_kind_fail_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unknown_kind_fail_test COMMAND unknown_kind_fail_test)
add_executable(theme_sentinel_fail_test tests/theme_sentinel_fail_test.cpp)
target_link_libraries(theme_sentinel_fail_test PRIVATE runtime)
target_compile_options(theme_sentinel_fail_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME theme_sentinel_fail_test COMMAND theme_sentinel_fail_test)
add_executable(unreadable_descriptor_fail_test tests/unreadable_descriptor_fail_test.cpp)
target_link_libraries(unreadable_descriptor_fail_test PRIVATE runtime)
target_compile_options(unreadable_descriptor_fail_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME unreadable_descriptor_fail_test COMMAND unreadable_descriptor_fail_test)
add_executable(proof_gate_blocks_rebaseline_test tests/proof_gate_blocks_rebaseline_test.cpp)
target_link_libraries(proof_gate_blocks_rebaseline_test PRIVATE runtime)
target_compile_options(proof_gate_blocks_rebaseline_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME proof_gate_blocks_rebaseline_test COMMAND proof_gate_blocks_rebaseline_test)

# demo-fallback-boot Batch 1 RED pins (GR-08 T1.1): T1 gate-kind coverage,
# T3 lib-freshness recipe gate. Test wiring only; no production change.
add_executable(fallback_gate_kinds_test tests/fallback_gate_kinds_test.cpp)
target_link_libraries(fallback_gate_kinds_test PRIVATE runtime)
target_compile_options(fallback_gate_kinds_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME fallback_gate_kinds_test COMMAND fallback_gate_kinds_test)
add_executable(lib_freshness_test tests/lib_freshness_test.cpp)
target_link_libraries(lib_freshness_test PRIVATE runtime)
target_compile_options(lib_freshness_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME lib_freshness_test COMMAND lib_freshness_test)

# fallback-kinds Batch 1 RED pin (GR-08 T1.1): T1 unknown/demo-kind
# fail-closed NAMED coverage. Test wiring only; no production change.
add_executable(gallery_gate_kind_test tests/gallery_gate_kind_test.cpp)
target_link_libraries(gallery_gate_kind_test PRIVATE runtime)
target_compile_options(gallery_gate_kind_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_gate_kind_test COMMAND gallery_gate_kind_test)

# SPEC-ENGINE-RUNTIME AC-RUNTIME-048/049 (FR-RUNTIME-014): TC-RUNTIME-057
# known-kind/unhandled-branch fail-closed gate +
# TC-RUNTIME-058 missing/malformed numeric descriptor gate.
add_executable(known_branch_fallback_fail_test tests/known_branch_fallback_fail_test.cpp)
target_link_libraries(known_branch_fallback_fail_test PRIVATE runtime)
target_compile_options(known_branch_fallback_fail_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME known_branch_fallback_fail_test COMMAND known_branch_fallback_fail_test)
add_executable(descriptor_number_fallback_test tests/descriptor_number_fallback_test.cpp)
target_link_libraries(descriptor_number_fallback_test PRIVATE runtime)
target_compile_options(descriptor_number_fallback_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME descriptor_number_fallback_test COMMAND descriptor_number_fallback_test)

# BUG-M3-FIDELITY-005 T5: gallery emission wiring (shape -> cache -> real
# ShapedText{glyphRunId}, measureLine sizing, UTF-8 row admission).
add_executable(gallery_text_emission_test tests/gallery_text_emission_test.cpp)
target_link_libraries(gallery_text_emission_test PRIVATE runtime)
target_compile_options(gallery_text_emission_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_text_emission_test COMMAND gallery_text_emission_test)

# BUG-M3-FIDELITY-005 T6: Swapchain rasterization of ShapedText runs.
add_executable(gallery_text_raster_test tests/gallery_text_raster_test.cpp)
target_link_libraries(gallery_text_raster_test PRIVATE runtime)
target_compile_options(gallery_text_raster_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_text_raster_test COMMAND gallery_text_raster_test)

# BUG-TEXT-008 T7d: S-spine host pin (FR-TEXT-016 / AC-TEXT-045).
add_executable(s_spine_test tests/s_spine_test.cpp)
target_link_libraries(s_spine_test PRIVATE runtime)
target_compile_options(s_spine_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME s_spine_test COMMAND s_spine_test)

# BUG-M3-FIDELITY-005 T10: presented-window sink for vector text (F1 fix).
add_executable(present_window_text_test tests/present_window_text_test.cpp)
target_link_libraries(present_window_text_test PRIVATE runtime)
target_compile_options(present_window_text_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME present_window_text_test COMMAND present_window_text_test)

# BUG-M3-FIDELITY-005 T11: pixel-level present-window tests for vector text
# (coverage blend, page slicing, no-post, card-distinct).
add_executable(present_window_text_pixels_test tests/present_window_text_pixels_test.cpp)
target_link_libraries(present_window_text_pixels_test PRIVATE runtime)
target_compile_options(present_window_text_pixels_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME present_window_text_pixels_test COMMAND present_window_text_pixels_test)

# BUG-M3-FIDELITY-007 batch 2: JNI touch-entry shape (T3) + native
# hit-test/arena wiring (T4). The input consume-lib compiles exactly the
# engine/input TUs T4 consumes (consume-only: hit-test, gesture arena, event
# model) so the hermetic device-chain tests link without pulling the whole
# input project into the runtime configure.
add_executable(jni_touch_dispatch_shape_test tests/jni_touch_dispatch_shape_test.cpp)
target_link_libraries(jni_touch_dispatch_shape_test PRIVATE runtime)
target_compile_options(jni_touch_dispatch_shape_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME jni_touch_dispatch_shape_test COMMAND jni_touch_dispatch_shape_test)

add_library(muse_input_units STATIC ${MUSE_INPUT_SOURCES})
target_include_directories(muse_input_units PUBLIC ../input/include)
target_compile_options(muse_input_units PRIVATE -Wall -Wextra -Wpedantic)

add_executable(input_chain_test tests/input_chain_test.cpp)
target_include_directories(input_chain_test PRIVATE ../input/include)
target_link_libraries(input_chain_test PRIVATE runtime muse_input_units)
target_compile_options(input_chain_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME input_chain_test COMMAND input_chain_test)

# T2 JNI contract regression (BUG-RUNTIME-002): every native* method in
# MainActivity.java must have a matching Java_com_muse_host_MainActivity_*
# export. Hermetic shell case; no device, no network.
add_test(NAME jni_contract_test
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/jni_contract_test.sh"
    "${CMAKE_BINARY_DIR}/libRuntime.so"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/m3-gallery/src/MainActivity.java"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/jni_bridge.cpp")

# T3 gallery-handle lifecycle variant (BUG-RUNTIME-002): attach -> render ->
# detach -> re-attach lifecycle at the handle level with a mock window
# (no NDK, no device). Mirrors surface_attach/swapchain_lifecycle structure.
add_executable(gallery_handle_lifecycle_test tests/gallery_handle_lifecycle_test.cpp)
target_link_libraries(gallery_handle_lifecycle_test PRIVATE runtime)
target_compile_options(gallery_handle_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_handle_lifecycle_test COMMAND gallery_handle_lifecycle_test)

# BUG-M3-FIDELITY-008 L6: layering-scene emitter order + inset-step probe
# (scrim-before-content, submission-order stacking, sheet tracks animated
# inset with no jump and no full-tree relayout).
add_executable(gallery_layering_test tests/gallery_layering_test.cpp)
target_link_libraries(gallery_layering_test PRIVATE runtime)
target_compile_options(gallery_layering_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gallery_layering_test COMMAND gallery_layering_test)

# BUG-RUNTIME-004 T1: anchorSheetToImeInset hermetic contract (ported from
# run/bug-m3-fidelity-008). Coder-wired: the tester owns test files only.
add_executable(layering_anchor_test tests/layering_anchor_test.cpp)
target_link_libraries(layering_anchor_test PRIVATE runtime)
target_compile_options(layering_anchor_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME layering_anchor_test COMMAND layering_anchor_test)

# T3 hermetic dual-ABI gate (permanent wiring; no NDK required).
# Real Android configure (ANDROID_ABI defined via T1 -C lane) skips this;
# plain host configure defines both <root>-<abi>/libRuntime.so outputs plus
# the android_build_gate ctest entry. Guarded so the -C hermetic preload
# (CMAKE_PROJECT_INCLUDE re-inclusion) does not double-define targets.
if(NOT DEFINED ANDROID_ABI AND NOT TARGET Runtime_android_arm64_v8a)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/android-ndk.cmake")
endif()

add_executable(egl_blit_test tests/egl_blit_test.cpp)
target_link_libraries(egl_blit_test PRIVATE runtime)
target_compile_options(egl_blit_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME egl_blit_test COMMAND egl_blit_test)

add_executable(present_fallback_test tests/present_fallback_test.cpp)
target_link_libraries(present_fallback_test PRIVATE runtime)
target_compile_options(present_fallback_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME present_fallback COMMAND present_fallback_test)

# AC-GRAPHICS-039 T3: Skia fallback contract (test owned by code-unit-tester,
# binds only to `PresentSkiaFrameWithFallback` + `skia_test::SetFaultHooks`
# in `engine/graphics/skia/present_skia.h`). Links the Skia present TU
# directly so the hermetic host lane needs no Skia SDK/GPU.
add_executable(skia_fallback_test tests/skia_fallback_test.cpp ../graphics/skia/present_skia.cpp)
target_include_directories(skia_fallback_test PRIVATE ../graphics/skia)
target_compile_options(skia_fallback_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME skia_fallback_test COMMAND skia_fallback_test)

# AC-GRAPHICS-041 T2: renderer-preference fallback contract (test owned by
# code-unit-tester, binds only to `ResolveRendererFallback` in
# `engine/runtime/include/runtime/renderer_preference.h`). Links the `runtime`
# lib (the coder defines the resolve function there); hermetic host lane —
# availability is injected per-call, no EGL/Skia/GPU on host.
add_executable(renderer_preference_fallback_test tests/renderer_preference_fallback_test.cpp)
target_link_libraries(renderer_preference_fallback_test PRIVATE runtime)
target_compile_options(renderer_preference_fallback_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME renderer_preference_fallback COMMAND renderer_preference_fallback_test)

add_executable(surface_contract_test tests/surface_contract_test.cpp)
target_link_libraries(surface_contract_test PRIVATE runtime)
target_compile_options(surface_contract_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME surface_contract_test COMMAND surface_contract_test)

# AC-M3-026 / AC-GRAPHICS-043 / AC-RUNTIME-033 T1: Surface-seam interactive
# path (attach -> touch -> render -> theme/inset -> detach). Hermetic host
# lane; binds only to the T1 Interface line in presenter.h.
add_executable(surface_interactive_test tests/surface_interactive_test.cpp)
target_link_libraries(surface_interactive_test PRIVATE runtime)
target_compile_options(surface_interactive_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME surface_interactive_test COMMAND surface_interactive_test)

# BUG-RUNTIME-012 Batch 1 RED pins, wired green by Batch 2 (T3+T4): the 3
# generic callers resolve without the content module and no per-app content
# remains under engine/. Hermetic source-grep gates; code-root is the tree
# that contains engine/.
add_test(NAME flappy_content_removal_check
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/flappy_content_removal_check.sh ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_test(NAME generic_caller_seam_check
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/generic_caller_seam_check.sh ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# FR-GRAPHICS-015 batch 1 (T1+T2): retained component-record presenter seam.
# Test owned by code-unit-tester; binds only to the T1/T2 `Interface:` lines.
add_executable(present_real_components_test tests/present_real_components_test.cpp)
target_link_libraries(present_real_components_test PRIVATE runtime)
target_compile_options(present_real_components_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME present_real_components_test COMMAND present_real_components_test)

# Batch 5 / T9 + engine-content-isolation T5: per-app content rendering
# (flappy game scene, never gallery-mapped rows). Test owned by
# code-unit-tester; binds only to the T9 `Interface:` line. Scene-block
# definitions deleted with the content module (T5); gallery_present.cpp
# keeps only the generic present path.
# Batch 2 / T3+T4: content-bound suites resolve dispatch through the
# registry, so the content module TU links into these executables directly
# (static-archive members otherwise drop when unreferenced). Test scope
# unchanged — linkage only; T6 owns the seam revision.
add_executable(flappy_render_path_test tests/flappy_render_path_test.cpp)
target_link_libraries(flappy_render_path_test PRIVATE runtime)
target_compile_options(flappy_render_path_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flappy_render_path_test COMMAND flappy_render_path_test)

# Batch 5 / T10: tap-tree-from-layout (real flappy bounds). Test owned by
# code-unit-tester; binds only to the T10 `Interface:` line. Definitions land
# in src/input_chain.cpp.
add_executable(flappy_tap_tree_test tests/flappy_tap_tree_test.cpp)
target_link_libraries(flappy_tap_tree_test PRIVATE runtime)
target_compile_options(flappy_tap_tree_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flappy_tap_tree_test COMMAND flappy_tap_tree_test)

# Batch 5 / T11: real tap→JS dispatch with node identity. Test owned by
# code-unit-tester; binds only to the T11 `Interface:` line. Definitions land
# in src/presenter.cpp.
add_executable(flappy_tap_js_dispatch_test tests/flappy_tap_js_dispatch_test.cpp)
target_link_libraries(flappy_tap_js_dispatch_test PRIVATE runtime)
target_compile_options(flappy_tap_js_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flappy_tap_js_dispatch_test COMMAND flappy_tap_js_dispatch_test)

# Run bug-runtime-009-010 security-review pins (5 cases). Test owned by
# code-unit-tester; binds only to the Real seams named in security-review.md
# "Security Tests Requested". Definitions live in the linked runtime lib.
add_executable(flappy_security_test tests/flappy_security_test.cpp)
target_link_libraries(flappy_security_test PRIVATE runtime)
target_compile_options(flappy_security_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flappy_security_test COMMAND flappy_security_test)

# T8R4 tester-owned acceptance regression (T8R3/F1): presents the REAL built
# tree bytes (`examples/flappy/dist/tree.mui.bin`) and asserts the flappy
# scene presents (no gallery fallback). Integration test — reads the
# filesystem by design; not hermetic, not a unit test.
add_executable(flappy_real_tree_acceptance_test tests/flappy_real_tree_acceptance_test.cpp)
target_link_libraries(flappy_real_tree_acceptance_test PRIVATE runtime)
target_compile_options(flappy_real_tree_acceptance_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flappy_real_tree_acceptance_test COMMAND flappy_real_tree_acceptance_test)

# Run bug-text-008: raster->blend seam column-order pins. Test owned by
# code-unit-tester. Definitions land in ../text/src/raster_blend_orientation.cpp
# (already compiled into runtime).
add_executable(raster_blend_orientation_test tests/raster_blend_orientation_test.cpp)
target_link_libraries(raster_blend_orientation_test PRIVATE runtime)
target_compile_options(raster_blend_orientation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME raster_blend_orientation_test COMMAND raster_blend_orientation_test)

# Batch 1 / T1+T2: content-tag registry seam (FR-RUNTIME-013 /
# AC-RUNTIME-038/039). Test owned by code-unit-tester; binds only to the T1
# `Interface:` line. Definitions land in src/content_registry.cpp.
add_executable(content_registry_test tests/content_registry_test.cpp)
target_link_libraries(content_registry_test PRIVATE runtime)
target_compile_options(content_registry_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME content_registry_test COMMAND content_registry_test)

# FR-GRAPHICS-015 batch 2 (T3): RULE-GRAPHICS-003 oracle-painter
# reachability gate. Hermetic shell case (grep/nm over the page-target
# objects, mirroring the jni_contract_test.sh pattern); no device, no
# network. Fails naming the offending symbol when an oracle painter is
# reachable from the device-frame page path.
add_test(NAME gallery_page_no_oracle_check
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/gallery_page_no_oracle_check.sh"
    "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# T8 production-link gate (FR-DEPS-008, AC-DEPS-019/021): no hermetic
# test double may ship in production. Plan Interface names
# `--target libRuntime.so`; the shipped gate exposes `--manifest` +
# `--self-test` only (T1.10 variance), so the wiring maps target->manifest:
# the production link set for the `Runtime` shared target is expressed as
# `build/dependency-manifest.json`, which is what the gate checks. Invoke
# the script with that manifest; exit nonzero fails the build. Gate
# semantics untouched (script read-only, never weakened); DI seams
# (AC-DEPS-021) stay denied on this tree so the gate keeps passing here.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  set(_MUSE_PROD_LINK_PYTHON ${Python3_EXECUTABLE})
else()
  set(_MUSE_PROD_LINK_PYTHON python3)
endif()
add_custom_target(production_link_gate ALL
  COMMAND ${_MUSE_PROD_LINK_PYTHON}
    "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/deps/production_link_check.py"
    --manifest "${CMAKE_CURRENT_SOURCE_DIR}/../../build/dependency-manifest.json"
  COMMENT "T8 production-link gate: allow-list + no test-double reachable (fails build on violation)"
  VERBATIM)
