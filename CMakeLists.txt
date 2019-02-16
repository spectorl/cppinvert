cmake_minimum_required(VERSION 3.5)

set (CMAKE_CXX_STANDARD 17)

project (cppinvert_test)

include(build/conan.cmake)
include(build/BoostTestHelpers.cmake)

conan_cmake_run(CONANFILE conan/conanfile.txt
    CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}"
    BASIC_SETUP CMAKE_TARGETS
    BUILD missing)

conan_basic_setup()

include_directories(.)
enable_testing ()
add_subdirectory (test)

