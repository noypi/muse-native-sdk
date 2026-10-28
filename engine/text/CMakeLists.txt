cmake_minimum_required(VERSION 3.16)
project(muse_text CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(muse_text STATIC
  src/shaper.cpp
  src/font_discovery.cpp
  src/layout_cache.cpp
  src/bidi.cpp
  src/emoji.cpp
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

include(CTest)
enable_testing()
add_test(NAME jni_free_path_test COMMAND jni_free_path_test)
add_test(NAME system_fonts_test COMMAND system_fonts_test)
add_test(NAME layout_cache_test COMMAND layout_cache_test)
add_test(NAME harfbuzz_parity_test COMMAND harfbuzz_parity_test)
add_test(NAME script_coverage_test COMMAND script_coverage_test)
add_test(NAME emoji_test COMMAND emoji_test)
