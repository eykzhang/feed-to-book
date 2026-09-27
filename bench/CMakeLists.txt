add_executable(ftb_bench smoke_bench.cpp)
target_link_libraries(ftb_bench PRIVATE ftb ftb_warnings benchmark::benchmark_main)
