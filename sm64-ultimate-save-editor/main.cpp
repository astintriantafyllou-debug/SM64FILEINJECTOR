#include "SM64HexEditorUI.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
int main(){if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER))return 1;SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2);auto*w=SDL_CreateWindow("SM64 Ultimate Save Editor",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1400,850,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);if(!w)return 2;auto gl=SDL_GL_CreateContext(w);SDL_GL_SetSwapInterval(1);IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::StyleColorsDark();ImGui_ImplSDL2_InitForOpenGL(w,gl);ImGui_ImplOpenGL3_Init("#version 150");sm64::Core core;UI ui(core);bool done=false;while(!done){SDL_Event e;while(SDL_PollEvent(&e)){ImGui_ImplSDL2_ProcessEvent(&e);if(e.type==SDL_QUIT)done=true;}ImGui_ImplOpenGL3_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();ui.render();ImGui::Render();int x,y;SDL_GL_GetDrawableSize(w,&x,&y);glViewport(0,0,x,y);glClearColor(.03,.04,.06,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());SDL_GL_SwapWindow(w);}ImGui_ImplOpenGL3_Shutdown();ImGui_ImplSDL2_Shutdown();ImGui::DestroyContext();SDL_GL_DeleteContext(gl);SDL_DestroyWindow(w);SDL_Quit();}
