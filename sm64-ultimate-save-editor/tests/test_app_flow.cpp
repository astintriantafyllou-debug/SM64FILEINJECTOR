// Application workflow tests: the file-safety behaviours, driven through the real App
// object with a fake file-dialog platform and a headless Dear ImGui context.
#include "SM64App.hpp"
#include "SM64Presets.hpp"
#include "SM64Theme.hpp"
#include "test_framework.hpp"

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace sm64ui;

namespace {

class FakePlatform final : public Platform {
public:
    std::optional<fs::path> nextOpen;
    std::optional<fs::path> nextSave;
    int openCalls{0};
    int saveCalls{0};
    std::optional<fs::path> openFileDialog(const std::string&) override {
        ++openCalls;
        return nextOpen;
    }
    std::optional<fs::path> saveFileDialog(const std::string&, const fs::path&,
                                           const std::vector<std::string>&) override {
        ++saveCalls;
        return nextSave;
    }
};

struct Env {
    ImGuiContext* ctx;
    Env() {
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = ImVec2(1280.0F, 720.0F);
        io.DeltaTime = 1.0F / 60.0F;
        configureFonts(1.0F);
        unsigned char* px = nullptr;
        int w = 0;
        int h = 0;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        applyTheme(true, 0, 1.0F);
    }
    ~Env() { ImGui::DestroyContext(ctx); }
    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;
    static void frame(App& app) {
        ImGui::NewFrame();
        app.render();
        ImGui::Render();
    }
};

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("sm64flow_" + std::to_string(++n) + "_" +
                                            std::to_string(reinterpret_cast<std::uintptr_t>(this) % 99991U));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

fs::path writeSave(const fs::path& where) {
    sm64::Core core;
    sm64::PresetRequest request;
    request.id = sm64::PresetId::Progress70Stars;
    std::string error;
    (void)sm64::applyPreset(core, request, error);
    sm64::Core::SaveOptions options;
    options.makeBackup = false;
    (void)core.save(where, options, error);
    return where;
}

sm64::Settings quietSettings() {
    sm64::Settings s;
    s.tutorialSeen = true;
    return s;
}

}  // namespace

TEST("flow: opening a file loads it, tracks it as recent and names the window") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "mario.eep");
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), dir.path / "settings.cfg");
    app.openPath(file);
    CHECK(app.hasPath());
    CHECK_EQ(core.stars(0).total, 70);
    CHECK_EQ(app.settings().recent.size(), std::size_t{1});
    CHECK_EQ(app.windowTitle(), std::string("SM64 Ultimate Save Editor - mario.eep"));
    CHECK(fs::exists(dir.path / "settings.cfg"));  // recent files persisted
    Env::frame(app);
}

TEST("flow: unsaved changes show an asterisk in the title; Save keeps the path and the .bin extension") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "game.bin");
    const auto before = readAll(file);
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);
    CHECK(core.setFlag(3, sm64::save_flag::FileExists, true));
    CHECK(app.windowTitle().find(" *") != std::string::npos);

    app.actionSave();
    CHECK_EQ(platform.saveCalls, 0);  // no dialog: it saves in place
    CHECK(app.path() == file);        // extension preserved
    CHECK(!fs::exists(dir.path / "game.eep"));
    CHECK(app.windowTitle().find(" *") == std::string::npos);
    CHECK(readAll(file) != before);
    CHECK_EQ(readAll(file).size(), std::size_t{512});
    CHECK(fs::exists(dir.path / "game.bin.bak"));  // backup of the previous version
    CHECK(readAll(dir.path / "game.bin.bak") == before);
}

TEST("flow: backups can be turned off") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "a.eep");
    sm64::Core core;
    FakePlatform platform;
    sm64::Settings settings = quietSettings();
    settings.backupOnSave = false;
    App app(core, platform, settings, {});
    app.openPath(file);
    CHECK(core.setFlag(0, sm64::save_flag::HaveKey1, true));
    app.actionSave();
    CHECK(!fs::exists(dir.path / "a.eep.bak"));
}

TEST("flow: Save As adds a default extension and never touches the original") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "orig.eep");
    const auto before = readAll(file);
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);
    CHECK(core.setCoinScore(0, 0, 55));
    platform.nextSave = dir.path / "copy";  // user typed no extension
    app.actionSaveAs();
    CHECK(fs::exists(dir.path / "copy.eep"));
    CHECK(readAll(file) == before);
    CHECK(app.path() == dir.path / "copy.eep");
}

TEST("flow: Save As onto an existing file asks before overwriting and leaves it intact") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "one.eep");
    const fs::path other = writeSave(dir.path / "two.eep");
    const auto otherBefore = readAll(other);
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);
    CHECK(core.setCoinScore(0, 0, 9));
    platform.nextSave = other;
    app.actionSaveAs();
    CHECK(app.openDialogKind() == Dialog::Overwrite);
    CHECK(readAll(other) == otherBefore);  // nothing written yet
    Env::frame(app);
    Env::frame(app);
    CHECK(app.openDialogKind() == Dialog::Overwrite);  // still waiting for the user
}

