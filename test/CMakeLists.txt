add_executable(ComPtr_test ComPtr_test.cpp)
target_link_libraries(ComPtr_test gtest_main gmock_main)
add_test(NAME ComPtr_test COMMAND ComPtr_test)
