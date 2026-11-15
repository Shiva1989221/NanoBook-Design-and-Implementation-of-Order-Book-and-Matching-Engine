cmake_minimum_required(VERSION 3.14)
project(NanoBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build (benchmarks on a Debug build are meaningless).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The engine lives in a library so the executable, the tests (step 4)
# and the benchmark (step 5) can all link against the same code.
add_library(nanobook_lib
  src/order_book.cpp
  src/csv_io.cpp
  src/workload.cpp
  src/snapshot_log.cpp
)
target_include_directories(nanobook_lib PUBLIC include)
if(MSVC)
  target_compile_options(nanobook_lib PUBLIC /W4)
else()
  target_compile_options(nanobook_lib PUBLIC -Wall -Wextra)
endif()

add_executable(nanobook src/main.cpp)
target_link_libraries(nanobook PRIVATE nanobook_lib)

# Benchmark: generates synthetic orders and measures throughput / latency.
add_executable(nanobook_bench src/bench.cpp)
target_link_libraries(nanobook_bench PRIVATE nanobook_lib)

# ---- Tests (build with -DNANOBOOK_BUILD_TESTS=OFF to skip) ----
option(NANOBOOK_BUILD_TESTS "Build the unit tests" ON)
if(NANOBOOK_BUILD_TESTS)
  enable_testing()

  add_executable(test_order_book tests/test_order_book.cpp)
  target_link_libraries(test_order_book PRIVATE nanobook_lib)
  add_test(NAME order_book COMMAND test_order_book)

  add_executable(test_csv_io tests/test_csv_io.cpp)
  target_link_libraries(test_csv_io PRIVATE nanobook_lib)
  add_test(NAME csv_io COMMAND test_csv_io)

  add_executable(test_workload tests/test_workload.cpp)
  target_link_libraries(test_workload PRIVATE nanobook_lib)
  add_test(NAME workload COMMAND test_workload)

  add_executable(test_pool_allocator tests/test_pool_allocator.cpp)
  target_link_libraries(test_pool_allocator PRIVATE nanobook_lib)
  add_test(NAME pool_allocator COMMAND test_pool_allocator)

  add_executable(test_flat_id_map tests/test_flat_id_map.cpp)
  target_link_libraries(test_flat_id_map PRIVATE nanobook_lib)
  add_test(NAME flat_id_map COMMAND test_flat_id_map)

  add_executable(test_snapshot_log tests/test_snapshot_log.cpp)
  target_link_libraries(test_snapshot_log PRIVATE nanobook_lib)
  add_test(NAME snapshot_log COMMAND test_snapshot_log)
endif()
