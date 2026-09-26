add_executable(ftb_tests smoke_test.cpp)
target_link_libraries(ftb_tests PRIVATE ftb_warnings GTest::gtest_main)

include(GoogleTest)
# PRE_TEST: list tests when ctest runs, not as a post-build step.
gtest_discover_tests(ftb_tests DISCOVERY_MODE PRE_TEST)
