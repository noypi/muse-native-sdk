cmake_minimum_required(VERSION 3.16)
project(muse_net CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(net
  src/transport.cc
  src/rpc.cc
  src/sse.cc
  src/graphql.cc
  src/http_cache.cc
  src/retry.cc
  src/resilience.cc
  src/websocket.cc
  src/webtransport.cc
  src/webrtc.cc
  src/codec.cc
  src/codec_json.cc
  src/codec_cbor.cc
  src/codec_msgpack.cc
  src/codec_protobuf.cc
  src/zero_copy.cc
)
target_include_directories(net PUBLIC include)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# One test executable per tests/*_test.cpp, linked against `net`, so the
# plan's `cmake -S engine/net -B <dir> && cmake --build <dir> &&
# ctest --test-dir <dir>` path runs all six suites.
foreach(TEST_SRC
    tests/transport_api_test.cpp
    tests/http_dispatch_test.cpp
    tests/http_cache_rfc7234_test.cpp
    tests/cache_ownership_test.cpp
    tests/retry_idempotency_test.cpp
    tests/resilience_test.cpp
    tests/websocket_test.cpp
    tests/webtransport_test.cpp
    tests/webrtc_session_test.cpp
    tests/rtc_harness_test.cpp
    tests/grpc_interop_test.cpp
    tests/graphql_subscription_test.cpp
    tests/codec_roundtrip_test.cpp
    tests/codec_fuzz_test.cpp
    tests/zero_copy_payload_test.cpp)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  add_executable(${TEST_NAME} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE net)
  target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()

# T8 fuzz gate: sanitizers best-effort only — never break the normal build.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fsanitize=address,undefined" HAVE_ASAN_UBSAN)
if(HAVE_ASAN_UBSAN)
  foreach(SAN_TARGET codec_fuzz_test zero_copy_payload_test)
    if(TARGET ${SAN_TARGET})
      target_compile_options(${SAN_TARGET} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${SAN_TARGET} PRIVATE -fsanitize=address,undefined)
    endif()
  endforeach()
endif()
