cmake_minimum_required(VERSION 3.24)

project(
    SM64Ultimate
    VERSION 1.0.0
    DESCRIPTION "SM64 Ultimate Save Editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "Build SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG 0.1.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL2 imgui)

FetchContent_GetProperties(pfd)

if(NOT pfd_POPULATED)
    FetchContent_Populate(pfd)
endif()

find_package(OpenGL REQUIRED)

add_library(
    sm64_imgui
    STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(
    sm64_imgui
    PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(
    sm64_imgui
    PUBLIC
    SDL2::SDL2
    OpenGL::GL
)

add_executable(
    sm64-ultimate
    main.cpp
    SM64SystemCore.cpp
    SM64SystemCore.hpp
    SM64HexEditorUI.cpp
    SM64HexEditorUI.hpp
)

target_include_directories(
    sm64-ultimate
    PRIVATE
    ${pfd_SOURCE_DIR}
)

target_link_libraries(
    sm64-ultimate
    PRIVATE
    sm64_imgui
    SDL2::SDL2
    OpenGL::GL
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(
        sm64-ultimate
        PRIVATE
        SDL2::SDL2main
    )
endif()

if(MSVC)
    target_compile_options(
        sm64-ultimate
        PRIVATE
        /W4
        /permissive-
        /EHsc
    )
else()
    target_compile_options(
        sm64-ultimate
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

install(
    TARGETS sm64-ultimate
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
