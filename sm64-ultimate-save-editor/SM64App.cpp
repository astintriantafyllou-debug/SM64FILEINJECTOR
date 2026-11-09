// SM64App.cpp - application shell: menus, navigation, dialogs and file workflow.
// Page bodies live in SM64Pages.cpp.
#include "SM64App.hpp"

#include "SM64PageUtil.hpp"
#include "SM64Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>

namespace sm64ui {
namespace {

constexpr std::array<const char*, kPageCount> kPageNames{
    "Dashboard", "Save Slots", "Secret Stars", "Castle Progress", "Validation & Repair",
    "Raw Data",  "Export",     "GameShark",    "Settings",        "About"};

constexpr std::array<const char*, 12> kDialogNames{
    "##none",  "Unsaved changes",    "Create a new save", "Preview preset",  "Confirm overwrite", "Notice",
    "Welcome", "Keyboard shortcuts", "Edit history",      "Command palette", "Repair preview",    "Please confirm"};

bool expertOnly(Page page) {
    return page == Page::RawData || page == Page::GameShark;
}

float em() {
    return ImGui::GetFontSize();
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string timestampForFilename() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

// Centered modal that never opens larger than the usable display.
bool beginModal(const char* name, float widthEm) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float maxW = viewport->WorkSize.x * 0.94F;
    const float width = std::min(widthEm * em(), maxW);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(24.0F * em(), maxW), 0.0F),
                                        ImVec2(width, viewport->WorkSize.y * 0.92F));
    return ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction and host integration
// ---------------------------------------------------------------------------
App::App(sm64::Core& core, Platform& platform, sm64::Settings settings, std::filesystem::path settingsPath)
    : core_(core), platform_(platform), settings_(std::move(settings)), settingsPath_(std::move(settingsPath)) {
    hexBookmarks_ = {0x000, 0x070, 0x0E0, 0x150, 0x1C0};
    for (std::size_t i = 0; i < shownStars_.size(); ++i) {
        shownStars_[i] = static_cast<float>(core_.stars(static_cast<int>(i)).total);
    }
    if (!settings_.tutorialSeen) openDialog(Dialog::Welcome);
}

std::string App::displayName() const {
    return path_.empty() ? std::string("Untitled") : path_.filename().string();
}

std::string App::windowTitle() const {
    return "SM64 Ultimate Save Editor - " + displayName() + (dirtyForUser() ? " *" : "");
}

void App::setExpert(bool expert) noexcept {
    settings_.expert = expert;
    if (!expert && expertOnly(page_)) page_ = Page::Dashboard;
}

void App::openDialog(Dialog dialog) {
    dialog_ = dialog;
    dialogJustOpened_ = dialog != Dialog::None;
}

void App::persistSettings() {
    if (settingsPath_.empty()) return;
    std::string error;
    (void)sm64::saveSettings(settingsPath_, settings_, error);
}

void App::notice(const std::string& title, const std::string& text) {
    noticeTitle_ = title;
    noticeText_ = text;
    openDialog(Dialog::Notice);
}

void App::toast(const std::string& text, Status level) {
    toasts_.push_back(Toast{text, static_cast<int>(level), 5.0F});
    if (toasts_.size() > 4U) toasts_.erase(toasts_.begin());
}

void App::requestQuit() {
    if (!dirtyForUser()) {
        quit_ = true;
        return;
    }
    pendingAction_ = [this] { quit_ = true; };
    openDialog(Dialog::Unsaved);
}

void App::openPath(const std::filesystem::path& path) {
    guardUnsaved([this, path] { loadFrom(path); });
}

// ---------------------------------------------------------------------------
// File workflow
// ---------------------------------------------------------------------------
void App::guardUnsaved(std::function<void()> action) {
    if (!dirtyForUser()) {
        action();
        return;
    }
    pendingAction_ = std::move(action);
    openDialog(Dialog::Unsaved);
}

std::filesystem::path App::notesPath() const {
    std::filesystem::path notes = path_;
    notes += ".sm64notes.txt";
    return notes;
}

void App::loadNotes() {
    notes_.fill('\0');
    notesDirty_ = false;
    notesLoadedFor_ = path_;
    if (path_.empty()) return;
    std::ifstream in(notesPath(), std::ios::binary);
    if (!in) return;
    in.read(notes_.data(), static_cast<std::streamsize>(notes_.size() - 1U));
}

void App::saveNotes() {
    if (path_.empty()) return;
    const std::string text(notes_.data());
    sm64::AtomicWriteOptions options;
    options.makeBackup = false;
    std::string error;
    if (text.empty()) {
        std::error_code ec;
        std::filesystem::remove(notesPath(), ec);
        notesDirty_ = false;
        return;
    }
    if (sm64::atomicWriteFile(
            notesPath(), std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
            options, error)) {
        notesDirty_ = false;
        toast("Notes saved next to the save file (they are not stored inside the EEPROM).", Status::Info);
    } else {
        notice("Could not save notes", error);
    }
}

void App::loadFrom(const std::filesystem::path& path) {
    std::string error;
    if (!core_.load(path, settings_.autoRepairOnLoad, error)) {
        notice("Could not open file", error);
        return;
    }
    path_ = path;
    untitled_ = false;
    neverSaved_ = false;
    sm64::addRecentFile(settings_, path.string());
    persistSettings();
    loadNotes();
    exportPath_.fill('\0');
    for (std::size_t i = 0; i < shownStars_.size(); ++i) {
        shownStars_[i] = static_cast<float>(core_.stars(static_cast<int>(i)).total);
    }
    const sm64::Report report = core_.validate();
    if (report.valid) {
        toast("Opened " + path.filename().string() + " - all 10 blocks are valid.", Status::Good);
    } else {
        toast("Opened " + path.filename().string() + " - problems found. Open Validation & Repair for details.",
              Status::Warning);
    }
    if (core_.detectedOrder() != sm64::ByteOrder::Native) {
        toast(std::string("Byte order converted: ") + sm64::byteOrderName(core_.detectedOrder()), Status::Info);
    }
}

bool App::saveTo(const std::filesystem::path& path) {
    if (settings_.forensic) {
        notice("Read-only mode",
               "Forensic (read-only) mode is on, so nothing can be saved over a file. "
               "Use Export to write a copy, or turn the mode off in Settings.");
        return false;
    }
    sm64::Core::SaveOptions options;
    options.makeBackup = settings_.backupOnSave;
    std::string error;
    if (!core_.save(path, options, error)) {
        notice("Save failed", error);
        return false;
    }
    path_ = path;
    untitled_ = false;
    neverSaved_ = false;
    sm64::addRecentFile(settings_, path.string());
    persistSettings();
    const sm64::Report report = core_.validate();
    toast("Saved " + path.filename().string() + " (512 bytes, verified by reading it back). " +
              (report.valid ? "All blocks valid." : "Note: some blocks are still invalid."),
          report.valid ? Status::Good : Status::Warning);
    return true;
}

void App::actionSave() {
    if (path_.empty()) {
        actionSaveAs();
        return;
    }
    saveTo(path_);
}

void App::actionSaveAs() {
    if (settings_.forensic) {
        saveTo(path_);  // shows the read-only notice
        return;
    }
    const std::filesystem::path suggested = path_.empty() ? std::filesystem::path("sm64-edited.eep") : path_;
    const auto chosen = platform_.saveFileDialog("Save SM64 EEPROM", suggested, {".eep", ".bin", ".sav"});
    if (!chosen) return;
    std::filesystem::path target = *chosen;
    if (!target.has_extension()) target += suggested.has_extension() ? suggested.extension() : ".eep";

    std::error_code ec;
    if (std::filesystem::exists(target, ec) && target != path_) {
        overwritePath_ = target;
        overwriteAction_ = [this](const std::filesystem::path& p) { saveTo(p); };
        openDialog(Dialog::Overwrite);
        return;
    }
    saveTo(target);
}

void App::saveThen(std::function<void()> next) {
    if (settings_.forensic) {
        next();
        return;
    }
    if (path_.empty()) {
        const auto chosen = platform_.saveFileDialog("Save SM64 EEPROM", "sm64-edited.eep", {".eep", ".bin", ".sav"});
        if (!chosen) return;
        std::filesystem::path target = *chosen;
        if (!target.has_extension()) target += ".eep";
        if (!saveTo(target)) return;
    } else if (!saveTo(path_)) {
        return;
    }
    next();
}

void App::actionOpen() {
    guardUnsaved([this] {
        const auto chosen = platform_.openFileDialog("Open SM64 EEPROM save");
        if (chosen) loadFrom(*chosen);
    });
}

void App::actionNew() {
    guardUnsaved([this] { openDialog(Dialog::NewSave); });
}

void App::actionUndo() {
    if (!editsAllowed()) return;
    if (core_.undo()) toast(core_.lastAction(), Status::Info);
}

void App::actionRedo() {
    if (!editsAllowed()) return;
    if (core_.redo()) toast(core_.lastAction(), Status::Info);
}

void App::createBackupCopy() {
    if (path_.empty()) {
        notice("Nothing to back up yet", "Save the file once first; backups are created next to the saved file.");
        return;
    }
    std::filesystem::path target = path_;
    target.replace_extension("");
    target += ".backup-" + timestampForFilename();
    target += path_.extension();

    std::error_code ec;
    if (std::filesystem::is_regular_file(path_, ec)) {
        std::filesystem::copy_file(path_, target, std::filesystem::copy_options::none, ec);
        if (ec) {
            notice("Backup failed", ec.message());
            return;
        }
    } else {
        sm64::AtomicWriteOptions options;
        options.makeBackup = false;
        std::string error;
        if (!sm64::atomicWriteFile(target, core_.view(), options, error)) {
            notice("Backup failed", error);
            return;
        }
    }
    toast("Backup written: " + target.filename().string(), Status::Good);
}

void App::exportTo(const std::filesystem::path& path) {
    std::string error;
    const auto profile = static_cast<sm64::Profile>(exportProfile_);
    if (!core_.exportFile(path, profile, error)) {
        notice("Export failed", error);
        return;
    }
    toast("Exported " + path.filename().string() + " (" + std::to_string(sm64::profileInfo(profile).outputSize) +
              " bytes).",
          Status::Good);
}

void App::openPresetDialog(sm64::PresetId id) {
    presetRequest_.id = id;
    presetRequest_.slot = slot_;
    presetRequest_.destSlot = (slot_ + 1) % 4;
    if (presetRequest_.seed == 0U) presetRequest_.seed = 1U;
    openDialog(Dialog::Preset);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void App::updateAnimations() {
    const float dt = ImGui::GetIO().DeltaTime;
    for (std::size_t i = 0; i < shownStars_.size(); ++i) {
        const auto target = static_cast<float>(core_.stars(static_cast<int>(i)).total);
        const float blend = std::min(1.0F, dt * 9.0F);
        shownStars_[i] += (target - shownStars_[i]) * blend;
        if (std::abs(target - shownStars_[i]) < 0.02F) shownStars_[i] = target;
    }
    for (Toast& t : toasts_) t.remaining -= dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [](const Toast& t) { return t.remaining <= 0.0F; }),
                  toasts_.end());
}

