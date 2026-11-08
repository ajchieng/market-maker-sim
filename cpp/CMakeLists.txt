cmake_minimum_required(VERSION 3.24)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)  # benchmarks are meaningless in Debug
endif()

include(FetchContent)
FetchContent_Declare(simdjson
  URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.10.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(simdjson Catch2)
find_package(ZLIB REQUIRED)

add_library(lob
  src/decimal.cpp
  src/feed.cpp
  src/gz_lines.cpp
  src/order_book.cpp)
target_include_directories(lob PUBLIC include)
target_link_libraries(lob PUBLIC simdjson ZLIB::ZLIB)
target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic)

add_executable(lob_replay src/replay_main.cpp)
target_link_libraries(lob_replay PRIVATE lob)

add_executable(lob_bench src/bench_main.cpp)
target_link_libraries(lob_bench PRIVATE lob)

add_executable(lob_tests tests/test_feed.cpp tests/test_order_book.cpp)
target_link_libraries(lob_tests PRIVATE lob Catch2::Catch2WithMain)
target_compile_definitions(lob_tests PRIVATE
  LOB_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/kraken_sample.jsonl.gz")

enable_testing()
include(Catch)
catch_discover_tests(lob_tests)
