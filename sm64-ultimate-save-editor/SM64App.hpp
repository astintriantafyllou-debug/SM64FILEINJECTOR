// SM64App.hpp - the Dear ImGui application (no SDL/OpenGL dependency).
//
// The App draws one frame of UI per render() call. Everything platform-specific
// (file dialogs, window title, font texture refresh) is injected or polled by
// the caller, so the same code runs in the real window and in headless tests.
#pragma once

#include "SM64Presets.hpp"
#include "SM64Report.hpp"
#include "SM64Settings.hpp"
#include "SM64SystemCore.hpp"
#include "SM64Theme.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sm64ui {

enum class Page : std::uint8_t {
    Dashboard,
    Slots,
    SecretStars,
    Castle,
    Validation,
    RawData,
    Export,
    GameShark,
    Settings,
    About,
};
inline constexpr std::size_t kPageCount = 10;

// Dialogs the App can show; exposed so tests can open each one deterministically.
enum class Dialog : std::uint8_t {
    None,
    Unsaved,
    NewSave,
    Preset,
    Overwrite,
    Notice,
    Welcome,
    Shortcuts,
    History,
    Palette,
    RepairPreview,
    Confirm,
};

// Implemented by main.cpp (native dialogs) and by tests (fakes).
class Platform {
public:
    virtual ~Platform() = default;
    virtual std::optional<std::filesystem::path> openFileDialog(const std::string& title) = 0;
    virtual std::optional<std::filesystem::path> saveFileDialog(const std::string& title,
                                                                const std::filesystem::path& suggested,
                                                                const std::vector<std::string>& extensions) = 0;
};

class App {
public:
    App(sm64::Core& core, Platform& platform, sm64::Settings settings, std::filesystem::path settingsPath);

    // Draws the whole UI for the current ImGui frame (call between NewFrame and Render).
    void render();

    // ---- integration with the host window ---------------------------------
    [[nodiscard]] std::string windowTitle() const;
    [[nodiscard]] bool quitRequested() const noexcept { return quit_; }
    void requestQuit();                           // e.g. window close button
    void openPath(const std::filesystem::path&);  // drag-and-drop / command line
    [[nodiscard]] const sm64::Settings& settings() const noexcept { return settings_; }
    // True once when the theme or scale changed and the host must rebuild style/fonts.
    [[nodiscard]] bool takeStyleDirty() noexcept {
        const bool dirty = styleDirty_;
        styleDirty_ = false;
        return dirty;
    }