void App::render() {
    updateAnimations();
    if (!settings_.expert && expertOnly(page_)) page_ = Page::Dashboard;
    if (notesLoadedFor_ != path_) loadNotes();

    drawMenuBar();
    drawShell();
    drawToasts();
    drawDialogs();
    handleShortcuts();
}

void App::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || dialog_ != Dialog::None) return;
    if (!io.KeyCtrl) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift)
            actionRedo();
        else
            actionUndo();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        actionRedo();
    } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift)
            actionSaveAs();
        else
            actionSave();
    } else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        actionOpen();
    } else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        actionNew();
    } else if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        paletteQuery_.fill('\0');
        openDialog(Dialog::Palette);
    }
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New save...", "Ctrl+N")) actionNew();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) actionOpen();
        if (ImGui::BeginMenu("Open recent", !settings_.recent.empty())) {
            for (std::size_t i = 0; i < settings_.recent.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                const std::filesystem::path recent = settings_.recent[i];
                if (ImGui::MenuItem(recent.filename().string().c_str())) openPath(recent);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", settings_.recent[i].c_str());
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear recent files")) {
                settings_.recent.clear();
                persistSettings();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, editsAllowed())) actionSave();
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, editsAllowed())) actionSaveAs();
        if (ImGui::MenuItem("Create backup copy", nullptr, false, !path_.empty())) createBackupCopy();
        ImGui::Separator();
        if (ImGui::MenuItem("Export...")) page_ = Page::Export;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const std::string undoText =
            core_.canUndo() ? "Undo: " + core_.undoStack().back().description : std::string("Undo");
        const std::string redoText =
            core_.canRedo() ? "Redo: " + core_.redoStack().back().description : std::string("Redo");
        if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, core_.canUndo() && editsAllowed())) actionUndo();
        if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, core_.canRedo() && editsAllowed())) actionRedo();
        ImGui::Separator();
        if (ImGui::MenuItem("History...")) openDialog(Dialog::History);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Command palette...", "Ctrl+K")) {
            paletteQuery_.fill('\0');
            openDialog(Dialog::Palette);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Validate and repair...")) page_ = Page::Validation;
        ImGui::SeparatorText("Whole-file presets");
        for (const sm64::PresetInfo& info : sm64::presetCatalog()) {
            if (info.scope != sm64::PresetScope::WholeFile) continue;
            if (ImGui::MenuItem(info.name, nullptr, false, editsAllowed())) openPresetDialog(info.id);
        }
        ImGui::SeparatorText("Selected-slot presets");
        for (const sm64::PresetInfo& info : sm64::presetCatalog()) {
            if (info.scope == sm64::PresetScope::WholeFile) continue;
            if (ImGui::MenuItem(info.name, nullptr, false, editsAllowed())) openPresetDialog(info.id);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Beginner mode", nullptr, !settings_.expert)) {
            setExpert(false);
            persistSettings();
        }
        if (ImGui::MenuItem("Expert mode", nullptr, settings_.expert)) {
            setExpert(true);
            persistSettings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Dark theme", nullptr, settings_.darkTheme)) {
            settings_.darkTheme = true;
            markStyleDirty();
            persistSettings();
        }
        if (ImGui::MenuItem("Light theme", nullptr, !settings_.darkTheme)) {
            settings_.darkTheme = false;
            markStyleDirty();
            persistSettings();
        }
        if (ImGui::BeginMenu("UI size")) {
            for (const float s : {1.0F, 1.25F, 1.5F, 1.75F, 2.0F}) {
                char label[16]{};
                std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(s * 100.0F + 0.5F));
                if (ImGui::MenuItem(label, nullptr, std::abs(settings_.userScale - s) < 0.01F)) {
                    settings_.userScale = s;
                    markStyleDirty();
                    persistSettings();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Welcome tour")) {
            tutorialStep_ = 0;
            openDialog(Dialog::Welcome);
        }
        if (ImGui::MenuItem("Keyboard shortcuts")) openDialog(Dialog::Shortcuts);
        if (ImGui::MenuItem("About")) page_ = Page::About;
        ImGui::EndMenu();
    }

    // Right-aligned unsaved indicator (text + icon, not colour alone).
    const char* label = dirtyForUser() ? "Unsaved changes" : "All changes saved";
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetTextLineHeight() + 24.0F;
    if (ImGui::GetWindowWidth() > w + 24.0F * em()) {
        ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().ItemSpacing.x);
        statusLabel(dirtyForUser() ? Status::Warning : Status::Good, label);
    }
    ImGui::EndMainMenuBar();
}

