#find_package (Boost COMPONENTS system filesystem unit_test_framework REQUIRED)
include_directories (../src)

set (Test "cppinvert_test")
add_executable (${Test} test.cpp TestIocContainer.cpp)
target_link_libraries (${Test} ${CONAN_LIBS})
add_test (NAME ${Test} COMMAND Test)

#add_library(cppinvert_testlib STATIC test/TestMain.cpp)
#add_executable(cppinvert_test test/TestIocContainer.cpp test/TestMain.cpp)
#add_boost_test(test/TestIocContainer.cpp cppinvert_testlib)
