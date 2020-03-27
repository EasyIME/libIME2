include_directories(${PROJECT_SOURCE_DIR}/src)

add_executable(ComPtr_test ComPtr_test.cpp)
target_link_libraries(ComPtr_test gtest_main gmock_main)
add_test(NAME ComPtr_test COMMAND ComPtr_test)

add_executable(ComObject_test ComObject_test.cpp)
target_link_libraries(ComObject_test gtest_main gmock_main)
add_test(NAME ComObject_test COMMAND ComObject_test)