void App::drawShell() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin("##shell", nullptr, flags);
    ImGui::PopStyleVar();

    const float unit = em();
    const float statusHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 0.5F + 4.0F;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool compact = avail.x < 56.0F * unit;  // ~840 px at 100%: below this the sidebar becomes a combo
    const float navWidth = 13.0F * unit;
    const float bodyHeight = std::max(1.0F, avail.y - statusHeight);

    if (!compact) {
        ImGui::BeginChild("##nav", ImVec2(navWidth, bodyHeight), ImGuiChildFlags_Borders);
        drawNav(navWidth);
        ImGui::EndChild();
        ImGui::SameLine(0.0F, 0.0F);
    }
    ImGui::BeginChild("##body", ImVec2(0.0F, bodyHeight), ImGuiChildFlags_None);
    if (compact) {
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y));
        drawNavCompact();
    }
    drawContent();
    ImGui::EndChild();

    drawStatusBar();
    ImGui::End();
}

void App::drawNav(float width) {
    (void)width;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPos(ImVec2(style.WindowPadding.x, style.WindowPadding.y));
    ImGui::BeginGroup();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = em() * 0.7F;
        drawStar(dl, ImVec2(p.x + r, p.y + r), r, ImGui::GetColorU32(palette().star));
        ImGui::Dummy(ImVec2(r * 2.0F, r * 2.0F));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted("SM64");
        detail::mutedText("Save Editor");
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 3.0F));
    const float rowHeight = ImGui::GetFrameHeight() * 1.05F;
    const float itemWidth = ImGui::GetContentRegionAvail().x - style.WindowPadding.x;
    bool expertHeaderShown = false;
    for (std::size_t i = 0; i < kPageCount; ++i) {
        const auto page = static_cast<Page>(i);
        if (expertOnly(page) && !settings_.expert) continue;
        if (page == Page::RawData && !expertHeaderShown) {
            ImGui::Spacing();
            detail::mutedText("EXPERT TOOLS");
            expertHeaderShown = true;
        }
        if (page == Page::Settings) {
            ImGui::Spacing();
            detail::mutedText("APP");
        }
        ImGui::SetCursorPosX(style.WindowPadding.x);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(kPageNames[i], page_ == page, 0, ImVec2(itemWidth, rowHeight))) page_ = page;
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    // Mode switch pinned to the bottom of the sidebar.
    const float bottom = ImGui::GetFrameHeight() * 2.0F + style.ItemSpacing.y * 3.0F;
    const float y = ImGui::GetWindowHeight() - bottom - style.WindowPadding.y;
    if (y > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(y);
    ImGui::SetCursorPosX(style.WindowPadding.x);
    ImGui::BeginGroup();
    detail::mutedText("MODE");
    const float half = (itemWidth - style.ItemSpacing.x) * 0.5F;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y));
    if (ImGui::Selectable("Beginner", !settings_.expert, ImGuiSelectableFlags_None, ImVec2(half, 0.0F))) {
        setExpert(false);
        persistSettings();
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Expert", settings_.expert, ImGuiSelectableFlags_None, ImVec2(half, 0.0F))) {
        setExpert(true);
        persistSettings();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

void App::drawNavCompact() {
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x - 8.0F, 22.0F * em()));
    if (ImGui::BeginCombo("##pagecombo", kPageNames[static_cast<std::size_t>(page_)])) {
        for (std::size_t i = 0; i < kPageCount; ++i) {
            const auto page = static_cast<Page>(i);
            if (expertOnly(page) && !settings_.expert) continue;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(kPageNames[i], page_ == page)) page_ = page;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    detail::mutedText("Mode:");
    ImGui::SameLine();
    if (ImGui::SmallButton(settings_.expert ? "Expert" : "Beginner")) {
        setExpert(!settings_.expert);
        persistSettings();
    }
    ImGui::Spacing();
}

