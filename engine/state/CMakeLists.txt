cmake_minimum_required(VERSION 3.16)
project(muse_state CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(state src/atom.cpp src/store.cpp src/provenance.cpp src/frame.cpp query/src/query_client.cpp query/src/query_core.cpp query/src/cache.cpp persist/src/snapshot.cpp persist/src/corruption.cpp persist/src/rehydrate.cpp persist/src/restore.cpp sync/src/mutation_queue.cpp sync/src/crdt.cpp sync/src/epoch.cpp sync/src/sync_scheduler.cpp sync/src/delta.cpp devtools/src/devtools.cpp)
target_include_directories(state PUBLIC include query/include persist/include sync/include devtools/include)
target_compile_options(state PRIVATE -Wall -Wextra -Wpedantic)
set_source_files_properties(devtools/src/devtools.cpp PROPERTIES COMPILE_DEFINITIONS "MUSE_STATE_DEVTOOLS=1")

add_executable(atom_core_test tests/atom_core_test.cpp)
target_link_libraries(atom_core_test PRIVATE state)
target_compile_options(atom_core_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()
add_test(NAME atom_core_test COMMAND atom_core_test)

add_executable(tier2_provenance_test tests/tier2_provenance_test.cpp)
target_link_libraries(tier2_provenance_test PRIVATE state)
target_compile_options(tier2_provenance_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tier2_provenance_test COMMAND tier2_provenance_test)

add_executable(native_path_test tests/native_path_test.cpp)
target_link_libraries(native_path_test PRIVATE state)
target_compile_options(native_path_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME native_path_test COMMAND native_path_test)

add_executable(tier2_frame_test tests/tier2_frame_test.cpp)
target_link_libraries(tier2_frame_test PRIVATE state)
target_compile_options(tier2_frame_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME tier2_frame_test COMMAND tier2_frame_test)

add_executable(coalescing_test query/tests/coalescing_test.cpp)
target_link_libraries(coalescing_test PRIVATE state)
target_compile_options(coalescing_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME coalescing_test COMMAND coalescing_test)

add_executable(generation_test query/tests/generation_test.cpp)
target_link_libraries(generation_test PRIVATE state)
target_compile_options(generation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME generation_test COMMAND generation_test)

add_executable(retry_test query/tests/retry_test.cpp)
target_link_libraries(retry_test PRIVATE state)
target_compile_options(retry_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME retry_test COMMAND retry_test)

add_executable(query_cache_test query/tests/query_cache_test.cpp)
target_link_libraries(query_cache_test PRIVATE state)
target_compile_options(query_cache_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME query_cache_test COMMAND query_cache_test)

add_executable(snapshot_migration_test persist/tests/snapshot_migration_test.cpp)
target_link_libraries(snapshot_migration_test PRIVATE state)
target_compile_options(snapshot_migration_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME snapshot_migration_test COMMAND snapshot_migration_test)

add_executable(corruption_test persist/tests/corruption_test.cpp)
target_link_libraries(corruption_test PRIVATE state)
target_compile_options(corruption_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME corruption_test COMMAND corruption_test)

add_executable(rehydrate_path_test persist/tests/rehydrate_path_test.cpp)
target_link_libraries(rehydrate_path_test PRIVATE state)
target_compile_options(rehydrate_path_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME rehydrate_path_test COMMAND rehydrate_path_test)

add_executable(restore_test persist/tests/restore_test.cpp)
target_link_libraries(restore_test PRIVATE state)
target_compile_options(restore_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME restore_test COMMAND restore_test)

add_executable(queue_durability_test sync/tests/queue_durability_test.cpp)
target_link_libraries(queue_durability_test PRIVATE state)
target_compile_options(queue_durability_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME queue_durability_test COMMAND queue_durability_test)

add_executable(crdt_property_test sync/tests/crdt_property_test.cpp)
target_link_libraries(crdt_property_test PRIVATE state)
target_compile_options(crdt_property_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME crdt_property_test COMMAND crdt_property_test)

add_executable(epoch_test sync/tests/epoch_test.cpp)
target_link_libraries(epoch_test PRIVATE state)
target_compile_options(epoch_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME epoch_test COMMAND epoch_test)

add_executable(background_sync_test sync/tests/background_sync_test.cpp)
target_link_libraries(background_sync_test PRIVATE state)
target_compile_options(background_sync_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME background_sync_test COMMAND background_sync_test)

add_executable(convergence_test sync/tests/convergence_test.cpp)
target_link_libraries(convergence_test PRIVATE state)
target_compile_options(convergence_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME convergence_test COMMAND convergence_test)

add_executable(time_travel_test devtools/tests/time_travel_test.cpp)
target_link_libraries(time_travel_test PRIVATE state)
target_compile_options(time_travel_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(time_travel_test PRIVATE MUSE_STATE_DEVTOOLS=1)
add_test(NAME time_travel_test COMMAND time_travel_test)

add_executable(disabled_overhead_test devtools/tests/disabled_overhead_test.cpp)
target_link_libraries(disabled_overhead_test PRIVATE state)
target_compile_options(disabled_overhead_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(disabled_overhead_test PRIVATE MUSE_STATE_DEVTOOLS=0)
add_test(NAME disabled_overhead_test COMMAND disabled_overhead_test)

# Release-artifact check (TC-STATE-033): devtools.cpp compiled as release
# (MUSE_STATE_DEVTOOLS=0) must expose no DevTimeline/Snapshot/Dag symbols.
add_test(NAME devtools_disabled_no_symbols
  COMMAND sh -c "c++ -std=c++17 -DMUSE_STATE_DEVTOOLS=0 -I ${CMAKE_CURRENT_SOURCE_DIR}/include -I ${CMAKE_CURRENT_SOURCE_DIR}/devtools/include -c ${CMAKE_CURRENT_SOURCE_DIR}/devtools/src/devtools.cpp -o ${CMAKE_CURRENT_BINARY_DIR}/devtools_disabled_check.o && (nm -C ${CMAKE_CURRENT_BINARY_DIR}/devtools_disabled_check.o | grep -E 'DevTimeline|Snapshot|exportDag' && exit 1 || exit 0)")
