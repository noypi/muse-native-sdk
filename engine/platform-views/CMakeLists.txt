cmake_minimum_required(VERSION 3.16)
project(muse_pviews CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-views native sources (NFR-PVIEWS-001 / FR-PVIEWS-001).
add_library(pviews
  src/external_memory_probe.cpp
  src/z_order.cpp
)
target_include_directories(pviews PUBLIC include)
target_compile_options(pviews PRIVATE -Wall -Wextra -Wpedantic)

# Brownfield embed target (FR-PVIEWS-003 / AC-PVIEWS-004).
add_library(pviews-embed
  src/embed.cpp
)
target_include_directories(pviews-embed PUBLIC include)
target_compile_options(pviews-embed PRIVATE -Wall -Wextra -Wpedantic)

# Compose interop target (FR-PVIEWS-009 / AC-PVIEWS-009 / AC-PVIEWS-010).
add_library(pviews-interop
  src/interop.cpp
)
target_include_directories(pviews-interop PUBLIC include)
target_compile_options(pviews-interop PRIVATE -Wall -Wextra -Wpedantic)

# Brownfield host target (FR-PVIEWS-010 / AC-PVIEWS-013).
add_library(pviews-brownfield
  src/brownfield.cpp
)
target_include_directories(pviews-brownfield PUBLIC include)
target_compile_options(pviews-brownfield PRIVATE -Wall -Wextra -Wpedantic)

# Semantics merge target (FR-PVIEWS-009 / AC-PVIEWS-011).
add_library(pviews-semantics
  src/semantics.cpp
)
target_include_directories(pviews-semantics PUBLIC include)
target_compile_options(pviews-semantics PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

add_executable(external_memory_test tests/external_memory_test.cpp)
target_link_libraries(external_memory_test PRIVATE pviews)
target_compile_options(external_memory_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME external_memory COMMAND external_memory_test)

add_executable(z_order_test tests/z_order_test.cpp)
target_link_libraries(z_order_test PRIVATE pviews)
target_compile_options(z_order_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME z_order COMMAND z_order_test)

add_executable(brownfield_embed_test tests/brownfield_embed_test.cpp)
target_link_libraries(brownfield_embed_test PRIVATE pviews-embed)
target_compile_options(brownfield_embed_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME brownfield_embed COMMAND brownfield_embed_test)

add_executable(compose_interop_test tests/compose_interop_test.cpp)
target_link_libraries(compose_interop_test PRIVATE pviews-interop)
target_compile_options(compose_interop_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME compose_interop COMMAND compose_interop_test)

add_executable(semantics_merge_test tests/semantics_merge_test.cpp)
target_link_libraries(semantics_merge_test PRIVATE pviews-semantics)
target_compile_options(semantics_merge_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME semantics_merge COMMAND semantics_merge_test)

add_executable(brownfield_host_test tests/brownfield_host_test.cpp)
target_link_libraries(brownfield_host_test PRIVATE pviews-brownfield pviews-embed)
target_compile_options(brownfield_host_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME brownfield_host COMMAND brownfield_host_test)

add_executable(shared_engine_teardown_test tests/shared_engine_teardown_test.cpp)
target_link_libraries(shared_engine_teardown_test PRIVATE pviews-brownfield pviews-embed)
target_compile_options(shared_engine_teardown_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME shared_engine_teardown COMMAND shared_engine_teardown_test)
