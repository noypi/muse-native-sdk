cmake_minimum_required(VERSION 3.16)
project(muse_core_thread CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(core_thread
  src/dispatcher.cpp
  src/worker_pool.cpp
)
target_include_directories(core_thread PUBLIC include)
target_compile_options(core_thread PRIVATE -Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)
target_link_libraries(core_thread PUBLIC Threads::Threads)

include(CTest)
enable_testing()

add_executable(main_thread_test tests/main_thread_test.cpp)
target_link_libraries(main_thread_test PRIVATE core_thread)
target_compile_options(main_thread_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME main_thread_test COMMAND main_thread_test)

add_executable(dispatcher_test tests/dispatcher_test.cpp)
target_link_libraries(dispatcher_test PRIVATE core_thread)
target_compile_options(dispatcher_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME dispatcher_test COMMAND dispatcher_test)

add_executable(task_cancellation_test tests/task_cancellation_test.cpp)
target_link_libraries(task_cancellation_test PRIVATE core_thread)
target_compile_options(task_cancellation_test PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME task_cancellation_test COMMAND task_cancellation_test)
