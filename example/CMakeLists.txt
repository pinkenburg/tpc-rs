cmake_minimum_required(VERSION 3.6 FATAL_ERROR)

project(TPCRS-EXAMPLE LANGUAGES CXX)

# Request a compiler with c++11 support
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(tpcrs)

add_executable(simple simple.cpp)
target_link_libraries(simple tpcrs)
