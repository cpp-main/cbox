cmake_minimum_required(VERSION 3.15)
add_executable(log_example log_example.c)
target_link_libraries(log_example cbox_base pthread)
