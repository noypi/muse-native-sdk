cmake_minimum_required(VERSION 3.16)
project(muse_net CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(net
  src/transport.cc
)
target_include_directories(net PUBLIC include)
target_compile_options(net PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

# NOTE (T1): no test executables are registered yet. The shared contract
# test (tests/transport_api_test.cpp) includes net/http_client.h and
# net/interceptor.h, which are T2-owned headers not yet landed, so it
# cannot compile in this slice; tests/http_dispatch_test.cpp needs the
# T3-owned RecordDispatchNanos/WithinBudget probe definitions. The planner
# routes test-target registration (plus the T2 shim headers) with the
# T2/T3 dispatches.