void App::drawContent() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(style.WindowPadding.x * 1.4F, style.WindowPadding.y * 1.2F));
    ImGui::BeginChild("##page", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
    ImGui::PopStyleVar();

    ImGui::SetWindowFontScale(1.35F);
    ImGui::TextUnformatted(kPageNames[static_cast<std::size_t>(page_)]);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::Separator();
    ImGui::Spacing();

    if (settings_.forensic) {
        statusLabel(Status::Info, "Read-only forensic mode is on: editing and saving are disabled.");
        ImGui::Spacing();
    }

    switch (page_) {
        case Page::Dashboard:
            pageDashboard();
            break;
        case Page::Slots:
            pageSlots();
            break;
        case Page::SecretStars:
            pageSecretStars();
            break;
        case Page::Castle:
            pageCastle();
            break;
        case Page::Validation:
            pageValidation();
            break;
        case Page::RawData:
            pageRawData();
            break;
        case Page::Export:
            pageExport();
            break;
        case Page::GameShark:
            pageGameShark();
            break;
        case Page::Settings:
            pageSettings();
            break;
        case Page::About:
            pageAbout();
            break;
    }
    ImGui::EndChild();
}

void App::drawStatusBar() {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::SetCursorPosX(style.WindowPadding.x);
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - style.WindowPadding.y * 0.5F - 2.0F);
    const sm64::Report report = core_.validate();
    int bad = 0;
    for (std::size_t id = 0; id < sm64::kBlockCount; ++id) bad += report.block(id).valid() ? 0 : 1;
    if (report.valid)
        statusLabel(Status::Good, "All 10 blocks valid");
    else
        statusLabel(Status::Bad, (std::to_string(bad) + " block(s) invalid").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(displayName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("512 bytes");
    if (!core_.lastAction().empty() &&
        ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize(core_.lastAction().c_str()).x + 4.0F * em()) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", core_.lastAction().c_str());
    }
}