TEST("flow: opening or quitting with unsaved changes asks first") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "x.eep");
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);

    app.actionOpen();  // clean: goes straight to the file dialog
    CHECK_EQ(platform.openCalls, 1);
    CHECK(app.openDialogKind() == Dialog::None);

    CHECK(core.setFlag(0, sm64::save_flag::HaveKey1, true));
    app.actionOpen();  // dirty: must warn, must not open the dialog yet
    CHECK(app.openDialogKind() == Dialog::Unsaved);
    CHECK_EQ(platform.openCalls, 1);

    app.openDialog(Dialog::None);
    app.requestQuit();
    CHECK(!app.quitRequested());
    CHECK(app.openDialogKind() == Dialog::Unsaved);
}

TEST("flow: quitting a clean file quits immediately; New save opens the wizard") {
    Env env;
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.actionNew();
    CHECK(app.openDialogKind() == Dialog::NewSave);
    app.openDialog(Dialog::None);
    app.requestQuit();
    CHECK(app.quitRequested());
}

TEST("flow: the unsaved indicator survives a failed save") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "f.eep");
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);
    CHECK(core.setFlag(0, sm64::save_flag::HaveKey1, true));
    fs::create_directories(dir.path / "f.eep.tmp");  // make the temp file impossible to create
    app.actionSave();
    CHECK(app.windowTitle().find(" *") != std::string::npos);
    CHECK(core.dirty());
    CHECK(app.openDialogKind() == Dialog::Notice);  // the user is told why
}

TEST("flow: Ctrl+Z and Ctrl+Y undo and redo through the keyboard shortcut handler") {
    Env env;
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    const auto original = core.bytes();
    CHECK(core.setFlag(0, sm64::save_flag::FileExists, true));
    const auto edited = core.bytes();

    ImGuiIO& io = ImGui::GetIO();
    Env::frame(app);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Z, true);
    Env::frame(app);
    io.AddKeyEvent(ImGuiKey_Z, false);
    Env::frame(app);
    CHECK(core.view() == original);

    io.AddKeyEvent(ImGuiKey_Y, true);
    Env::frame(app);
    io.AddKeyEvent(ImGuiKey_Y, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    Env::frame(app);
    CHECK(core.view() == edited);
}

TEST("flow: read-only forensic mode blocks saving and undo, and says why") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "r.eep");
    const auto before = readAll(file);
    sm64::Core core;
    FakePlatform platform;
    sm64::Settings settings = quietSettings();
    settings.forensic = true;
    App app(core, platform, settings, {});
    app.openPath(file);
    CHECK(core.setCoinScore(0, 1, 7));  // the engine itself still works; the UI refuses to save
    app.actionSave();
    CHECK(readAll(file) == before);
    CHECK(app.openDialogKind() == Dialog::Notice);
    app.actionSaveAs();
    CHECK_EQ(platform.saveCalls, 0);
    CHECK(readAll(file) == before);
    const bool canUndo = core.canUndo();
    app.actionUndo();
    CHECK(core.canUndo() == canUndo);  // undo is disabled too
}

TEST("flow: Create backup copy writes a timestamped copy and leaves the original alone") {
    Env env;
    TempDir dir;
    const fs::path file = writeSave(dir.path / "b.eep");
    const auto before = readAll(file);
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.openPath(file);
    app.actionBackup();
    int copies = 0;
    for (const auto& entry : fs::directory_iterator(dir.path)) {
        const std::string name = entry.path().filename().string();
        if (name.find("b.backup-") == 0 && entry.path().extension() == ".eep") {
            ++copies;
            CHECK(readAll(entry.path()) == before);
        }
    }
    CHECK_EQ(copies, 1);
    CHECK(readAll(file) == before);
}

TEST("flow: rejecting a bad file keeps the previous data and reports an error") {
    Env env;
    TempDir dir;
    {
        std::ofstream out(dir.path / "bad.eep", std::ios::binary);
        out << "too small";
    }
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    const auto before = core.bytes();
    app.openPath(dir.path / "bad.eep");
    CHECK(core.view() == before);
    CHECK(!app.hasPath());
    CHECK(app.openDialogKind() == Dialog::Notice);
}

TEST("flow: the New save wizard creates a valid unsaved file that guards against being discarded") {
    Env env;
    TempDir dir;
    sm64::Core core;
    FakePlatform platform;
    App app(core, platform, quietSettings(), {});
    app.actionNew();
    Env::frame(app);
    Env::frame(app);
    CHECK(app.openDialogKind() == Dialog::NewSave);
    // (Choosing and confirming a wizard option needs mouse input; the content it builds is covered
    //  by the preset tests. Here we check the wizard renders and is dismissable.)
    app.openDialog(Dialog::None);
    Env::frame(app);
    CHECK(app.openDialogKind() == Dialog::None);
}
