cmake_minimum_required(VERSION 3.24)

project(
    SM64Ultimate
    VERSION 2.0.0
    DESCRIPTION "SM64 Ultimate Save Editor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SM64_BUILD_GUI "Build the SDL2/OpenGL/Dear ImGui application" ON)
option(SM64_BUILD_TESTS "Build the automated tests" ON)
option(SM64_SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(SM64_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Warning / sanitizer flags shared by every target
# ---------------------------------------------------------------------------
add_library(sm64_flags INTERFACE)
if(MSVC)
    target_compile_options(sm64_flags INTERFACE /W4 /permissive- /EHsc /utf-8)
    if(SM64_WARNINGS_AS_ERRORS)
        target_compile_options(sm64_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(sm64_flags INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(SM64_WARNINGS_AS_ERRORS)
        target_compile_options(sm64_flags INTERFACE -Werror)
    endif()
    if(SM64_SANITIZE)
        target_compile_options(sm64_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
                                                     -fno-sanitize-recover=undefined)
        target_link_options(sm64_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()
if(WIN32)
    # Windows headers must never define min/max macros (they break std::min/std::max).
    target_compile_definitions(sm64_flags INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# The engine (no GUI dependencies)
# ---------------------------------------------------------------------------
add_library(
    sm64_core
    STATIC
    SM64SaveFormat.hpp
    SM64SystemCore.hpp
    SM64SystemCore.cpp
    SM64FileIO.cpp
    SM64Export.cpp
    SM64Describe.cpp
    SM64Presets.hpp
    SM64Presets.cpp
    SM64Report.hpp
    SM64Report.cpp
    SM64Settings.hpp
    SM64Settings.cpp
)
target_include_directories(sm64_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm64_core PUBLIC sm64_flags)

# ---------------------------------------------------------------------------
# Dear ImGui (pinned). Used by the GUI and by the headless ID-conflict test.
# ---------------------------------------------------------------------------
if(SM64_BUILD_GUI OR SM64_BUILD_TESTS)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(imgui)

    set(SM64_IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )

    # Core ImGui without any platform backend (safe to run headless).
    add_library(sm64_imgui_core STATIC ${SM64_IMGUI_SOURCES})
    target_include_directories(sm64_imgui_core SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    if(SM64_SANITIZE AND NOT MSVC)
        target_compile_options(sm64_imgui_core PRIVATE -fsanitize=address,undefined)
        target_link_options(sm64_imgui_core PUBLIC -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# The application UI as a library (ImGui only, no SDL) so tests can drive it.
# ---------------------------------------------------------------------------
set(SM64_APP_SOURCES
    SM64App.hpp SM64App.cpp SM64Pages.cpp SM64Pages2.cpp SM64PageUtil.hpp SM64Theme.hpp SM64Theme.cpp)

if(SM64_BUILD_GUI)
    add_library(sm64_app STATIC ${SM64_APP_SOURCES})
    target_include_directories(sm64_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sm64_app PUBLIC sm64_core sm64_imgui_core sm64_flags)
endif()

if(SM64_BUILD_TESTS)
    # Dear ImGui built with its item hook enabled and assertions kept on, so tests
    # can see every widget ID submitted in a frame and ImGui misuse aborts loudly.
    add_library(sm64_imgui_audit STATIC ${SM64_IMGUI_SOURCES})
    target_include_directories(sm64_imgui_audit SYSTEM PUBLIC ${imgui_SOURCE_DIR})
    target_compile_definitions(sm64_imgui_audit PUBLIC IMGUI_ENABLE_TEST_ENGINE)
    if(MSVC)
        target_compile_options(sm64_imgui_audit PRIVATE /UNDEBUG)
    else()
        target_compile_options(sm64_imgui_audit PRIVATE -UNDEBUG)
    endif()
    if(SM64_SANITIZE AND NOT MSVC)
        target_compile_options(sm64_imgui_audit PRIVATE -fsanitize=address,undefined)
        target_link_options(sm64_imgui_audit PUBLIC -fsanitize=address,undefined)
    endif()

    add_library(sm64_app_audit STATIC ${SM64_APP_SOURCES})
    target_include_directories(sm64_app_audit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(sm64_app_audit PUBLIC sm64_core sm64_imgui_audit sm64_flags)
endif()

# ---------------------------------------------------------------------------
# GUI executable
# ---------------------------------------------------------------------------
if(SM64_BUILD_GUI)
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
    # portable-file-dialogs is header-only and ships an outdated CMake project, so
    # only its headers are fetched: SOURCE_SUBDIR points at a directory without a
    # CMakeLists.txt, which makes MakeAvailable download but not configure it.
    FetchContent_Declare(
        pfd
        GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
        GIT_TAG 0.1.0
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR sm64-no-cmake-project
    )
    FetchContent_MakeAvailable(SDL2 pfd)

    find_package(OpenGL REQUIRED)

    add_library(
        sm64_imgui_backends
        STATIC
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(sm64_imgui_backends SYSTEM PUBLIC ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(sm64_imgui_backends PUBLIC sm64_imgui_core SDL2::SDL2 OpenGL::GL)
    if(SM64_SANITIZE AND NOT MSVC)
        target_compile_options(sm64_imgui_backends PRIVATE -fsanitize=address,undefined)
    endif()

    add_executable(sm64-ultimate main.cpp)
    target_include_directories(sm64-ultimate SYSTEM PRIVATE ${pfd_SOURCE_DIR})
    target_link_libraries(sm64-ultimate PRIVATE sm64_app sm64_imgui_backends SDL2::SDL2 OpenGL::GL sm64_flags)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(sm64-ultimate PRIVATE SDL2::SDL2main)
    endif()
    if(WIN32)
        # GUI subsystem (no console window) while keeping a standard main().
        set_target_properties(sm64-ultimate PROPERTIES WIN32_EXECUTABLE ON)
    endif()

    install(TARGETS sm64-ultimate RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    install(FILES README.md LICENSE DESTINATION . OPTIONAL)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SM64_BUILD_TESTS)
    enable_testing()

    add_executable(sm64-core-tests tests/test_core.cpp tests/test_report.cpp tests/test_settings.cpp tests/test_main.cpp)
    target_link_libraries(sm64-core-tests PRIVATE sm64_core sm64_flags)
    add_test(NAME core COMMAND sm64-core-tests)

    add_executable(sm64-ui-tests tests/test_ui_ids.cpp tests/test_app_flow.cpp tests/test_main.cpp)
    target_link_libraries(sm64-ui-tests PRIVATE sm64_app_audit sm64_flags)
    add_test(NAME ui-ids COMMAND sm64-ui-tests)
endif()