void App::drawToasts() {
    if (toasts_.empty()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = em() * 0.6F;
    float y = viewport->WorkPos.y + viewport->WorkSize.y - ImGui::GetFrameHeight() * 1.6F;
    for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
        const float wrap = std::min(viewport->WorkSize.x * 0.6F, 34.0F * em());
        const ImVec2 text = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, wrap);
        const ImVec2 size(text.x + pad * 3.4F, text.y + pad * 1.6F);
        const ImVec2 max(viewport->WorkPos.x + viewport->WorkSize.x - pad, y);
        const ImVec2 min(max.x - size.x, max.y - size.y);
        const float fade = std::min(1.0F, it->remaining / 0.6F);
        const auto level = static_cast<Status>(it->level);
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        bg.w = 0.96F * fade;
        dl->AddRectFilled(min, max, ImGui::GetColorU32(bg), 6.0F);
        ImVec4 edge = statusColor(level);
        edge.w = fade;
        dl->AddRect(min, max, ImGui::GetColorU32(edge), 6.0F, 0, 1.5F);
        dl->AddRectFilled(min, ImVec2(min.x + 4.0F, max.y), ImGui::GetColorU32(edge), 6.0F,
                          ImDrawFlags_RoundCornersLeft);
        ImVec4 fg = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        fg.w = fade;
        dl->AddText(nullptr, 0.0F, ImVec2(min.x + pad * 1.6F, min.y + pad * 0.8F), ImGui::GetColorU32(fg),
                    it->text.c_str(), nullptr, wrap);
        y = min.y - pad * 0.5F;
    }
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------
void App::drawDialogs() {
    if (dialog_ == Dialog::None) return;
    const char* name = kDialogNames[static_cast<std::size_t>(dialog_)];
    if (dialogJustOpened_) {
        ImGui::OpenPopup(name);
        dialogJustOpened_ = false;
    }
    switch (dialog_) {
        case Dialog::Unsaved:
            dialogUnsaved();
            break;
        case Dialog::NewSave:
            dialogNewSave();
            break;
        case Dialog::Preset:
            dialogPreset();
            break;
        case Dialog::Overwrite:
            dialogOverwrite();
            break;
        case Dialog::Notice:
            dialogNotice();
            break;
        case Dialog::Welcome:
            dialogWelcome();
            break;
        case Dialog::Shortcuts:
            dialogShortcuts();
            break;
        case Dialog::History:
            dialogHistory();
            break;
        case Dialog::Palette:
            dialogPalette();
            break;
        case Dialog::RepairPreview:
            dialogRepairPreview();
            break;
        case Dialog::Confirm:
            dialogConfirm();
            break;
        case Dialog::None:
            break;
    }
    if (dialog_ != Dialog::None && !ImGui::IsPopupOpen(name)) dialog_ = Dialog::None;
}

