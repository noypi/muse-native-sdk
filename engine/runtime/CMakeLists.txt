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
)

add_library(runtime
  src/tree.cpp
  src/frame.cpp
  src/layout.cpp
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
  ../text/src/rich_buffer.cpp
)

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

# BUG-M3-FIDELITY-007 T5: the valid-env touch dispatch body in jni_bridge.cpp
# drives ResolveTapTarget/ClaimTap/FireTapHandler (input_chain.cpp) over the
# consume-only engine/input units, so their TUs compile into the static
# runtime (engine/input itself is untouched — consumed, never modified).
target_sources(runtime PRIVATE ${MUSE_INPUT_SOURCES})

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
    src/frame.cpp
    src/layout.cpp
    src/js_runtime.cpp
    src/js_engine.cpp
    src/vulkan_baseline.cpp
    src/renderer.cpp
    src/jni_bridge.cpp
    src/input_chain.cpp
    src/gallery_present.cpp
    src/presenter.cpp
    ../graphics/vector/slug_msdf.cc
    ../text/src/shaper.cpp
    ../text/src/layout_cache.cpp
    ../text/src/font_discovery.cpp
    ../text/src/font_discovery_android.cpp
    ../text/src/glyph_atlas.cpp
    ../text/src/rich_buffer.cpp
    ../graphics/vector/tile_raster.cc
    ${MUSE_INPUT_SOURCES}
    ../graphics/pacing/frame_rate.cc
    ../graphics/pacing/render_ahead.cc
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
  # T4 pacing link fix: frame_rate.cc/render_ahead.cc self-include as
  # "engine/graphics/pacing/...", so the repo root must be on the include
  # path (mirrors the T4 pinning test's `-I .`).
  target_include_directories(Runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  if(JNI_FOUND)
    target_include_directories(Runtime PUBLIC ${JNI_INCLUDE_DIRS})
  endif()
  target_compile_options(Runtime PRIVATE -Wall -Wextra -Wpedantic)
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
