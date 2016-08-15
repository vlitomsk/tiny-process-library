cmake_minimum_required(VERSION 2.8.12)

project(tiny-process-library)

#######
include(CheckCXXCompilerFlag)
if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Wextra -Wno-unused-parameter ")
endif()

find_package(Threads REQUIRED)

set(process_source_files process.cpp)

if(WIN32)
  list(APPEND process_source_files process_win.cpp)
  #If compiled using MSYS2, use sh to run commands
  if(MSYS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DMSYS_PROCESS_USE_SH")
  endif()
else()
  list(APPEND process_source_files process_unix.cpp)
endif()

set(LIB_OLD ${PROJECT_NAME}.oldcxx11abi)
set(LIB_NEW ${PROJECT_NAME}.newcxx11abi)

add_library(${LIB_OLD} ${process_source_files})
target_link_libraries(${LIB_OLD} ${CMAKE_THREAD_LIBS_INIT})
target_compile_options(${LIB_OLD} PRIVATE "-D_GLIBCXX_USE_CXX11_ABI=0")

message(STATUS "building old+new cxx11 abi versions")
add_library(${LIB_NEW} ${process_source_files})
target_link_libraries(${LIB_NEW} ${CMAKE_THREAD_LIBS_INIT})
target_compile_options(${LIB_NEW} PRIVATE "-D_GLIBCXX_USE_CXX11_ABI=1")

# To enable tests: cmake -DENABLE_TESTING=1 .
if(ENABLE_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
