cmake_minimum_required(VERSION 3.5)

set (CMAKE_CXX_STANDARD 17)

project (cppinvert_test)

include(build/BoostTestHelpers.cmake)

include_directories(.)
enable_testing ()
add_subdirectory (test)

