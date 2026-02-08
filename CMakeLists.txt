cmake_minimum_required(VERSION 3.14)

project(Perun VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(PkgConfig REQUIRED)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


# Library Source Files
file(GLOB_RECURSE SOURCES 
    "src/Protocol/*.cpp"
    "src/Transport/*.cpp"
)

# Add Server.cpp but not main.cpp
list(APPEND SOURCES "src/Server/Server.cpp")

# Main Library Target
add_library(Perun ${SOURCES})

target_include_directories(Perun PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Testing
enable_testing()
include(GoogleTest)

# Protocol Tests
add_executable(ProtocolTests 
    tests/unit/protocol/PacketsTests.cpp
    tests/unit/protocol/HandshakeTests.cpp
)
target_link_libraries(ProtocolTests PRIVATE Perun GTest::gtest_main)
gtest_discover_tests(ProtocolTests)

# Transport Tests
add_executable(TransportTests 
    tests/unit/transport/UnixTransportTests.cpp
    tests/unit/transport/TCPTransportTests.cpp
)
target_link_libraries(TransportTests PRIVATE Perun GTest::gtest_main)
gtest_discover_tests(TransportTests)

# IPC Server (Relay)
add_executable(perun-server src/Server/main.cpp)
target_link_libraries(perun-server PRIVATE Perun)
target_link_libraries(perun-server PRIVATE rt) # Realtime library for shm/timers if needed

# Python Conformance Tests
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME ConformanceTests
             COMMAND ${Python3_EXECUTABLE}   "${CMAKE_SOURCE_DIR}/tests/conformance/conformance_suite.py"
             WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(ConformanceTests PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}/sdk/python")
endif()
