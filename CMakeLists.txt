cmake_minimum_required(VERSION 3.14)

project(Perun VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(OpenGL REQUIRED)

# GLAD (OpenGL Loader) - Fetching a CMake-ready version or header-only
# For simplicity, we will fetch a repo that provides GLAD
include(FetchContent)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.34
)
FetchContent_MakeAvailable(glad)

# GoogleTest
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Library Source Files
file(GLOB_RECURSE SOURCES 
    "src/Core/*.cpp"
    "src/Graphics/*.cpp"
    "src/Math/*.cpp"
)

# Main Library Target
add_library(Perun ${SOURCES})

target_include_directories(Perun PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
)

# Robust linking: use targets if available, else variables
if(TARGET SDL2::SDL2)
    target_link_libraries(Perun PUBLIC SDL2::SDL2)
else()
    target_link_libraries(Perun PUBLIC ${SDL2_LIBRARIES})
endif()

if(TARGET SDL2_image::SDL2_image)
    target_link_libraries(Perun PUBLIC SDL2_image::SDL2_image)
elseif(TARGET SDL2::SDL2_image)
    target_link_libraries(Perun PUBLIC SDL2::SDL2_image)
else()
    target_link_libraries(Perun PUBLIC ${SDL2_IMAGE_LIBRARIES})
endif()

target_link_libraries(Perun PUBLIC OpenGL::GL glad)

# Testing
enable_testing()

add_executable(UnitTests 
    tests/unit/Vector2Tests.cpp
    tests/unit/Matrix4Tests.cpp
    tests/unit/RendererTests.cpp
    tests/unit/FramebufferTests.cpp
    tests/unit/TextureTests.cpp
)

target_link_libraries(UnitTests PRIVATE Perun GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(UnitTests)

# Example App
add_executable(PerunSandbox examples/main.cpp)
target_link_libraries(PerunSandbox PRIVATE Perun)

# Installation
# include(GNUInstallDirs)

# install(TARGETS Perun
#     EXPORT PerunTargets
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )

# install(DIRECTORY include/Perun
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

# install(EXPORT PerunTargets
#     FILE PerunTargets.cmake
#     NAMESPACE Perun::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Perun
# )
