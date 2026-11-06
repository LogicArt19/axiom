cmake_minimum_required(VERSION 3.20)
project(AXIOM VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
set(AXIOM_VERSION "${PROJECT_VERSION}" CACHE STRING "Version string baked into the binaries")
set(AXIOM_REPO "" CACHE STRING "GitHub repository (owner/name) used by the launcher for updates")
option(AXIOM_BUILD_TESTS "Build the headless simulation test" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
if(MSVC)
    # static CRT: the zip runs on any Windows 10/11 machine without redistributables
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# raylib 5.5 (OpenGL 3.3 desktop, static)
# ---------------------------------------------------------------------------
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPENGL_VERSION "3.3" CACHE STRING "" FORCE)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

function(axiom_flags target)
    target_compile_definitions(${target} PRIVATE AXIOM_VERSION_STR="${AXIOM_VERSION}" AXIOM_REPO="${AXIOM_REPO}")
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3 /O2 /fp:fast /wd4244 /wd4267 /wd4305 /wd4996 /bigobj)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -O2 -Wall -Wno-missing-field-initializers)
    endif()
endfunction()

set(SIM_SOURCES
    src/world.cpp src/map.cpp src/weapons.cpp src/bot.cpp src/config.cpp src/net.cpp src/net_socket.cpp)

# ---------------------------------------------------------------------------
# Game client
# ---------------------------------------------------------------------------
add_executable(AxiomGame
    src/main.cpp src/game.cpp src/render.cpp src/hud.cpp src/ui.cpp src/audio.cpp ${SIM_SOURCES})
target_link_libraries(AxiomGame PRIVATE raylib)
axiom_flags(AxiomGame)

# ---------------------------------------------------------------------------
# Dedicated server (headless)
# ---------------------------------------------------------------------------
add_executable(AxiomServer server/server_main.cpp ${SIM_SOURCES})
target_link_libraries(AxiomServer PRIVATE raylib)
axiom_flags(AxiomServer)

# ---------------------------------------------------------------------------
# Launcher / updater
# ---------------------------------------------------------------------------
add_executable(AxiomLauncher launcher/launcher.cpp launcher/proc.cpp)
target_link_libraries(AxiomLauncher PRIVATE raylib)
axiom_flags(AxiomLauncher)

if(WIN32)
    target_link_libraries(AxiomGame PRIVATE ws2_32 winmm)
    target_link_libraries(AxiomServer PRIVATE ws2_32 winmm)
    target_link_libraries(AxiomLauncher PRIVATE winmm shell32)
    # no console window for the GUI programs
    set_target_properties(AxiomGame AxiomLauncher PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(AxiomGame PRIVATE /ENTRY:mainCRTStartup)
        target_link_options(AxiomLauncher PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(AXIOM_BUILD_TESTS)
    add_executable(AxiomSimTest tests/simtest.cpp src/world.cpp src/map.cpp src/weapons.cpp src/bot.cpp src/config.cpp)
    target_link_libraries(AxiomSimTest PRIVATE raylib)
    axiom_flags(AxiomSimTest)
    if(WIN32)
        target_link_libraries(AxiomSimTest PRIVATE winmm)
    endif()
endif()
