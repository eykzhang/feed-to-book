cmake_minimum_required(VERSION 3.25)
project(feed_to_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FTB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FTB_NATIVE "Tune for the build machine's CPU" OFF)

# Sanitizers and CPU tuning apply to everything, dependencies included, so
# GoogleTest and Google Benchmark are built the same way as our code.
if(FTB_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

if(FTB_NATIVE)
  # Same intent, different spelling: clang and gcc want -mcpu on arm64, -march on x86.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    add_compile_options(-mcpu=native)
  else()
    add_compile_options(-march=native)
  endif()
endif()

# Warnings apply to our targets only. Link ftb_warnings into every target we own.
add_library(ftb_warnings INTERFACE)
target_compile_options(ftb_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)

include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
  URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
  SYSTEM)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
  URL_HASH SHA256=9631341c82bac4a288bef951f8b26b41f69021794184ece969f8473977eaa340
  SYSTEM)

FetchContent_MakeAvailable(googletest benchmark)

enable_testing()
add_subdirectory(tests)
add_subdirectory(bench)
