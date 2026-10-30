cmake_minimum_required(VERSION 3.16)
project(muse_text CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(jni_free_path_test tests/jni_free_path_test.cpp)
target_link_libraries(jni_free_path_test PRIVATE muse_text)
target_compile_options(jni_free_path_test PRIVATE -Wall -Wextra -Wpedantic)

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
