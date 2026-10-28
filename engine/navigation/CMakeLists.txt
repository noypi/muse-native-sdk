cmake_minimum_required(VERSION 3.16)
project(muse_navigation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(navigation
  src/graph.cpp
  src/state_schema.cpp
  src/trie.cpp
  src/predictive_back.cpp
  src/router_policy.cpp
  src/router_bridge.cpp
  src/cold_link.cpp
  src/applinks_manifest.cpp
  src/uri_validation.cpp
  src/deferred_link.cpp
  src/link_validation.cpp
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

add_executable(predictive_back_stream_test tests/predictive_back_stream_test.cpp)
target_link_libraries(predictive_back_stream_test PRIVATE navigation)
target_compile_options(predictive_back_stream_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME predictive_back_stream_test COMMAND predictive_back_stream_test)

add_executable(single_router_test tests/single_router_test.cpp)
target_link_libraries(single_router_test PRIVATE navigation)
target_compile_options(single_router_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME single_router_test COMMAND single_router_test)

add_executable(state_schema_test tests/state_schema_test.cpp)
target_link_libraries(state_schema_test PRIVATE navigation)
target_compile_options(state_schema_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME state_schema_test COMMAND state_schema_test)

add_executable(router_bridge_test tests/router_bridge_test.cpp)
target_link_libraries(router_bridge_test PRIVATE navigation)
target_compile_options(router_bridge_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME router_bridge_test COMMAND router_bridge_test)

add_executable(cold_link_perf_test tests/cold_link_perf_test.cpp)
target_link_libraries(cold_link_perf_test PRIVATE navigation)
target_compile_options(cold_link_perf_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME cold_link_perf_test COMMAND cold_link_perf_test)

add_executable(uri_validation_test tests/uri_validation_test.cpp)
target_link_libraries(uri_validation_test PRIVATE navigation)
target_compile_options(uri_validation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME uri_validation_test COMMAND uri_validation_test)

add_executable(deferred_link_test tests/deferred_link_test.cpp)
target_link_libraries(deferred_link_test PRIVATE navigation)
target_compile_options(deferred_link_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME deferred_link_test COMMAND deferred_link_test)

add_executable(link_validation_test tests/link_validation_test.cpp)
target_link_libraries(link_validation_test PRIVATE navigation)
target_compile_options(link_validation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME link_validation_test COMMAND link_validation_test)

# FR-NAV-006 T2 (NAV-APPLINKS codegen invocation — documentation only, no new
# engine target added here):
#   Generate: python3 ../../toolchain/applinks/codegen.py
#               --manifest <manifest.json> --out <out-dir>
#             (emits intent-filters.xml + assetlinks.json from the single-source
#             route manifest described by applinks_manifest.schema.json).
#   Drift gate (TC-NAV-013, fails the build on hand-edited outputs):
#             python3 ../../toolchain/applinks/codegen.py
#               --manifest <manifest.json> --out <out-dir> --check
#             exits 0 on match, 2 on drift with a unified diff on stdout.
#   CI owner of the drift gate is `./gradlew :toolchain:appLinksCodegen`;
#   the hermetic `codegen.py --check` invocation above exercises the same
#   behavior without a Gradle daemon.
