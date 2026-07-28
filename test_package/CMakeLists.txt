cmake_minimum_required(VERSION 3.24)
project(rdp_sdk_cpp_test_package LANGUAGES CXX)

find_package(RdpSdkCpp CONFIG REQUIRED)

enable_testing()

add_executable(test_package src/test_package.cpp)
target_link_libraries(test_package PRIVATE Rdp::Sdk)

add_test(NAME test_package COMMAND test_package)
