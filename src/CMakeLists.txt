# The feed handler and book: every hand-written component builds into this one library.
add_library(ftb
  framing.cpp)
target_include_directories(ftb PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(ftb PRIVATE ftb_warnings)
