// main.cpp - SDL2 + OpenGL + Dear ImGui host for the SM64 Ultimate Save Editor.
//
// Developer/QA flags (all optional):
//   --frames N         exit after N frames
//   --screenshot FILE  write the last frame as a binary PPM (needs --frames)
//   --page N           start on page N (0 = Dashboard ... 9 = About)
//   --expert           start in Expert mode
//   --size WxH         force the window size in pixels (ignores display limits)
//   --dpi S            override the detected display scale (e.g. 1.5)
//   --settings FILE    use this settings file
//   --no-tour          do not show the welcome tour
//   --slot N           start with slot N (0-3) selected
//   any other argument is treated as a save file to open
#include "SM64App.hpp"
#include "SM64Theme.hpp"

#include <SDL.h>
#include <SDL_opengl.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#include <portable-file-dialogs.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Options {
    int frames{-1};
    std::string screenshot;
    int page{-1};
    bool expert{false};
    int width{0};
    int height{0};
    float dpi{0.0F};
    std::string settingsFile;
    bool noTour{false};
    int slot{0};
    std::string file;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--page") o.page = std::atoi(next().c_str());
        else if (a == "--expert") o.expert = true;
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--dpi") o.dpi = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--settings") o.settingsFile = next();
        else if (a == "--no-tour") o.noTour = true;
        else if (a == "--slot") o.slot = std::atoi(next().c_str());
        else if (!a.empty() && a[0] != '-') o.file = a;
    }
    return o;
}

// Native dialogs through portable-file-dialogs (zenity/kdialog on Linux).
class NativePlatform final : public sm64ui::Platform {
public:
    std::optional<std::filesystem::path> openFileDialog(const std::string& title) override {
        const auto result = pfd::open_file(title, "", {"SM64 saves (*.eep *.bin *.sav)", "*.eep *.bin *.sav", "All files", "*"}).result();
        if (result.empty()) return std::nullopt;
        return std::filesystem::path(result.front());
    }
    std::optional<std::filesystem::path> saveFileDialog(const std::string& title, const std::filesystem::path& suggested,
                                                        const std::vector<std::string>& extensions) override {
        std::string pattern;
        for (const std::string& e : extensions) pattern += (pattern.empty() ? "*" : " *") + e;
        if (pattern.empty()) pattern = "*";
        // The application asks for overwrite confirmation itself.
        const std::string chosen = pfd::save_file(title, suggested.string(), {"Save files", pattern, "All files", "*"},
                                                  pfd::opt::force_overwrite).result();
        if (chosen.empty()) return std::nullopt;
        return std::filesystem::path(chosen);
    }
};

float detectDpiScale(SDL_Window* window, float override) {
    if (override > 0.0F) return std::clamp(override, 0.75F, 4.0F);
#if defined(__APPLE__)
    (void)window;
    return 1.0F;  // macOS scales through the framebuffer instead
#else
    const int display = std::max(0, SDL_GetWindowDisplayIndex(window));
    float ddpi = 0.0F;
    if (SDL_GetDisplayDPI(display, &ddpi, nullptr, nullptr) != 0 || ddpi < 48.0F) return 1.0F;
    const float scale = ddpi / 96.0F;
    return std::clamp(std::round(scale * 4.0F) / 4.0F, 1.0F, 3.0F);
#endif
}

std::filesystem::path chooseSettingsPath(const std::string& override) {
    if (!override.empty()) return override;
    char* base = SDL_GetBasePath();
    if (base != nullptr) {
        const std::filesystem::path portable = std::filesystem::path(base) / "portable.txt";
        const std::filesystem::path dir(base);
        SDL_free(base);
        std::error_code ec;
        if (std::filesystem::exists(portable, ec)) return dir / "settings.cfg";
    }
    char* pref = SDL_GetPrefPath("SM64Ultimate", "SaveEditor");
    if (pref == nullptr) return {};
    const std::filesystem::path path = std::filesystem::path(pref) / "settings.cfg";
    SDL_free(pref);
    return path;
}

bool writePpm(const std::string& file, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3U),
                  static_cast<std::streamsize>(width) * 3);
    }
    return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseArgs(argc, argv);