    // ---- test hooks ---------------------------------------------------------
    void setPage(Page page) noexcept { page_ = page; }
    [[nodiscard]] Page page() const noexcept { return page_; }
    void setExpert(bool expert) noexcept;
    void setSlot(int slot) noexcept { slot_ = slot; }
    void openDialog(Dialog dialog);
    [[nodiscard]] Dialog openDialogKind() const noexcept { return dialog_; }
    [[nodiscard]] bool hasPath() const noexcept { return !path_.empty(); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    // Immediate (non-dialog) actions, also bound to menus and shortcuts.
    void actionNew();
    void actionOpen();
    void actionSave();
    void actionSaveAs();
    void actionUndo();
    void actionRedo();
    void actionBackup() { createBackupCopy(); }

private:
    // frame
    void drawMenuBar();
    void drawShell();
    void drawNav(float width);
    void drawNavCompact();
    void drawContent();
    void drawStatusBar();
    void drawToasts();
    void drawDialogs();
    void handleShortcuts();
    void updateAnimations();

    // pages
    void pageDashboard();
    void pageSlots();
    void pageSecretStars();
    void pageCastle();
    void pageValidation();
    void pageRawData();
    void pageExport();
    void pageGameShark();
    void pageSettings();
    void pageAbout();

    // shared widgets
    void slotSelector(const char* id);
    void shortcutButtons();
    void slotCard(int slot, float width);
    void courseTable(int slot);
    void unsavedChangesPanel();
    void historyList();
    bool editsAllowed() const noexcept { return !settings_.forensic; }

    // dialogs
    void dialogUnsaved();
    void dialogNewSave();
    void dialogPreset();
    void dialogOverwrite();
    void dialogNotice();
    void dialogWelcome();
    void dialogShortcuts();
    void dialogHistory();
    void dialogPalette();
    void dialogRepairPreview();
    void dialogConfirm();

    // workflow
    void guardUnsaved(std::function<void()> action);
    void loadFrom(const std::filesystem::path& path);
    bool saveTo(const std::filesystem::path& path);
    void saveThen(std::function<void()> next);
    void createBackupCopy();
    void exportTo(const std::filesystem::path& path);
    void writeReport(const std::string& defaultName, const std::string& extension, const std::string& text);
    void openPresetDialog(sm64::PresetId id);
    void notice(const std::string& title, const std::string& text);
    void toast(const std::string& text, Status level);
    void persistSettings();
    void markStyleDirty() noexcept { styleDirty_ = true; }
    void loadNotes();
    void saveNotes();
    [[nodiscard]] std::filesystem::path notesPath() const;
    [[nodiscard]] std::string displayName() const;
    // "Unsaved" includes a freshly created file that has never been written to disk.
    [[nodiscard]] bool dirtyForUser() const noexcept { return core_.dirty() || neverSaved_; }

    // hex editor helpers
    void hexCommitByte(std::size_t offset, std::uint8_t value);
    void hexSearchRun();
    void hexCopySelection();
    void hexPasteClipboard();

    sm64::Core& core_;
    Platform& platform_;
    sm64::Settings settings_;
    std::filesystem::path settingsPath_;

    // file state
    std::filesystem::path path_;
    bool untitled_{true};
    bool neverSaved_{false};
    bool quit_{false};
    bool styleDirty_{true};

    // navigation / selection
    Page page_{Page::Dashboard};
    int slot_{0};
    std::array<float, 4> shownStars_{};
    std::array<char, 64> courseFilter_{};

    // dialogs
    Dialog dialog_{Dialog::None};
    bool dialogJustOpened_{false};
    std::function<void()> pendingAction_;
    std::string noticeTitle_;
    std::string noticeText_;
    std::string confirmText_;
    std::function<void()> confirmAction_;
    sm64::PresetRequest presetRequest_{};
    int newSaveChoice_{0};
    std::array<char, 64> paletteQuery_{};
    std::filesystem::path overwritePath_;
    std::function<void(const std::filesystem::path&)> overwriteAction_;
    bool advancedChecksumAck_{false};

    // toasts
    struct Toast {
        std::string text;
        int level{};
        float remaining{};
    };
    std::vector<Toast> toasts_;
    std::vector<sm64::FieldChange> lastChanges_;
    std::string lastChangesTitle_;

    // notes (sidecar file, never written into the EEPROM)
    std::array<char, 1024> notes_{};
    bool notesDirty_{false};
    std::filesystem::path notesLoadedFor_;

    // validation page
    int diffPair_{-1};

    // hex editor
    int hexCursor_{0};
    int hexAnchor_{0};
    int hexPendingNibble_{-1};
    bool hexReadOnly_{false};
    bool hexAutoFix_{false};
    bool hexCompare_{false};
    bool hexHighlightChanges_{true};
    bool hexFocus_{false};  // the hex table owns the keyboard (after a click)
    int hexScrollRow_{-1};
    std::array<char, 16> hexJump_{};
    std::array<char, 96> hexSearch_{};
    std::vector<int> hexMatches_;
    int hexMatchIndex_{-1};
    int hexMatchLength_{0};
    std::string hexMessage_;
    std::vector<int> hexBookmarks_;

    // export / gameshark
    int exportProfile_{0};
    std::array<char, 512> exportPath_{};
    std::array<char, 16> gsBaseText_{};
    bool gsBaseInit_{false};
    std::string gsLastText_;

    int tutorialStep_{0};
};

}  // namespace sm64ui
