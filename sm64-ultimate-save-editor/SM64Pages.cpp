// SM64Pages.cpp - bodies of the application pages.
//
// Every repeated widget is wrapped in PushID(slot / course / star / offset ...)
// so Dear ImGui never sees two visible items with the same ID. tests/test_ui_ids.cpp
// sweeps a virtual mouse over every page and asserts that ImGui's own conflict
// counter never fires.
#include "SM64PageUtil.hpp"

#include <cmath>
#include <cstdlib>

namespace sm64ui {
using namespace detail;

// ---------------------------------------------------------------------------
// Shared widgets
// ---------------------------------------------------------------------------
void App::slotSelector(const char* id) {
    ImGui::PushID(id);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::Selectable(slotName(i), slot_ == i, 0, ImVec2(5.5F * em(), 0.0F))) slot_ = i;
        ImGui::PopID();
    }
    ImGui::PopID();
}

void App::shortcutButtons() {
    FlowRow row;
    if (row.button("Open")) actionOpen();
    if (row.button("New save")) actionNew();
    if (row.button("Save", editsAllowed())) actionSave();
    if (row.button("Save as", editsAllowed())) actionSaveAs();
    if (row.button("Validate")) page_ = Page::Validation;
    if (row.button("Repair", editsAllowed() && !core_.planRepair().empty())) openDialog(Dialog::RepairPreview);
    if (row.button("Create backup", !path_.empty())) createBackupCopy();
    if (row.button("Export")) page_ = Page::Export;
    if (row.button("Undo", core_.canUndo() && editsAllowed())) actionUndo();
    if (row.button("Redo", core_.canRedo() && editsAllowed())) actionRedo();
}

void App::writeReport(const std::string& defaultName, const std::string& extension, const std::string& text) {
    std::filesystem::path suggested = path_.empty() ? std::filesystem::path(defaultName)
                                                    : path_.parent_path() / defaultName;
    const auto chosen = platform_.saveFileDialog("Save report", suggested, {extension});
    if (!chosen) return;
    std::filesystem::path target = *chosen;
    if (!target.has_extension()) target += extension;
    sm64::AtomicWriteOptions options;
    options.makeBackup = false;
    std::string error;
    if (sm64::atomicWriteFile(
            target, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
            options, error)) {
        toast("Saved " + target.filename().string(), Status::Good);
    } else {
        notice("Could not save the report", error);
    }
}

