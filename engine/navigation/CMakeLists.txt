cmake_minimum_required(VERSION 3.16)
project(muse_navigation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(navigation
  src/graph.cpp
  src/trie.cpp
)
target_include_directories(navigation PUBLIC include)
target_compile_options(navigation PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

add_executable(graph_unification_test tests/graph_unification_test.cpp)
target_link_libraries(graph_unification_test PRIVATE navigation)
target_compile_options(graph_unification_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME graph_unification_test COMMAND graph_unification_test)

add_executable(trie_match_test tests/trie_match_test.cpp)
target_link_libraries(trie_match_test PRIVATE navigation)
target_compile_options(trie_match_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME trie_match_test COMMAND trie_match_test)
