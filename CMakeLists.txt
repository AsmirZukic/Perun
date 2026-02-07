cmake_minimum_required(VERSION 3.14)

project(Perun VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
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

# Dear ImGui
# We need to expose SDL2 and OpenGL3 backend files
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.4-docking
)
FetchContent_MakeAvailable(imgui)


# Library Source Files
file(GLOB_RECURSE SOURCES 
    "src/Core/*.cpp"
    "src/Graphics/*.cpp"
    "src/Math/*.cpp"
    "src/ImGui/*.cpp"
    "src/Audio/*.cpp"
    "src/C/*.cpp"
)

# Main Library Target
add_library(Perun 
    ${SOURCES}
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(Perun PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)

# Link SDL2 and SDL2_image using pkg-config variables
target_link_libraries(Perun PUBLIC ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})

target_link_libraries(Perun PUBLIC OpenGL::GL glad)

# Testing
enable_testing()

add_executable(UnitTests 
    tests/unit/RendererTests.cpp
    tests/unit/FramebufferTests.cpp
    tests/unit/TextureTests.cpp
    tests/unit/AudioTests.cpp
)

target_link_libraries(UnitTests PRIVATE Perun GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(UnitTests)



# Installation
# include(GNUInstallDirs)

# install(TARGETS Perun
#     EXPORT PerunTargets
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
# )
# IPC Server (Universal Frontend)
add_executable(perun-server src/Server/main.cpp)
target_link_libraries(perun-server PRIVATE Perun)
target_link_libraries(perun-server PRIVATE rt) # Realtime library for shm

# install(DIRECTORY include/Perun
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

# C-API Verification
add_executable(PerunCTest tests/c_shim/test.c)
target_link_libraries(PerunCTest PRIVATE Perun)
set_target_properties(PerunCTest PROPERTIES LINKER_LANGUAGE CXX) # Link C++ stdlib

