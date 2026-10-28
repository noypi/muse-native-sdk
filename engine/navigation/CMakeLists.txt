cmake_minimum_required(VERSION 3.16)
project(muse_navigation CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(navigation
  src/graph.cpp
  src/snapshot.cpp
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
  src/route_guard.cpp
  src/typed_routes.cpp
  src/nested_graph.cpp
  src/result_channel.cpp
  src/route_transition.cpp
  src/predictive_transition.cpp
  src/entry_funnel.cpp
  src/back_dispatch.cpp
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

add_executable(snapshot_capture_test tests/snapshot_capture_test.cpp)
target_link_libraries(snapshot_capture_test PRIVATE navigation)
target_compile_options(snapshot_capture_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME snapshot_capture_test COMMAND snapshot_capture_test)

add_executable(snapshot_scroll_focus_test tests/snapshot_scroll_focus_test.cpp)
target_link_libraries(snapshot_scroll_focus_test PRIVATE navigation)
target_compile_options(snapshot_scroll_focus_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME snapshot_scroll_focus_test COMMAND snapshot_scroll_focus_test)

add_executable(snapshot_truncation_test tests/snapshot_truncation_test.cpp)
target_link_libraries(snapshot_truncation_test PRIVATE navigation)
target_compile_options(snapshot_truncation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME snapshot_truncation_test COMMAND snapshot_truncation_test)

add_executable(restore_test tests/restore_test.cpp)
target_link_libraries(restore_test PRIVATE navigation)
target_compile_options(restore_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME restore_test COMMAND restore_test)

add_executable(process_death_proxy_test tests/process_death_proxy_test.cpp)
target_link_libraries(process_death_proxy_test PRIVATE navigation)
target_compile_options(process_death_proxy_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME process_death_proxy_test COMMAND process_death_proxy_test)

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

add_executable(security_gate_test tests/security_gate_test.cpp)
target_link_libraries(security_gate_test PRIVATE navigation)
target_compile_options(security_gate_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME security_gate_test COMMAND security_gate_test)

add_executable(route_guard_test tests/route_guard_test.cpp)
target_link_libraries(route_guard_test PRIVATE navigation)
target_compile_options(route_guard_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME route_guard_test COMMAND route_guard_test)

add_executable(param_validation_test tests/param_validation_test.cpp)
target_link_libraries(param_validation_test PRIVATE navigation)
target_compile_options(param_validation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME param_validation_test COMMAND param_validation_test)

add_executable(nested_deeplink_test tests/nested_deeplink_test.cpp)
target_link_libraries(nested_deeplink_test PRIVATE navigation)
target_compile_options(nested_deeplink_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME nested_deeplink_test COMMAND nested_deeplink_test)

add_executable(result_channel_test tests/result_channel_test.cpp)
target_link_libraries(result_channel_test PRIVATE navigation)
target_compile_options(result_channel_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME result_channel_test COMMAND result_channel_test)

add_executable(route_transition_config_test tests/route_transition_config_test.cpp)
target_link_libraries(route_transition_config_test PRIVATE navigation)
target_compile_options(route_transition_config_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME route_transition_config_test COMMAND route_transition_config_test)

add_executable(predictive_progress_test tests/predictive_progress_test.cpp)
target_link_libraries(predictive_progress_test PRIVATE navigation)
target_compile_options(predictive_progress_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME predictive_progress_test COMMAND predictive_progress_test)

add_executable(transition_retarget_test tests/transition_retarget_test.cpp)
target_link_libraries(transition_retarget_test PRIVATE navigation)
target_compile_options(transition_retarget_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME transition_retarget_test COMMAND transition_retarget_test)

add_executable(entry_funnel_test tests/entry_funnel_test.cpp)
target_link_libraries(entry_funnel_test PRIVATE navigation)
target_compile_options(entry_funnel_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME entry_funnel_test COMMAND entry_funnel_test)

add_executable(back_dispatch_test tests/back_dispatch_test.cpp)
target_link_libraries(back_dispatch_test PRIVATE navigation)
target_compile_options(back_dispatch_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME back_dispatch_test COMMAND back_dispatch_test)

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
