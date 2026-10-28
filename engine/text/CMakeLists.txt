cmake_minimum_required(VERSION 3.16)
project(muse_text CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(muse_text STATIC
  src/shaper.cpp
  src/font_discovery.cpp
)
target_include_directories(muse_text PUBLIC include)
target_compile_options(muse_text PRIVATE -Wall -Wextra -Wpedantic)

add_executable(jni_free_path_test tests/jni_free_path_test.cpp)
target_link_libraries(jni_free_path_test PRIVATE muse_text)
target_compile_options(jni_free_path_test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(system_fonts_test tests/system_fonts_test.cpp)
target_link_libraries(system_fonts_test PRIVATE muse_text)
target_compile_options(system_fonts_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME jni_free_path_test COMMAND jni_free_path_test)
add_test(NAME system_fonts_test COMMAND system_fonts_test)
