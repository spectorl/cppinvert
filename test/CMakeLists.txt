find_package (Boost COMPONENTS system filesystem unit_test_framework REQUIRED)
include_directories (${TEST_SOURCE_DIR}/src
                     ${Boost_INCLUDE_DIRS}
                     )

set (Test "cppinvert_test")
add_definitions (-DBOOST_TEST_DYN_LINK)
add_executable (${Test} test.cpp TestIocContainer.cpp)
target_link_libraries (${Test}
                       ${Boost_FILESYSTEM_LIBRARY}
                       ${Boost_SYSTEM_LIBRARY}
                       ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}
                       )
add_test (NAME ${Test} COMMAND Test)

#add_library(cppinvert_testlib STATIC test/TestMain.cpp)
#add_executable(cppinvert_test test/TestIocContainer.cpp test/TestMain.cpp)
#add_boost_test(test/TestIocContainer.cpp cppinvert_testlib)
