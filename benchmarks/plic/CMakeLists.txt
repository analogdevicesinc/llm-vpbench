cmake_minimum_required(VERSION 3.14)
project(plic_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED ENV{SYSTEMC_HOME})
    message(FATAL_ERROR "Set SYSTEMC_HOME environment variable")
endif()
set(SYSTEMC_HOME $ENV{SYSTEMC_HOME})

if(NOT DEFINED SUBMISSION_DIR)
    set(SUBMISSION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/golden)
endif()

include_directories(${SYSTEMC_HOME}/include)
include_directories(${SUBMISSION_DIR})

if(EXISTS ${SYSTEMC_HOME}/lib-linux64)
    link_directories(${SYSTEMC_HOME}/lib-linux64)
elseif(EXISTS ${SYSTEMC_HOME}/lib)
    link_directories(${SYSTEMC_HOME}/lib)
endif()

add_executable(plic_testbench
    ${CMAKE_CURRENT_SOURCE_DIR}/testbench/plic_testbench.cpp
)

target_include_directories(plic_testbench PRIVATE ${SUBMISSION_DIR})
target_link_libraries(plic_testbench PRIVATE systemc)

enable_testing()
add_test(NAME plic_test COMMAND plic_testbench)