void App::dialogUnsaved() {
    if (!beginModal("Unsaved changes", 34.0F)) return;
    statusLabel(Status::Warning, "This file has unsaved changes.");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", ("\"" + displayName() +
                              "\" has changes that have not been written to disk. "
                              "If you continue without saving, they will be lost.")
                                 .c_str());
    const auto changes = core_.pendingChanges();
    if (!changes.empty()) {
        ImGui::Spacing();
        detail::mutedText("%zu change(s) pending", changes.size());
    }
    ImGui::Spacing();
    const std::function<void()> action = pendingAction_;
    if (ImGui::Button("Save and continue")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
        saveThen(action);
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard changes")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
        neverSaved_ = false;
        if (action) action();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogOverwrite() {
    if (!beginModal("Confirm overwrite", 34.0F)) return;
    statusLabel(Status::Warning, "This file already exists.");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", ("Replace \"" + overwritePath_.filename().string() + "\"?" +
                              (settings_.backupOnSave ? " The previous version will be kept as a .bak file."
                                                      : " Backups are turned off, so the old version will be lost."))
                                 .c_str());
    ImGui::Spacing();
    if (ImGui::Button("Replace")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
        if (overwriteAction_) overwriteAction_(overwritePath_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogNotice() {
    if (!beginModal("Notice", 34.0F)) return;
    ImGui::TextUnformatted(noticeTitle_.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", noticeText_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(8.0F * em(), 0.0F))) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogConfirm() {
    if (!beginModal("Please confirm", 34.0F)) return;
    ImGui::TextWrapped("%s", confirmText_.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Continue")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
        if (confirmAction_) confirmAction_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogPreset() {
    if (!beginModal("Preview preset", 46.0F)) return;
    const sm64::PresetInfo& info = sm64::presetInfo(presetRequest_.id);

    ImGui::TextUnformatted(info.name);
    ImGui::Separator();
    ImGui::TextWrapped("%s", info.description);
    ImGui::Spacing();

    if (info.scope != sm64::PresetScope::WholeFile) {
        static const char* slotNames[] = {"Slot A", "Slot B", "Slot C", "Slot D"};
        ImGui::SetNextItemWidth(8.0F * em());
        ImGui::Combo(info.scope == sm64::PresetScope::SlotPair ? "From" : "Slot", &presetRequest_.slot, slotNames, 4);
        if (info.scope == sm64::PresetScope::SlotPair) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(8.0F * em());
            ImGui::Combo("To", &presetRequest_.destSlot, slotNames, 4);
        }
    }
    if (presetRequest_.id == sm64::PresetId::RandomChallenge) {
        int seed = static_cast<int>(presetRequest_.seed & 0x7FFFFFFFU);
        ImGui::SetNextItemWidth(10.0F * em());
        if (ImGui::InputInt("Seed", &seed)) presetRequest_.seed = static_cast<std::uint32_t>(std::max(1, seed));
        ImGui::SameLine();
        if (ImGui::Button("Roll")) {
            presetRequest_.seed =
                static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFFLL) |
                1U;
        }
    }

    const sm64::PresetPlan plan = sm64::planPreset(core_.view(), presetRequest_);
    ImGui::Spacing();
    if (!plan.ok) {
        statusLabel(Status::Bad, plan.error.c_str());
    } else if (plan.unchanged) {
        statusLabel(Status::Info, "This preset would not change anything right now.");
    } else {
        if (info.destructive) {
            statusLabel(Status::Warning, "This replaces existing data. You can undo it with Ctrl+Z.");
        } else {
            statusLabel(Status::Info, "You can undo this with Ctrl+Z.");
        }
        ImGui::TextWrapped("%s", plan.summary.c_str());
        ImGui::Spacing();
        ImGui::Text("%zu field(s) will change:", plan.changes.size());
        const float height =
            std::min(14.0F * ImGui::GetTextLineHeightWithSpacing(), ImGui::GetMainViewport()->WorkSize.y * 0.4F);
        if (ImGui::BeginTable("##presetchanges", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0.0F, height))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("Old value", ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableSetupColumn("New value", ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < plan.changes.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", plan.changes[i].field.c_str());
                ImGui::TableNextColumn();
                detail::mutedText("%s", plan.changes[i].before.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(plan.changes[i].after.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    const bool canApply = plan.ok && !plan.unchanged && editsAllowed();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply preset")) {
        const sm64::PresetPlan applied = plan;
        if (core_.applyEdit(applied.title + ": " + applied.summary, applied.after)) {
            lastChanges_ = applied.changes;
            lastChangesTitle_ = applied.title;
            toast(applied.summary + " Ctrl+Z undoes it.", Status::Good);
            if (presetRequest_.id == sm64::PresetId::NewBlankEeprom ||
                presetRequest_.id == sm64::PresetId::NewOccupiedSlotA) {
                slot_ = 0;
            }
        }
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogNewSave() {
    if (!beginModal("Create a new save", 44.0F)) return;
    ImGui::TextWrapped(
        "Choose a starting point. The new file is created in memory with correct signatures, "
        "checksums and mirrored copies. It is not written to disk until you save it.");
    ImGui::Spacing();
    struct Choice {
        const char* name;
        const char* description;
    };
    static const std::array<Choice, 5> choices{{
        {"Blank valid EEPROM",
         "Four empty slots and a default menu block, exactly as the game writes a fresh cartridge."},
        {"New occupied Slot A", "Slot A exists but has no stars and no switches."},
        {"Slot A with all caps unlocked", "Occupied Slot A with the red, green and blue switches pressed."},
        {"Slot A with 70 stars", "Exactly 70 stars, caps and castle access. Handy for testing late-game areas."},
        {"Slot A with 120 stars", "Full completion: 105 main-course stars + 15 secret stars."},
    }};
    for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
        ImGui::PushID(i);
        ImGui::RadioButton(choices[static_cast<std::size_t>(i)].name, &newSaveChoice_, i);
        ImGui::Indent(em() * 1.6F);
        detail::mutedText("%s", choices[static_cast<std::size_t>(i)].description);
        ImGui::Unindent(em() * 1.6F);
        ImGui::PopID();
    }

    // Build the candidate and validate it before offering to create it.
    sm64::EepromBytes candidate = sm64::makeBlankEeprom();
    if (newSaveChoice_ >= 1) {
        sm64::PresetRequest request;
        request.id = sm64::PresetId::NewOccupiedSlotA;
        candidate = sm64::planPreset(candidate, request).after;
    }
    if (newSaveChoice_ >= 2) {
        sm64::PresetRequest request;
        request.slot = 0;
        request.id = newSaveChoice_ == 2   ? sm64::PresetId::UnlockAllCaps
                     : newSaveChoice_ == 3 ? sm64::PresetId::Progress70Stars
                                           : sm64::PresetId::Complete120Stars;
        candidate = sm64::planPreset(candidate, request).after;
    }
    const sm64::Report report = sm64::validateBytes(candidate);
    ImGui::Spacing();
    statusLabel(report.valid ? Status::Good : Status::Bad,
                report.valid ? "The new file validates: all 10 blocks have correct signatures and checksums."
                             : "The new file does not validate; it will not be created.");
    ImGui::Spacing();
    ImGui::BeginDisabled(!report.valid);
    if (ImGui::Button("Create")) {
        std::string error;
        if (core_.loadBytes(candidate, false, error)) {
            path_.clear();
            untitled_ = true;
            neverSaved_ = true;
            core_.setLastAction("Created a new save");
            slot_ = 0;
            page_ = Page::Dashboard;
            for (std::size_t i = 0; i < shownStars_.size(); ++i) {
                shownStars_[i] = static_cast<float>(core_.stars(static_cast<int>(i)).total);
            }
            toast("New save created. Use Save to write it to disk.", Status::Good);
        } else {
            notice("Could not create the file", error);
        }
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogWelcome() {
    if (!beginModal("Welcome", 42.0F)) return;
    struct Step {
        const char* title;
        const char* body;
    };
    static const std::array<Step, 4> steps{{
        {"Welcome to the SM64 Save Editor",
         "This editor changes Super Mario 64 save files (512-byte EEPROM dumps). You do not need to know "
         "anything about hexadecimal. Always keep a copy of your original save: the editor creates a .bak "
         "file for you whenever it overwrites something, but a second copy never hurts."},
        {"1. Open or create a save",
         "Use File > Open (or drag a save file into the window). Or choose File > New save to start from "
         "a blank or pre-filled file. Files must be exactly 512 bytes."},
        {"2. Edit with plain-language tools",
         "Save Slots lets you set stars and coin scores per course. Secret Stars covers the extra 15 stars. "
         "Castle Progress has the cap switches, keys and doors. Everything can be undone with Ctrl+Z."},
        {"3. Check, then save",
         "Validation & Repair shows whether every block is healthy. Save writes the file safely (a temporary "
         "file, then an atomic swap). Switch to Expert mode any time for the hex editor and GameShark tools."},
    }};
    tutorialStep_ = std::clamp(tutorialStep_, 0, static_cast<int>(steps.size()) - 1);
    const Step& step = steps[static_cast<std::size_t>(tutorialStep_)];
    ImGui::TextUnformatted(step.title);
    ImGui::Separator();
    ImGui::TextWrapped("%s", step.body);
    ImGui::Spacing();
    detail::mutedText("Step %d of %d", tutorialStep_ + 1, static_cast<int>(steps.size()));
    ImGui::Spacing();
    ImGui::BeginDisabled(tutorialStep_ == 0);
    if (ImGui::Button("Back")) --tutorialStep_;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (tutorialStep_ + 1 < static_cast<int>(steps.size())) {
        if (ImGui::Button("Next")) ++tutorialStep_;
    } else if (ImGui::Button("Get started")) {
        settings_.tutorialSeen = true;
        persistSettings();
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::SameLine();
    if (ImGui::Button("Skip")) {
        settings_.tutorialSeen = true;
        persistSettings();
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogShortcuts() {
    if (!beginModal("Keyboard shortcuts", 40.0F)) return;
    static const char* rows[][2] = {
        {"Ctrl+N", "New save"},
        {"Ctrl+O", "Open"},
        {"Ctrl+S", "Save"},
        {"Ctrl+Shift+S", "Save as"},
        {"Ctrl+Z", "Undo"},
        {"Ctrl+Y / Ctrl+Shift+Z", "Redo"},
        {"Ctrl+K", "Command palette"},
        {"Tab / Arrow keys", "Move between controls"},
        {"Space", "Toggle the focused checkbox"},
        {"Hex editor: 0-9, A-F", "Type a byte (two digits)"},
        {"Hex editor: arrows", "Move the cursor"},
        {"Hex editor: Shift+arrows", "Extend the selection"},
    };
    if (ImGui::BeginTable("##shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row[0]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row[1]);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::historyList() {
    const auto& stack = core_.undoStack();
    if (stack.empty() && core_.redoStack().empty()) {
        detail::mutedText("No edits yet.");
        return;
    }
    const float height =
        std::min(16.0F * ImGui::GetTextLineHeightWithSpacing(), ImGui::GetMainViewport()->WorkSize.y * 0.45F);
    if (ImGui::BeginChild("##historylist", ImVec2(0.0F, height), ImGuiChildFlags_Borders)) {
        int index = 0;
        for (auto it = core_.redoStack().begin(); it != core_.redoStack().end(); ++it, ++index) {
            ImGui::PushID(index);
            detail::mutedText("(undone) %s", it->description.c_str());
            ImGui::PopID();
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it, ++index) {
            ImGui::PushID(index);
            ImGui::Text("%d. %s", static_cast<int>(stack.size()) - (index - static_cast<int>(core_.redoStack().size())),
                        it->description.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void App::dialogHistory() {
    if (!beginModal("Edit history", 44.0F)) return;
    ImGui::TextWrapped("Newest first. Each meaningful action is one entry (typing in a field counts once).");
    ImGui::Spacing();
    historyList();
    ImGui::Spacing();
    ImGui::BeginDisabled(!core_.canUndo() || !editsAllowed());
    if (ImGui::Button("Undo")) actionUndo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!core_.canRedo() || !editsAllowed());
    if (ImGui::Button("Redo")) actionRedo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogRepairPreview() {
    if (!beginModal("Repair preview", 44.0F)) return;
    const auto plan = core_.planRepair();
    if (plan.empty()) {
        statusLabel(Status::Info,
                    "No block has exactly one valid copy, so there is nothing safe to repair automatically.");
    } else {
        ImGui::TextWrapped(
            "These blocks have one valid copy and one invalid copy. The invalid copy will be "
            "overwritten with the valid one:");
        ImGui::Spacing();
        for (std::size_t i = 0; i < plan.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::BulletText("%s", plan[i].description.c_str());
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    detail::mutedText("Pairs where both copies are invalid, or both are valid but different, are never changed here.");
    ImGui::Spacing();
    ImGui::BeginDisabled(plan.empty() || !editsAllowed());
    if (ImGui::Button("Repair")) {
        const auto done = core_.repair();
        toast("Repaired " + std::to_string(done.size()) + " block pair(s). Ctrl+Z undoes it.", Status::Good);
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

void App::dialogPalette() {
    if (!beginModal("Command palette", 36.0F)) return;
    struct Command {
        std::string name;
        std::function<void()> run;
        bool enabled;
    };
    std::vector<Command> commands;
    commands.push_back({"New save...", [this] { actionNew(); }, true});
    commands.push_back({"Open...", [this] { actionOpen(); }, true});
    commands.push_back({"Save", [this] { actionSave(); }, editsAllowed()});
    commands.push_back({"Save as...", [this] { actionSaveAs(); }, editsAllowed()});
    commands.push_back({"Undo", [this] { actionUndo(); }, core_.canUndo() && editsAllowed()});
    commands.push_back({"Redo", [this] { actionRedo(); }, core_.canRedo() && editsAllowed()});
    commands.push_back({"Create backup copy", [this] { createBackupCopy(); }, !path_.empty()});
    for (std::size_t i = 0; i < kPageCount; ++i) {
        const auto page = static_cast<Page>(i);
        if (expertOnly(page) && !settings_.expert) continue;
        commands.push_back({std::string("Go to ") + kPageNames[i], [this, page] { page_ = page; }, true});
    }
    commands.push_back({"Switch to Expert mode",
                        [this] {
                            setExpert(true);
                            persistSettings();
                        },
                        !settings_.expert});
    commands.push_back({"Switch to Beginner mode",
                        [this] {
                            setExpert(false);
                            persistSettings();
                        },
                        settings_.expert});
    commands.push_back({"Toggle dark/light theme",
                        [this] {
                            settings_.darkTheme = !settings_.darkTheme;
                            markStyleDirty();
                            persistSettings();
                        },
                        true});
    commands.push_back({"Show keyboard shortcuts", [this] { openDialog(Dialog::Shortcuts); }, true});
    commands.push_back({"Show edit history", [this] { openDialog(Dialog::History); }, true});
    for (const sm64::PresetInfo& info : sm64::presetCatalog()) {
        const sm64::PresetId id = info.id;
        commands.push_back({std::string("Preset: ") + info.name, [this, id] { openPresetDialog(id); }, editsAllowed()});
    }

    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##query", "Type a command...", paletteQuery_.data(), paletteQuery_.size());
    const std::string query = lower(paletteQuery_.data());

    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (query.empty() || lower(commands[i].name).find(query) != std::string::npos) matches.push_back(i);
    }
    const float height =
        std::min(12.0F * ImGui::GetTextLineHeightWithSpacing(), ImGui::GetMainViewport()->WorkSize.y * 0.45F);
    std::size_t chosen = commands.size();
    if (ImGui::BeginChild("##commands", ImVec2(0.0F, height), ImGuiChildFlags_Borders)) {
        for (std::size_t k = 0; k < matches.size(); ++k) {
            const std::size_t i = matches[k];
            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginDisabled(!commands[i].enabled);
            if (ImGui::Selectable(commands[i].name.c_str(), k == 0U)) chosen = i;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (matches.empty()) detail::mutedText("No matching command.");
    }
    ImGui::EndChild();
    if (!matches.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter) && commands[matches[0]].enabled) chosen = matches[0];

    if (chosen < commands.size()) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
        commands[chosen].run();
    } else if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        dialog_ = Dialog::None;
    }
    ImGui::EndPopup();
}

}  // namespace sm64ui
