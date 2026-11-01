#include "SM64HexEditorUI.hpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0)
    {
        std::fprintf(
            stderr,
            "SDL_Init failed: %s\n",
            SDL_GetError()
        );

        return 1;
    }

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_FLAGS,
        0
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE
    );

#if defined(__APPLE__)
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_FLAGS,
        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MAJOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MINOR_VERSION,
        2
    );

    const char* glslVersion = "#version 150";
#else
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MAJOR_VERSION,
        3
    );

    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_MINOR_VERSION,
        2
    );

    const char* glslVersion = "#version 150";
#endif

    SDL_GL_SetAttribute(
        SDL_GL_DOUBLEBUFFER,
        1
    );

    SDL_GL_SetAttribute(
        SDL_GL_DEPTH_SIZE,
        24
    );

    SDL_GL_SetAttribute(
        SDL_GL_STENCIL_SIZE,
        8
    );

    SDL_Window* window = SDL_CreateWindow(
        "SM64 Ultimate Save Editor",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1400,
        850,
        SDL_WINDOW_OPENGL |
            SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (window == nullptr)
    {
        std::fprintf(
            stderr,
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError()
        );

        SDL_Quit();
        return 2;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);

    if (glContext == nullptr)
    {
        std::fprintf(
            stderr,
            "SDL_GL_CreateContext failed: %s\n",
            SDL_GetError()
        );

        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }

    if (SDL_GL_MakeCurrent(window, glContext) != 0)
    {
        std::fprintf(
            stderr,
            "SDL_GL_MakeCurrent failed: %s\n",
            SDL_GetError()
        );

        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 4;
    }

    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0F;
    style.ChildRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 6.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;

    style.WindowPadding = ImVec2(
        12.0F,
        12.0F
    );

    style.FramePadding = ImVec2(
        8.0F,
        5.0F
    );

    style.ItemSpacing = ImVec2(
        8.0F,
        6.0F
    );

    style.ItemInnerSpacing = ImVec2(
        6.0F,
        4.0F
    );

    const bool sdlBackendInitialized =
        ImGui_ImplSDL2_InitForOpenGL(
            window,
            glContext
        );

    if (!sdlBackendInitialized)
    {
        std::fprintf(
            stderr,
            "ImGui SDL2 backend initialization failed.\n"
        );

        ImGui::DestroyContext();
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 5;
    }

    const bool openGLBackendInitialized =
        ImGui_ImplOpenGL3_Init(glslVersion);

    if (!openGLBackendInitialized)
    {
        std::fprintf(
            stderr,
            "ImGui OpenGL backend initialization failed.\n"
        );

        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 6;
    }

    sm64::Core core;
    UI userInterface(core);

    bool applicationRunning = true;

    while (applicationRunning)
    {
        SDL_Event event{};

        while (SDL_PollEvent(&event) != 0)
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
            {
                applicationRunning = false;
            }

            if (
                event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)
            )
            {
                applicationRunning = false;
            }

            if (
                event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_q &&
                (event.key.keysym.mod & KMOD_CTRL) != 0
            )
            {
                applicationRunning = false;
            }
        }

        if (
            (SDL_GetWindowFlags(window) &
             SDL_WINDOW_MINIMIZED) != 0U
        )
        {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        userInterface.render();

        ImGui::Render();

        int drawableWidth = 0;
        int drawableHeight = 0;

        SDL_GL_GetDrawableSize(
            window,
            &drawableWidth,
            &drawableHeight
        );

        if (
            drawableWidth > 0 &&
            drawableHeight > 0
        )
        {
            glViewport(
                0,
                0,
                drawableWidth,
                drawableHeight
            );

            glClearColor(
                0.030F,
                0.040F,
                0.060F,
                1.000F
            );

            glClear(
                GL_COLOR_BUFFER_BIT
            );

            ImGui_ImplOpenGL3_RenderDrawData(
                ImGui::GetDrawData()
            );

            SDL_GL_SwapWindow(window);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();

    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);

    SDL_Quit();

    return 0;
}
