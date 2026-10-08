cmake_minimum_required(VERSION 3.16)
project(jamwarden_core CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(jamwarden_core src/ubx.cpp src/messages.cpp src/detector.cpp)
target_include_directories(jamwarden_core PUBLIC include)

add_executable(jamwarden_replay tools/replay.cpp)
target_link_libraries(jamwarden_replay jamwarden_core)

add_executable(jamwarden_tests tests/test_main.cpp)
target_link_libraries(jamwarden_tests jamwarden_core)

enable_testing()
add_test(NAME unit COMMAND jamwarden_tests)
