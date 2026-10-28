cmake_minimum_required(VERSION 3.16)
project(muse_core_budget CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(core_budget
  src/manager.cpp
)
target_include_directories(core_budget PUBLIC include)
target_compile_options(core_budget PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
enable_testing()

add_executable(budget_tiers_test tests/budget_tiers_test.cpp)
target_link_libraries(budget_tiers_test PRIVATE core_budget)
target_compile_options(budget_tiers_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME budget_tiers_test COMMAND budget_tiers_test)
