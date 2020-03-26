cmake_minimum_required(VERSION 3.0)
project(libIME2)

enable_testing()

# http://www.utf8everywhere.org/
add_definitions(
    /D_UNICODE=1
    /DUNICODE=1
)

set(CMAKE_CXX_STANDARD 14)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# This requires newer C++ compilers and does not work with VC++ 2015.
add_subdirectory(lib/googletest-release-1.10.0)
add_subdirectory(src)
add_subdirectory(test)