void App::slotCard(int slot, float width) {
    const Palette& pal = palette();
    const sm64::SlotImage image = core_.slotImage(slot);
    const sm64::StarSummary stars = sm64::summarizeStars(image);
    const std::uint32_t flags = sm64::slotFlags(image);
    const sm64::Report report = core_.validate();
    const sm64::PairStatus health = report.pairs[static_cast<std::size_t>(slot)].status;

    ImGui::PushID(slot);
    ImGui::BeginChild("##card", ImVec2(width, 0.0F),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);

    // Header
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = em() * 0.55F;
        drawStar(dl, ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5F), r, ImGui::GetColorU32(pal.star));
        ImGui::Dummy(ImVec2(r * 2.0F, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextUnformatted(slotName(slot));
        ImGui::SameLine();
        const bool occupied = (flags & sm64::save_flag::FileExists) != 0U;
        statusLabel(occupied ? Status::Good : Status::Info, occupied ? "In use" : "Empty");
    }

    // Completion ring with animated star count.
    const float ringR = em() * 2.3F;
    const ImVec2 ringTop = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ringR * 2.0F + 6.0F, ringR * 2.0F + 6.0F));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(ringTop.x + ringR + 3.0F, ringTop.y + ringR + 3.0F);
        const float shown = shownStars_[static_cast<std::size_t>(slot)];
        const ImVec4 track = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        drawRing(dl, c, ringR, em() * 0.45F, shown / static_cast<float>(sm64::kMaxStars),
                 ImGui::GetColorU32(track), ImGui::GetColorU32(pal.star));
        char number[16]{};
        std::snprintf(number, sizeof(number), "%d", static_cast<int>(std::lround(shown)));
        const ImVec2 ts = ImGui::CalcTextSize(number);
        dl->AddText(ImVec2(c.x - ts.x * 0.5F, c.y - ts.y * 0.5F - em() * 0.45F), ImGui::GetColorU32(ImGuiCol_Text), number);
        const char* of = "of 120";
        const ImVec2 os = ImGui::CalcTextSize(of);
        dl->AddText(ImVec2(c.x - os.x * 0.5F, c.y + em() * 0.1F), ImGui::GetColorU32(ImGuiCol_TextDisabled), of);
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Main %d/%d", stars.mainStars, 105);
    ImGui::Text("Secret %d/%d", stars.secretStars, static_cast<int>(sm64::kSecretStarCount));
    mutedText("Total %d/%d", stars.total, sm64::kMaxStars);
    ImGui::EndGroup();
    if (stars.exceedsRetail()) statusLabel(Status::Warning, "More than 120, or unmapped star bits are set");

    // Cap switches and keys, always with text.
    struct Cap { std::uint32_t mask; const char* name; ImVec4 color; };
    const Cap caps[3] = {{sm64::save_flag::HaveWingCap, "Wing", pal.wing},
                         {sm64::save_flag::HaveMetalCap, "Metal", pal.metal},
                         {sm64::save_flag::HaveVanishCap, "Vanish", pal.vanish}};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        const bool on = (flags & caps[i].mask) != 0U;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float s = ImGui::GetTextLineHeight();
        ImGui::Dummy(ImVec2(s, s));
        const ImU32 col = ImGui::GetColorU32(caps[i].color);
        if (on) {
            dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), col, 3.0F);
            dl->AddLine(ImVec2(p.x + s * 0.22F, p.y + s * 0.52F), ImVec2(p.x + s * 0.42F, p.y + s * 0.74F), IM_COL32_WHITE, 2.0F);
            dl->AddLine(ImVec2(p.x + s * 0.42F, p.y + s * 0.74F), ImVec2(p.x + s * 0.80F, p.y + s * 0.28F), IM_COL32_WHITE, 2.0F);
        } else {
            dl->AddRect(p, ImVec2(p.x + s, p.y + s), col, 3.0F, 0, 1.5F);
        }
        ImGui::SameLine(0.0F, 4.0F);
        ImGui::TextUnformatted(caps[i].name);
        tooltipText(on ? "Cap switch pressed: the matching coloured cap blocks are available."
                       : "Cap switch not pressed yet.");
    }
    const bool basement = (flags & (sm64::save_flag::UnlockedBasementDoor)) != 0U;
    const bool upstairs = (flags & (sm64::save_flag::UnlockedUpstairsDoor)) != 0U;
    ImGui::Text("Basement door: %s", basement ? "unlocked" : "locked");
    ImGui::Text("Upstairs door: %s", upstairs ? "unlocked" : "locked");

    // Per-course completion heat strip (15 courses).
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float cell = std::clamp(avail / 15.0F, 6.0F, em() * 1.0F);
        for (std::size_t c = 0; c < sm64::kMainCourseCount; ++c) {
            const float f = static_cast<float>(sm64::courseStarCount(image, c)) / 7.0F;
            ImVec4 col = pal.star;
            col.w = 0.12F + 0.88F * f;
            const ImVec2 a(p.x + static_cast<float>(c) * cell, p.y);
            dl->AddRectFilled(a, ImVec2(a.x + cell - 2.0F, a.y + cell), ImGui::GetColorU32(col), 2.0F);
        }
        ImGui::InvisibleButton("##heat", ImVec2(cell * 15.0F, cell));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            const float mx = ImGui::GetIO().MousePos.x - p.x;
            const auto idx = static_cast<std::size_t>(std::clamp(static_cast<int>(mx / cell), 0, 14));
            ImGui::SetTooltip("%s: %d / 7 stars", sm64::Courses[idx], sm64::courseStarCount(image, idx));
        }
    }

    // Health
    statusLabel(pairStatus(health), pairStatusText(health));

    if (ImGui::Button("Edit this slot")) {
        slot_ = slot;
        page_ = Page::Slots;
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void App::unsavedChangesPanel() {
    const auto changes = core_.pendingChanges();
    const std::string title = "Unsaved changes (" + std::to_string(changes.size()) + ")###unsaved";
    if (ImGui::CollapsingHeader(title.c_str(), changes.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
        if (changes.empty()) {
            mutedText("Nothing has changed since the file was opened or last saved.");
        } else if (ImGui::BeginTable("##pending", 3,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp |
                                         ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
                                     ImVec2(0.0F, textTableHeight(static_cast<int>(std::min<std::size_t>(changes.size(), 12U)))))) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 3.0F);
            ImGui::TableSetupColumn("Was", ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthStretch, 1.4F);
            ImGui::TableHeadersRow();
            const std::size_t limit = std::min<std::size_t>(changes.size(), 200U);
            for (std::size_t i = 0; i < limit; ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(changes[i].field.c_str());
                ImGui::TableNextColumn();
                mutedText("%s", changes[i].before.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(changes[i].after.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (changes.size() > limit) mutedText("... and %zu more", changes.size() - limit);
        }
    }
    if (!lastChanges_.empty() && ImGui::CollapsingHeader("What changed? (last preset)###lastpreset")) {
        ImGui::TextWrapped("%s changed %zu field(s):", lastChangesTitle_.c_str(), lastChanges_.size());
        const std::size_t limit = std::min<std::size_t>(lastChanges_.size(), 60U);
        for (std::size_t i = 0; i < limit; ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::BulletText("%s: %s -> %s", lastChanges_[i].field.c_str(), lastChanges_[i].before.c_str(),
                              lastChanges_[i].after.c_str());
            ImGui::PopID();
        }
        if (lastChanges_.size() > limit) mutedText("... and %zu more", lastChanges_.size() - limit);
    }
    if (ImGui::CollapsingHeader("Edit history###history")) historyList();
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------
void App::pageDashboard() {
    const sm64::Report report = core_.validate();
    std::size_t healthyPairs = 0;
    std::size_t blocksValid = 0;
    for (const sm64::PairReport& pair : report.pairs) healthyPairs += pair.status == sm64::PairStatus::BothValidIdentical ? 1U : 0U;
    for (std::size_t id = 0; id < sm64::kBlockCount; ++id) blocksValid += report.block(id).valid() ? 1U : 0U;

    Status overall = Status::Good;
    for (const sm64::PairReport& pair : report.pairs) {
        if (pair.status == sm64::PairStatus::BothInvalid) overall = Status::Bad;
        else if (pair.status != sm64::PairStatus::BothValidIdentical && overall != Status::Bad) overall = Status::Warning;
    }

    // File summary
    ImGui::BeginChild("##fileinfo", ImVec2(0.0F, 0.0F),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (ImGui::BeginTable("##filetable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 11.0F * em());
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        labelValue("File", displayName() + (path_.empty() ? std::string(" (not saved yet)") : std::string()));
        if (!path_.empty()) labelValue("Location", path_.parent_path().string());
        labelValue("Type", "Super Mario 64 EEPROM save (4 Kbit)");
        labelValue("Size", "512 bytes (exact)");
        labelValue("Byte order", sm64::byteOrderName(core_.detectedOrder()));
        labelValue("CRC-32 (file ID)", hex(sm64::crc32(core_.view()), 8));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Validation");
        ImGui::TableNextColumn();
        const std::string text = overall == Status::Good
                                     ? "All 10 blocks valid"
                                     : std::to_string(blocksValid) + " of 10 blocks valid";
        statusLabel(overall, text.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Primary / backup");
        ImGui::TableNextColumn();
        const std::string health = std::to_string(healthyPairs) + " of 5 pairs healthy and identical";
        statusLabel(healthyPairs == 5U ? Status::Good : Status::Warning, health.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Unsaved changes");
        ImGui::TableNextColumn();
        statusLabel(dirtyForUser() ? Status::Warning : Status::Good, dirtyForUser() ? "Yes - not saved to disk" : "None");
        labelValue("Last action", core_.lastAction().empty() ? std::string("None yet") : core_.lastAction());
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    shortcutButtons();
    ImGui::Spacing();

    // Responsive grid of slot cards.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float minCard = 17.0F * em();
    const int columns = std::clamp(static_cast<int>((avail + spacing) / (minCard + spacing)), 1, 4);
    const float cardWidth = (avail - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    for (int slot = 0; slot < 4; ++slot) {
        slotCard(slot, cardWidth);
        if ((slot + 1) % columns != 0 && slot != 3) ImGui::SameLine();
    }
    ImGui::Spacing();

    unsavedChangesPanel();

    if (ImGui::CollapsingHeader("Notes for this save (stored in a separate file)###notes")) {
        mutedText("Notes never go into the EEPROM. They are saved as \"<save file>.sm64notes.txt\".");
        ImGui::BeginDisabled(path_.empty());
        if (ImGui::InputTextMultiline("##notes", notes_.data(), notes_.size(), ImVec2(-1.0F, 5.0F * ImGui::GetTextLineHeightWithSpacing()))) {
            notesDirty_ = true;
        }
        ImGui::BeginDisabled(!notesDirty_);
        if (ImGui::Button("Save notes")) saveNotes();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (path_.empty()) mutedText("Save the file first to attach notes.");
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void App::courseTable(int slot) {
    const Palette& pal = palette();
    const sm64::SlotImage image = core_.slotImage(slot);
    const sm64::SlotImage saved = slotOf(core_.savedImage(), slot);
    const std::string filter = lower(courseFilter_.data());

    ImGui::PushID(slot);
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    // Give the table its full content height so the page is the only vertical scroller
    // (the table itself scrolls horizontally only when the window is too narrow).
    int visibleRows = 0;
    for (std::size_t course = 0; course < sm64::kMainCourseCount; ++course) {
        if (filter.empty() || lower(sm64::Courses[course]).find(filter) != std::string::npos) ++visibleRows;
    }
    const ImGuiStyle& tableStyle = ImGui::GetStyle();
    const float rowHeight = ImGui::GetFrameHeight() + tableStyle.CellPadding.y * 2.0F;
    const float headerHeight = ImGui::GetTextLineHeight() + tableStyle.CellPadding.y * 2.0F;
    const float tableHeight = headerHeight + rowHeight * static_cast<float>(std::max(visibleRows, 1)) +
                              tableStyle.ScrollbarSize + 4.0F;
    if (ImGui::BeginTable("##courses", 12, flags, ImVec2(0.0F, tableHeight))) {
        ImGui::TableSetupColumn("Course", ImGuiTableColumnFlags_WidthFixed, 14.0F * em());
        ImGui::TableSetupColumn("Coins", ImGuiTableColumnFlags_WidthFixed, 5.2F * em());
        for (int s = 0; s < 7; ++s) {
            char label[8]{};
            std::snprintf(label, sizeof(label), "%d", s + 1);
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, 2.0F * em());
        }
        ImGui::TableSetupColumn("All", ImGuiTableColumnFlags_WidthFixed, 2.8F * em());
        ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 3.6F * em());
        ImGui::TableHeadersRow();

        for (std::size_t course = 0; course < sm64::kMainCourseCount; ++course) {
            if (!filter.empty() && lower(sm64::Courses[course]).find(filter) == std::string::npos) continue;
            ImGui::PushID(static_cast<int>(course));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const int count = sm64::courseStarCount(image, course);
            ImGui::TextUnformatted(sm64::Courses[course]);
            ImGui::SameLine();
            ImGui::TextDisabled("%d/7", count);

            ImGui::TableNextColumn();
            int coins = sm64::coinScore(image, course);
            const bool coinsChanged = coins != sm64::coinScore(saved, course);
            if (coinsChanged) ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.changed);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::BeginDisabled(!editsAllowed());
            if (ImGui::InputInt("##Coins", &coins, 0, 0)) core_.setCoinScore(slot, course, coins);
            ImGui::EndDisabled();
            if (coinsChanged) ImGui::PopStyleColor();
            tooltipText("Coin high score for this course (0-255).");

            for (int star = 0; star < 7; ++star) {
                ImGui::TableNextColumn();
                const bool collected = ((sm64::courseByte(image, course) >> star) & 1U) != 0U;
                const bool was = ((sm64::courseByte(saved, course) >> star) & 1U) != 0U;
                bool value = collected;
                const float offset = (ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight()) * 0.5F;
                if (offset > 0.0F) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
                ImGui::PushID(slot);
                ImGui::PushID(static_cast<int>(course));
                ImGui::PushID(star);
                if (was != collected) ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.changed);
                ImGui::BeginDisabled(!editsAllowed());
                if (ImGui::Checkbox("##Collected", &value)) core_.setCourseStar(slot, course, star, value);
                ImGui::EndDisabled();
                if (was != collected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::SetTooltip("%s - star %d%s", sm64::Courses[course], star + 1,
                                      star == 6 ? " (the 100-coin star)" : "");
                }
                ImGui::PopID();
                ImGui::PopID();
                ImGui::PopID();
            }

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!editsAllowed());
            if (ImGui::SmallButton("All##all")) core_.setCourseStars(slot, course, sm64::kStarBitsMask);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Clear##clear")) core_.setCourseStars(slot, course, 0);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void App::pageSlots() {
    slotSelector("##slotpick");
    ImGui::Spacing();

    const sm64::SlotImage image = core_.slotImage(slot_);
    const sm64::StarSummary stars = sm64::summarizeStars(image);
    const sm64::Report report = core_.validate();
    const sm64::PairReport& pair = report.pairs[static_cast<std::size_t>(slot_)];

    ImGui::Text("%s: %d stars total (%d main course, %d secret)", slotName(slot_), stars.total, stars.mainStars, stars.secretStars);
    if (pair.status != sm64::PairStatus::BothValidIdentical) {
        statusLabel(pairStatus(pair.status), pairStatusText(pair.status));
        ImGui::SameLine();
        if (ImGui::SmallButton("Open Validation & Repair")) page_ = Page::Validation;
    }
    mutedText("Edits change both the primary and backup copies and rebuild their checksums.");
    ImGui::Spacing();

    FlowRow row;
    if (row.button("Presets...", editsAllowed())) ImGui::OpenPopup("##slotpresets");
    if (row.button("Unlock all caps", editsAllowed())) openPresetDialog(sm64::PresetId::UnlockAllCaps);
    if (row.button("Complete all courses", editsAllowed())) {
        core_.editSlot(slot_, std::string(slotName(slot_)) + ": all main-course stars collected", [](sm64::SlotImage& img) {
            for (std::size_t c = 0; c < sm64::kMainCourseCount; ++c) sm64::setCourseStarBits(img, c, sm64::kStarBitsMask);
        });
    }
    if (row.button("Clear all courses", editsAllowed())) {
        core_.editSlot(slot_, std::string(slotName(slot_)) + ": all main-course stars cleared", [](sm64::SlotImage& img) {
            for (std::size_t c = 0; c < sm64::kMainCourseCount; ++c) sm64::setCourseStarBits(img, c, 0);
        });
    }
    if (ImGui::BeginPopup("##slotpresets")) {
        for (const sm64::PresetInfo& info : sm64::presetCatalog()) {
            if (info.scope == sm64::PresetScope::WholeFile) continue;
            ImGui::PushID(static_cast<int>(info.id));
            if (ImGui::MenuItem(info.name)) openPresetDialog(info.id);
            tooltipText(info.description);
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(14.0F * em());
    ImGui::InputTextWithHint("##filter", "Filter courses...", courseFilter_.data(), courseFilter_.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear filter")) courseFilter_.fill('\0');
    ImGui::Spacing();

    courseTable(slot_);
    ImGui::Spacing();
    mutedText("Columns 1-7 are the course's seven stars (the 7th is the 100-coin star). Amber highlights "
                        "values that differ from the saved file. The cannon-open flag shares these bytes and is never changed here.");
}

// ---------------------------------------------------------------------------
// Secret stars
// ---------------------------------------------------------------------------
void App::pageSecretStars() {
    slotSelector("##secretpick");
    ImGui::Spacing();
    const sm64::SlotImage image = core_.slotImage(slot_);
    const sm64::SlotImage saved = slotOf(core_.savedImage(), slot_);
    const sm64::StarSummary stars = sm64::summarizeStars(image);

    ImGui::BeginChild("##secretsummary", ImVec2(0.0F, 0.0F),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::Text("Main-course stars: %d / 105", stars.mainStars);
    ImGui::Text("Secret stars: %d / %d", stars.secretStars, static_cast<int>(sm64::kSecretStarCount));
    ImGui::Text("Overall: %d / %d (a normal completed retail save has exactly 120)", stars.total, sm64::kMaxStars);
    char overlay[32]{};
    std::snprintf(overlay, sizeof(overlay), "%d / 120", stars.total);
    ImGui::ProgressBar(static_cast<float>(stars.total) / static_cast<float>(sm64::kMaxStars), ImVec2(-FLT_MIN, 0.0F), overlay);
    if (stars.exceedsRetail()) {
        statusLabel(Status::Warning, (std::to_string(stars.unmappedExtra) +
                                      " extra star bit(s) are set that no retail star uses. The game still counts them.").c_str());
    }
    ImGui::EndChild();
    ImGui::Spacing();

    FlowRow row;
    if (row.button("Set 120 stars...", editsAllowed())) openPresetDialog(sm64::PresetId::Complete120Stars);
    if (row.button("Collect all secret stars", editsAllowed())) {
        core_.editSlot(slot_, std::string(slotName(slot_)) + ": all secret stars collected", [](sm64::SlotImage& img) {
            for (const sm64::SecretStar& s : sm64::kSecretStars) sm64::setSecretStar(img, s, true);
        });
    }
    if (row.button("Clear all secret stars", editsAllowed())) {
        core_.editSlot(slot_, std::string(slotName(slot_)) + ": all secret stars cleared", [](sm64::SlotImage& img) {
            for (const sm64::SecretStar& s : sm64::kSecretStars) sm64::setSecretStar(img, s, false);
        });
    }
    ImGui::Spacing();

    const char* group = nullptr;
    for (std::size_t i = 0; i < sm64::kSecretStarCount; ++i) {
        if (group == nullptr || std::string(group) != sm64::kSecretStars[i].group) {
            group = sm64::kSecretStars[i].group;
            ImGui::SeparatorText(group);
        }
        const sm64::SecretStar& star = sm64::kSecretStars[i];
        bool value = sm64::secretStarSet(image, star);
        const bool was = sm64::secretStarSet(saved, star);
        ImGui::PushID(static_cast<int>(i));
        if (value != was) ImGui::PushStyleColor(ImGuiCol_FrameBg, palette().changed);
        ImGui::BeginDisabled(!editsAllowed());
        if (ImGui::Checkbox(star.name, &value)) core_.setSecretStarFlag(slot_, i, value);
        ImGui::EndDisabled();
        if (value != was) ImGui::PopStyleColor();
        if (settings_.expert) {
            ImGui::SameLine();
            char where[64]{};
            if (star.storage == sm64::StarStorage::CourseByte) {
                std::snprintf(where, sizeof(where), "courseStars[%d] bit %d", star.byteIndex, star.bit);
            } else {
                std::snprintf(where, sizeof(where), "flags bit %d", star.bit);
            }
            mutedText("(%s)", where);
        }
        tooltipText(star.storage == sm64::StarStorage::CastleFlag
                        ? "A castle reward star, stored in the slot's flags word."
                        : "A star from a bonus stage, stored in that stage's star byte.");
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// Castle progress
// ---------------------------------------------------------------------------
void App::pageCastle() {
    slotSelector("##castlepick");
    ImGui::Spacing();
    const sm64::SlotImage image = core_.slotImage(slot_);
    const sm64::SlotImage saved = slotOf(core_.savedImage(), slot_);
    const std::uint32_t flags = sm64::slotFlags(image);
    const Palette& pal = palette();

    auto flagBox = [&](std::uint32_t mask, const ImVec4* swatch) {
        const sm64::FlagInfo* info = findFlag(mask);
        if (info == nullptr) return;
        bool value = (flags & mask) != 0U;
        const bool was = (sm64::slotFlags(saved) & mask) != 0U;
        ImGui::PushID(static_cast<int>(mask));
        if (swatch != nullptr) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float s = ImGui::GetFrameHeight();
            dl->AddRectFilled(p, ImVec2(p.x + em() * 0.5F, p.y + s), ImGui::GetColorU32(*swatch), 3.0F);
            ImGui::Dummy(ImVec2(em() * 0.5F, s));
            ImGui::SameLine();
        }
        if (value != was) ImGui::PushStyleColor(ImGuiCol_FrameBg, pal.changed);
        ImGui::BeginDisabled(!editsAllowed());
        if (ImGui::Checkbox(info->name, &value)) core_.setFlag(slot_, mask, value);
        ImGui::EndDisabled();
        if (value != was) ImGui::PopStyleColor();
        tooltipText(info->tooltip);
        ImGui::PopID();
    };

    ImGui::SeparatorText("Save file");
    flagBox(sm64::save_flag::FileExists, nullptr);

    ImGui::SeparatorText("Cap switches");
    ImGui::TextWrapped("A cap switch only unlocks the matching coloured cap blocks in the game. It does not make Mario wear a "
                       "cap permanently. For the Wing Cap, flying still needs a triple jump or a cannon launch.");
    flagBox(sm64::save_flag::HaveWingCap, &pal.wing);
    flagBox(sm64::save_flag::HaveMetalCap, &pal.metal);
    flagBox(sm64::save_flag::HaveVanishCap, &pal.vanish);

    ImGui::SeparatorText("Keys and key doors");
    flagBox(sm64::save_flag::HaveKey1, nullptr);
    flagBox(sm64::save_flag::HaveKey2, nullptr);
    flagBox(sm64::save_flag::UnlockedBasementDoor, nullptr);
    flagBox(sm64::save_flag::UnlockedUpstairsDoor, nullptr);
    if (((flags & sm64::save_flag::HaveKey1) != 0U && (flags & sm64::save_flag::UnlockedBasementDoor) != 0U) ||
        ((flags & sm64::save_flag::HaveKey2) != 0U && (flags & sm64::save_flag::UnlockedUpstairsDoor) != 0U)) {
        statusLabel(Status::Info, "A key is marked as held and its door as unlocked. The game clears the key when it opens the door.");
    }

    ImGui::SeparatorText("Castle events");
    flagBox(sm64::save_flag::DddMovedBack, nullptr);
    flagBox(sm64::save_flag::MoatDrained, nullptr);

    ImGui::SeparatorText("Star doors");
    for (const std::uint32_t mask :
         {sm64::save_flag::UnlockedPssDoor, sm64::save_flag::UnlockedWfDoor, sm64::save_flag::UnlockedCcmDoor,
          sm64::save_flag::UnlockedJrbDoor, sm64::save_flag::UnlockedBitdwDoor, sm64::save_flag::UnlockedBitfsDoor,
          sm64::save_flag::Unlocked50StarDoor}) {
        flagBox(mask, nullptr);
    }

    ImGui::SeparatorText("Lost cap");
    ImGui::TextWrapped("Where Mario's cap currently is after being lost in a stage.");
    for (const std::uint32_t mask : {sm64::save_flag::CapOnGround, sm64::save_flag::CapOnKlepto,
                                     sm64::save_flag::CapOnUkiki, sm64::save_flag::CapOnMrBlizzard}) {
        flagBox(mask, nullptr);
    }
    if (settings_.expert) {
        mutedText("Lost-cap level byte: 0x%02X, area byte: 0x%02X (not editable here)",
                            image[sm64::slot_field::CapLevel], image[sm64::slot_field::CapArea]);
    }

    ImGui::Spacing();
    FlowRow row;
    if (row.button("Unlock all caps...", editsAllowed())) openPresetDialog(sm64::PresetId::UnlockAllCaps);
    if (row.button("Unlock castle access...", editsAllowed())) openPresetDialog(sm64::PresetId::UnlockCastleAccess);
}

}  // namespace sm64ui
