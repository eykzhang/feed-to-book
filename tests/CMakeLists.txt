add_executable(ftb_tests framing_test.cpp)
target_link_libraries(ftb_tests PRIVATE ftb ftb_warnings GTest::gtest_main)
target_compile_definitions(ftb_tests PRIVATE FTB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

include(GoogleTest)
# PRE_TEST: list tests when ctest runs, not as a post-build step.
gtest_discover_tests(ftb_tests DISCOVERY_MODE PRE_TEST)