#if defined(_WIN32)
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glslVersion = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // Size the window from the *usable* display area (excludes the taskbar/dock).
    SDL_Rect usable{0, 0, 1280, 720};
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0) SDL_GetDisplayBounds(0, &usable);
    float dpiGuess = options.dpi > 0.0F ? options.dpi : 1.0F;
    int width = options.width;
    int height = options.height;
    int minWidth = 640;
    int minHeight = 480;
    if (width <= 0 || height <= 0) {
        width = std::clamp(static_cast<int>(static_cast<float>(usable.w) * 0.82F), 1, usable.w);
        height = std::clamp(static_cast<int>(static_cast<float>(usable.h) * 0.82F), 1, usable.h);
        minWidth = std::min(usable.w, static_cast<int>(800.0F * dpiGuess));
        minHeight = std::min(usable.h, static_cast<int>(600.0F * dpiGuess));
        width = std::max(width, minWidth);
        height = std::max(height, minHeight);
    }
    const int x = usable.x + std::max(0, (usable.w - width) / 2);
    const int y = usable.y + std::max(0, (usable.h - height) / 2);

    SDL_Window* window = SDL_CreateWindow("SM64 Ultimate Save Editor", x, y, width, height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (options.width <= 0) SDL_SetWindowMinimumSize(window, minWidth, minHeight);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(options.frames >= 0 ? 0 : 1);

    const float dpiScale = detectDpiScale(window, options.dpi);
    if (options.width <= 0) {
        // Re-apply the minimum now that the real DPI scale is known.
        SDL_SetWindowMinimumSize(window, std::min(usable.w, static_cast<int>(800.0F * dpiScale)),
                                 std::min(usable.h, static_cast<int>(600.0F * dpiScale)));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // never persist window positions

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glslVersion);

    const std::filesystem::path settingsPath = chooseSettingsPath(options.settingsFile);
    sm64::Settings settings;
    if (!settingsPath.empty()) (void)sm64::loadSettings(settingsPath, settings);
    if (options.expert) settings.expert = true;
    if (options.noTour) settings.tutorialSeen = true;

    sm64::Core core;
    NativePlatform platform;
    auto app = std::make_unique<sm64ui::App>(core, platform, settings, settingsPath);
    if (options.page >= 0 && options.page < static_cast<int>(sm64ui::kPageCount)) app->setPage(static_cast<sm64ui::Page>(options.page));
    app->setSlot(std::clamp(options.slot, 0, 3));
    if (!options.file.empty()) app->openPath(options.file);

    auto rebuildStyle = [&]() {
        int winW = 0;
        int winH = 0;
        int drawW = 0;
        int drawH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
        (void)winH;
        (void)drawH;
        const float fb = winW > 0 ? std::max(1.0F, static_cast<float>(drawW) / static_cast<float>(winW)) : 1.0F;
        const float total = dpiScale * app->settings().userScale;
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        sm64ui::configureFonts(total * fb);
        io.FontGlobalScale = 1.0F / fb;
        sm64ui::applyTheme(app->settings().darkTheme, app->settings().accent, total);
        ImGui_ImplOpenGL3_CreateFontsTexture();
    };

    std::string lastTitle;
    int frame = 0;
    bool running = true;
    bool styleReady = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) app->requestQuit();
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                app->requestQuit();
            }
            if (event.type == SDL_DROPFILE) {
                app->openPath(event.drop.file);
                SDL_free(event.drop.file);
            }
        }
        if (app->quitRequested()) running = false;

        if (!styleReady || app->takeStyleDirty()) {
            rebuildStyle();
            styleReady = true;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        app->render();
        ImGui::Render();

        const std::string title = app->windowTitle();
        if (title != lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastTitle = title;
        }

        int fbWidth = 0;
        int fbHeight = 0;
        SDL_GL_GetDrawableSize(window, &fbWidth, &fbHeight);
        glViewport(0, 0, fbWidth, fbHeight);
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        glClearColor(bg.x, bg.y, bg.z, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frame;
        if (options.frames >= 0 && frame >= options.frames) {
            if (!options.screenshot.empty() && !writePpm(options.screenshot, fbWidth, fbHeight)) {
                std::fprintf(stderr, "Could not write screenshot %s\n", options.screenshot.c_str());
            }
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }

    app.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
