// SM64Pages2.cpp - validation, raw hex editor, export, GameShark, settings, about.
#include "SM64PageUtil.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sm64ui {
using namespace detail;

namespace {

ImVec4 regionColor(std::size_t blockId, bool dark) {
    // Alternating hues so neighbouring blocks are distinguishable; backups are slightly dimmer.
    static const float hues[5] = {0.58F, 0.12F, 0.80F, 0.38F, 0.0F};
    const std::size_t group = blockId < 8U ? blockId / 2U : 4U;
    const bool backup = blockId < 8U ? (blockId % 2U) != 0U : blockId == 9U;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    ImGui::ColorConvertHSVtoRGB(hues[group], dark ? 0.55F : 0.60F, dark ? (backup ? 0.55F : 0.80F) : (backup ? 0.80F : 0.95F), r, g, b);
    return ImVec4(r, g, b, dark ? 0.20F : 0.26F);
}

int hexDigit(ImWchar c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A') + 10;
    return -1;
}

// Parses "DA 41", "0xDA,0x41", "da41" ... into bytes. Returns false on any invalid character or odd digit count.
bool parseHexBytes(const char* text, std::vector<std::uint8_t>& out) {
    out.clear();
    std::string digits;
    const std::string s(text != nullptr ? text : "");
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '0' && i + 1U < s.size() && (s[i + 1U] == 'x' || s[i + 1U] == 'X')) {
            ++i;
            continue;
        }
        if (hexDigit(static_cast<ImWchar>(static_cast<unsigned char>(c))) < 0) return false;
        digits += c;
    }
    if (digits.empty() || digits.size() % 2U != 0U) return false;
    for (std::size_t i = 0; i < digits.size(); i += 2U) {
        const int hi = hexDigit(static_cast<ImWchar>(static_cast<unsigned char>(digits[i])));
        const int lo = hexDigit(static_cast<ImWchar>(static_cast<unsigned char>(digits[i + 1U])));
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Validation & repair
// ---------------------------------------------------------------------------
void App::pageValidation() {
    const sm64::Report report = core_.validate();
    Status overall = Status::Good;
    std::size_t recoverable = 0;
    for (const sm64::PairReport& pair : report.pairs) {
        if (pair.status == sm64::PairStatus::BothInvalid) overall = Status::Bad;
        else if (pair.status != sm64::PairStatus::BothValidIdentical && overall != Status::Bad) overall = Status::Warning;
        recoverable += pair.recoverable ? 1U : 0U;
    }

    ImGui::BeginChild("##valsummary", ImVec2(0.0F, 0.0F),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    if (overall == Status::Good) {
        statusLabel(Status::Good, "Everything checks out: all 10 blocks have valid signatures and checksums, and every backup matches its primary.");
    } else if (overall == Status::Warning) {
        statusLabel(Status::Warning, "Some blocks need attention, but nothing is lost. Review the items below.");
    } else {
        statusLabel(Status::Bad, "At least one block has no valid copy. The game would erase it on boot.");
    }
    mutedText("Byte order: %s", sm64::byteOrderName(core_.detectedOrder()));
    ImGui::EndChild();
    ImGui::Spacing();

    FlowRow row;
    if (row.button("Repair all recoverable...", editsAllowed() && recoverable > 0U)) openDialog(Dialog::RepairPreview);
    if (row.button("Save validation report (.txt)")) {
        writeReport("sm64-validation.txt", ".txt", sm64::buildValidationText(core_.view(), displayName()));
    }
    if (row.button("Save JSON report")) {
        writeReport("sm64-report.json", ".json", sm64::buildJsonReport(core_.view(), displayName(), core_.detectedOrder()));
    }
    ImGui::Spacing();

    ImGui::TextWrapped("Each slot and the menu block are stored twice (a primary and a backup copy). The game checks both and "
                       "restores one from the other when only one is damaged. A repair here does the same, and never "
                       "guesses when it cannot be sure.");
    ImGui::Spacing();

    for (std::size_t pair = 0; pair < sm64::kPairCount; ++pair) {
        const sm64::PairReport& p = report.pairs[pair];
        ImGui::PushID(static_cast<int>(pair));
        ImGui::BeginChild("##pair", ImVec2(0.0F, 0.0F),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextUnformatted(pairName(pair).c_str());
        ImGui::SameLine();
        statusLabel(pairStatus(p.status), pairStatusText(p.status));
        ImGui::TextWrapped("%s", p.text.c_str());

        FlowRow actions;
        switch (p.status) {
            case sm64::PairStatus::OnlyPrimaryValid:
                if (actions.button("Restore backup from primary", editsAllowed())) {
                    std::string error;
                    core_.resolvePair(pair, true, error);
                }
                break;
            case sm64::PairStatus::OnlyBackupValid:
                if (actions.button("Restore primary from backup", editsAllowed())) {
                    std::string error;
                    core_.resolvePair(pair, false, error);
                }
                break;
            case sm64::PairStatus::BothValidDifferent:
                if (actions.button("Keep primary copy", editsAllowed())) {
                    std::string error;
                    core_.resolvePair(pair, true, error);
                }
                if (actions.button("Keep backup copy", editsAllowed())) {
                    std::string error;
                    core_.resolvePair(pair, false, error);
                }
                if (actions.button(diffPair_ == static_cast<int>(pair) ? "Hide differences" : "Show differences")) {
                    diffPair_ = diffPair_ == static_cast<int>(pair) ? -1 : static_cast<int>(pair);
                }
                break;
            case sm64::PairStatus::BothInvalid:
                if (!settings_.expert) {
                    mutedText("Switch to Expert mode for advanced options.");
                } else {
                    ImGui::Checkbox("I understand a rebuilt checksum does not make the data correct", &advancedChecksumAck_);
                    if (actions.button("Rebuild checksum of primary (advanced)", editsAllowed() && advancedChecksumAck_)) {
                        core_.rebuildChecksum(pair < sm64::kSlotCount ? pair * 2U : 8U);
                    }
                    if (actions.button("Rebuild checksum of backup (advanced)", editsAllowed() && advancedChecksumAck_)) {
                        core_.rebuildChecksum(pair < sm64::kSlotCount ? pair * 2U + 1U : 9U);
                    }
                    mutedText("Rebuilding only recomputes the signature and checksum. If the bytes themselves are damaged, "
                                        "the file will pass validation but still contain wrong data.");
                }
                break;
            case sm64::PairStatus::BothValidIdentical: break;
        }

        if (diffPair_ == static_cast<int>(pair) && p.status == sm64::PairStatus::BothValidDifferent) {
            const sm64::BlockInfo pi = sm64::blockInfo(pair < sm64::kSlotCount ? pair * 2U : 8U);
            const sm64::BlockInfo bi = sm64::blockInfo(pair < sm64::kSlotCount ? pair * 2U + 1U : 9U);
            if (ImGui::BeginTable("##diff", 4,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0F, textTableHeight(static_cast<int>(std::min<std::size_t>(p.differingOffsets.size(), 12U)))))) {
                ImGui::TableSetupColumn("Offset (primary)");
                ImGui::TableSetupColumn("Primary");
                ImGui::TableSetupColumn("Backup");
                ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 3.0F);
                ImGui::TableHeadersRow();
                const std::size_t limit = std::min<std::size_t>(p.differingOffsets.size(), 56U);
                for (std::size_t i = 0; i < limit; ++i) {
                    const std::size_t rel = p.differingOffsets[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%03zX", pi.offset + rel);
                    ImGui::TableNextColumn();
                    ImGui::Text("%02X", core_.view()[pi.offset + rel]);
                    ImGui::TableNextColumn();
                    ImGui::Text("%02X", core_.view()[bi.offset + rel]);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(sm64::describeOffset(pi.offset + rel).field.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    if (settings_.expert) {
        ImGui::SeparatorText("Every block");
        if (ImGui::BeginTable("##blocks", 7,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_SizingFixedFit,
                              ImVec2(0.0F, textTableHeight(10)))) {
            ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, 9.0F * em());
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 5.0F * em());
            ImGui::TableSetupColumn("Signature", ImGuiTableColumnFlags_WidthFixed, 9.0F * em());
            ImGui::TableSetupColumn("Stored", ImGuiTableColumnFlags_WidthFixed, 5.0F * em());
            ImGui::TableSetupColumn("Calculated", ImGuiTableColumnFlags_WidthFixed, 6.0F * em());
            ImGui::TableSetupColumn("Match", ImGuiTableColumnFlags_WidthFixed, 7.0F * em());
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 7.0F * em());
            ImGui::TableHeadersRow();
            for (std::size_t id = 0; id < sm64::kBlockCount; ++id) {
                const sm64::BlockInfo info = sm64::blockInfo(id);
                const sm64::BlockState& s = report.block(id);
                ImGui::PushID(static_cast<int>(id));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(blockName(id).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("0x%03zX", info.offset);
                ImGui::TableNextColumn();
                statusLabel(s.magic ? Status::Good : Status::Bad,
                            (std::string(s.magic ? "OK " : "BAD ") + "0x" + hex(s.storedMagic, 4)).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", s.stored);
                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", s.computed);
                ImGui::TableNextColumn();
                statusLabel(s.checksum ? Status::Good : Status::Bad, s.checksum ? "Equal" : "Different");
                ImGui::TableNextColumn();
                statusLabel(s.valid() ? Status::Good : Status::Bad, s.valid() ? "Valid" : "Invalid");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        mutedText("Checksum = 16-bit sum of every byte in the block except the last two. Slot signature 0x4441 (\"DA\"), menu signature 0x4849 (\"HI\").");
    }
}

// ---------------------------------------------------------------------------
// Raw hex editor
// ---------------------------------------------------------------------------
void App::hexCommitByte(std::size_t offset, std::uint8_t value) {
    if (hexReadOnly_ || !editsAllowed() || offset >= sm64::Size) return;
    if (!hexAutoFix_) {
        core_.set(offset, value);
        return;
    }
    sm64::EepromBytes after = core_.view();
    after[offset] = value;
    const sm64::RegionInfo region = sm64::describeOffset(offset);
    const sm64::BlockInfo info = sm64::blockInfo(region.blockId);
    sm64::signBlock(after.data() + info.offset, info.size, info.magic);
    core_.applyEdit("Edit byte 0x" + hex(static_cast<unsigned>(offset), 3) + " and rebuild checksum", after,
                    "byte:" + std::to_string(offset));
}

void App::hexSearchRun() {
    std::vector<std::uint8_t> pattern;
    hexMatches_.clear();
    hexMatchIndex_ = -1;
    if (!parseHexBytes(hexSearch_.data(), pattern)) {
        hexMessage_ = "Enter hex bytes such as: 44 41  (an even number of hex digits).";
        return;
    }
    hexMatchLength_ = static_cast<int>(pattern.size());
    const auto& data = core_.view();
    for (std::size_t i = 0; i + pattern.size() <= data.size(); ++i) {
        if (std::equal(pattern.begin(), pattern.end(), data.begin() + static_cast<std::ptrdiff_t>(i))) {
            hexMatches_.push_back(static_cast<int>(i));
        }
    }
    if (hexMatches_.empty()) {
        hexMessage_ = "No matches.";
        return;
    }
    hexMatchIndex_ = 0;
    hexCursor_ = hexAnchor_ = hexMatches_[0];
    hexScrollRow_ = hexCursor_ / 16;
    hexMessage_ = std::to_string(hexMatches_.size()) + " match(es).";
}

void App::hexCopySelection() {
    const int lo = std::min(hexAnchor_, hexCursor_);
    const int hi = std::max(hexAnchor_, hexCursor_);
    std::string text;
    for (int i = lo; i <= hi; ++i) {
        if (i > lo) text += ' ';
        text += hex(core_.view()[static_cast<std::size_t>(i)], 2);
    }
    ImGui::SetClipboardText(text.c_str());
    hexMessage_ = "Copied " + std::to_string(hi - lo + 1) + " byte(s).";
}

void App::hexPasteClipboard() {
    if (hexReadOnly_ || !editsAllowed()) return;
    std::vector<std::uint8_t> bytes;
    const char* clip = ImGui::GetClipboardText();
    if (!parseHexBytes(clip, bytes)) {
        hexMessage_ = "The clipboard does not contain valid hex bytes.";
        return;
    }
    const int start = std::min(hexAnchor_, hexCursor_);
    if (!core_.pasteBytes(static_cast<std::size_t>(start), bytes)) {
        hexMessage_ = "Not pasted: the bytes would run past the end of the 512-byte file.";
        return;
    }
    hexMessage_ = "Pasted " + std::to_string(bytes.size()) + " byte(s) at 0x" + hex(static_cast<unsigned>(start), 3) + ".";
}

void App::pageRawData() {
    const Palette& pal = palette();
    const bool dark = settings_.darkTheme;
    const sm64::EepromBytes& data = core_.view();
    const sm64::EepromBytes& saved = core_.savedImage();
    const sm64::Report report = core_.validate();
    constexpr int kRows = 32;

    ImGui::TextWrapped("Raw edits do not rebuild checksums, so a hand edit shows up as an invalid block below. Turn on "
                       "\"rebuild checksum\" if you want each edit to keep its block valid.");
    ImGui::Spacing();

    // ---- toolbar ----
    ImGui::SetNextItemWidth(6.0F * em());
    bool jump = ImGui::InputTextWithHint("##jump", "Offset", hexJump_.data(), hexJump_.size(),
                                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    jump = ImGui::Button("Go to") || jump;
    if (jump) {
        const unsigned long value = std::strtoul(hexJump_.data(), nullptr, 16);
        if (value < sm64::Size) {
            hexCursor_ = hexAnchor_ = static_cast<int>(value);
            hexScrollRow_ = hexCursor_ / 16;
            hexPendingNibble_ = -1;
            hexMessage_.clear();
        } else {
            hexMessage_ = "Offset must be 000-1FF.";
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(10.0F * em());
    const bool searchEnter = ImGui::InputTextWithHint("##hexsearch", "Find bytes: 44 41", hexSearch_.data(), hexSearch_.size(),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Find") || searchEnter) hexSearchRun();
    ImGui::SameLine();
    ImGui::BeginDisabled(hexMatches_.empty());
    if (ImGui::Button("Prev") && !hexMatches_.empty()) {
        hexMatchIndex_ = (hexMatchIndex_ + static_cast<int>(hexMatches_.size()) - 1) % static_cast<int>(hexMatches_.size());
        hexCursor_ = hexAnchor_ = hexMatches_[static_cast<std::size_t>(hexMatchIndex_)];
        hexScrollRow_ = hexCursor_ / 16;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next") && !hexMatches_.empty()) {
        hexMatchIndex_ = (hexMatchIndex_ + 1) % static_cast<int>(hexMatches_.size());
        hexCursor_ = hexAnchor_ = hexMatches_[static_cast<std::size_t>(hexMatchIndex_)];
        hexScrollRow_ = hexCursor_ / 16;
    }
    ImGui::EndDisabled();
    if (!hexMessage_.empty()) {
        ImGui::SameLine();
        mutedText("%s", hexMessage_.c_str());
    }

    ImGui::Checkbox("Read-only", &hexReadOnly_);
    ImGui::SameLine();
    ImGui::Checkbox("Rebuild checksum after each edit", &hexAutoFix_);
    ImGui::SameLine();
    ImGui::Checkbox("Compare primary/backup", &hexCompare_);
    ImGui::SameLine();
    ImGui::Checkbox("Highlight changes", &hexHighlightChanges_);

    // Block bookmarks double as the colour legend.
    {
        FlowRow bookmarks;
        for (std::size_t id = 0; id < sm64::kBlockCount; ++id) {
            const sm64::BlockInfo info = sm64::blockInfo(id);
            ImGui::PushID(static_cast<int>(id));
            ImGui::PushStyleColor(ImGuiCol_Button, regionColor(id, dark));
            if (bookmarks.button(blockName(id).c_str())) {
                hexCursor_ = hexAnchor_ = static_cast<int>(info.offset);
                hexScrollRow_ = hexCursor_ / 16;
                hexPendingNibble_ = -1;
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip("0x%03zX-0x%03zX", info.offset, info.offset + info.size - 1U);
            }
            ImGui::PopID();
        }
    }
    ImGui::Spacing();

    // ---- keyboard ----
    ImGuiIO& io = ImGui::GetIO();
    const bool keysLive = hexFocus_ && !io.WantTextInput && dialog_ == Dialog::None;
    int pendingScrollRow = -1;
    if (keysLive) {
        auto move = [&](int delta) {
            hexCursor_ = std::clamp(hexCursor_ + delta, 0, static_cast<int>(sm64::Size) - 1);
            if (!io.KeyShift) hexAnchor_ = hexCursor_;
            hexPendingNibble_ = -1;
            pendingScrollRow = hexCursor_ / 16;
        };
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) move(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) move(1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) move(-16);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) move(16);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) move(-16 * 8);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) move(16 * 8);
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) move(io.KeyCtrl ? -hexCursor_ : -(hexCursor_ % 16));
        if (ImGui::IsKeyPressed(ImGuiKey_End)) move(io.KeyCtrl ? 511 - hexCursor_ : 15 - (hexCursor_ % 16));
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) hexPendingNibble_ = -1;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) hexCopySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) hexPasteClipboard();
        if (!io.KeyCtrl && !hexReadOnly_ && editsAllowed()) {
            for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
                const int digit = hexDigit(io.InputQueueCharacters[i]);
                if (digit < 0) continue;
                if (hexPendingNibble_ < 0) {
                    hexPendingNibble_ = digit;
                } else {
                    hexCommitByte(static_cast<std::size_t>(hexCursor_), static_cast<std::uint8_t>((hexPendingNibble_ << 4) | digit));
                    hexPendingNibble_ = -1;
                    hexCursor_ = std::min(hexCursor_ + 1, static_cast<int>(sm64::Size) - 1);
                    hexAnchor_ = hexCursor_;
                    pendingScrollRow = hexCursor_ / 16;
                }
            }
        }
    }

    // ---- table ----
    const float inspectorHeight = 9.0F * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0F;
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0F;
    const float tableHeight = std::clamp(ImGui::GetContentRegionAvail().y - inspectorHeight, 9.0F * rowH, 20.0F * rowH);
    const int selLo = std::min(hexAnchor_, hexCursor_);
    const int selHi = std::max(hexAnchor_, hexCursor_);
    bool hovered = false;

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders |
                                           ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##hex", 18, tableFlags, ImVec2(0.0F, tableHeight))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 4.2F * em());
        for (int c = 0; c < 16; ++c) {
            char label[8]{};
            std::snprintf(label, sizeof(label), "%X", c);
            ImGui::TableSetupColumn(label, ImGuiTableColumnFlags_WidthFixed, 2.3F * em());
        }
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, 10.5F * em());
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(kRows);
        if (pendingScrollRow >= 0) clipper.IncludeItemByIndex(pendingScrollRow);
        else if (hexScrollRow_ >= 0) clipper.IncludeItemByIndex(hexScrollRow_);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                ImGui::PushID(row);
                ImGui::TableNextColumn();
                mutedText("%03X", row * 16);
                if ((pendingScrollRow == row) || (hexScrollRow_ == row)) ImGui::SetScrollHereY(0.5F);

                char ascii[17]{};
                for (int col = 0; col < 16; ++col) {
                    const std::size_t offset = static_cast<std::size_t>(row * 16 + col);
                    const std::uint8_t value = data[offset];
                    ascii[col] = (value >= 32 && value < 127) ? static_cast<char>(value) : '.';

                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(offset));
                    const sm64::RegionInfo region = sm64::describeOffset(offset);
                    ImVec4 bg = regionColor(region.blockId, dark);
                    const bool changed = value != saved[offset];
                    bool mirrorDiff = false;
                    if (hexCompare_) {
                        const sm64::BlockInfo bi = sm64::blockInfo(region.blockId);
                        const std::size_t pair = region.blockId < 8U ? region.blockId / 2U : 4U;
                        const sm64::BlockInfo p = sm64::blockInfo(pair < sm64::kSlotCount ? pair * 2U : 8U);
                        const sm64::BlockInfo b = sm64::blockInfo(pair < sm64::kSlotCount ? pair * 2U + 1U : 9U);
                        const std::size_t other = (bi.offset == p.offset ? b.offset : p.offset) + region.relative;
                        mirrorDiff = data[offset] != data[other];
                    }
                    if (hexHighlightChanges_ && changed) bg = pal.changed;
                    if (mirrorDiff) bg = ImVec4(pal.bad.x, pal.bad.y, pal.bad.z, 0.40F);
                    for (const int m : hexMatches_) {
                        if (static_cast<int>(offset) >= m && static_cast<int>(offset) < m + hexMatchLength_) {
                            bg = ImVec4(pal.info.x, pal.info.y, pal.info.z, 0.45F);
                            break;
                        }
                    }
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));

                    char text[16]{};
                    if (hexPendingNibble_ >= 0 && static_cast<int>(offset) == hexCursor_) {
                        std::snprintf(text, sizeof(text), "%X_", hexPendingNibble_);
                    } else {
                        std::snprintf(text, sizeof(text), "%02X", value);
                    }
                    const bool selected = static_cast<int>(offset) >= selLo && static_cast<int>(offset) <= selHi;
                    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
                    if (ImGui::Selectable(text, selected, ImGuiSelectableFlags_None, ImVec2(0.0F, 0.0F))) {
                        hexCursor_ = static_cast<int>(offset);
                        if (!io.KeyShift) hexAnchor_ = hexCursor_;
                        hexPendingNibble_ = -1;
                        hexFocus_ = true;
                    }
                    ImGui::PopItemFlag();
                    if (static_cast<int>(offset) == hexCursor_) {
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                            ImGui::GetColorU32(pal.accent), 0.0F, 0, 2.0F);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                        ImGui::BeginTooltip();
                        ImGui::Text("0x%03zX  %s", offset, region.name.c_str());
                        mutedText("%s", region.field.c_str());
                        ImGui::Text("Current %02X, loaded %02X, saved %02X", value, core_.original()[offset], saved[offset]);
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(ascii);
                ImGui::PopID();
            }
        }
        hexScrollRow_ = -1;
        hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_RootAndChildWindows);
        ImGui::EndTable();
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered && !ImGui::IsAnyItemHovered()) hexFocus_ = false;

    // ---- inspector ----
    ImGui::Spacing();
    ImGui::BeginChild("##inspector", ImVec2(0.0F, 0.0F),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    const auto cursor = static_cast<std::size_t>(hexCursor_);
    const sm64::RegionInfo region = sm64::describeOffset(cursor);
    const std::uint8_t value = data[cursor];
    ImGui::Text("Offset 0x%03zX (%zu)  -  %s", cursor, cursor, region.name.c_str());
    mutedText("%s", region.field.c_str());
    char bits[9]{};
    for (int b = 0; b < 8; ++b) bits[b] = ((value >> (7 - b)) & 1U) != 0U ? '1' : '0';
    ImGui::Text("Value: 0x%02X = %u = %s = '%c'", value, value, bits, (value >= 32 && value < 127) ? static_cast<char>(value) : '.');
    ImGui::Text("Loaded: 0x%02X    Saved: 0x%02X    %s", core_.original()[cursor], saved[cursor],
                value != saved[cursor] ? "(changed since save)" : "(unchanged)");
    if (selHi > selLo) mutedText("Selection: %d bytes, 0x%03X-0x%03X", selHi - selLo + 1, selLo, selHi);
    statusLabel(report.block(region.blockId).valid() ? Status::Good : Status::Bad,
                report.block(region.blockId).valid() ? "This block is valid" : "This block is invalid (signature or checksum)");

    FlowRow row;
    ImGui::PushID("inspectorbuttons");
    if (row.button("Copy hex")) hexCopySelection();
    if (row.button("Paste hex", editsAllowed() && !hexReadOnly_)) hexPasteClipboard();
    if (row.button("Revert byte to loaded value", editsAllowed() && !hexReadOnly_ && value != core_.original()[cursor])) {
        hexCommitByte(cursor, core_.original()[cursor]);
    }
    if (row.button("Rebuild this block's checksum", editsAllowed() && !hexReadOnly_)) core_.rebuildChecksum(region.blockId);
    ImGui::PopID();
    ImGui::EndChild();
    mutedText("Click a byte, then type two hex digits to change it. Arrow keys move; Shift extends the selection; Ctrl+C / Ctrl+V copy and paste.");
}

// ---------------------------------------------------------------------------
// Export
// ---------------------------------------------------------------------------
void App::pageExport() {
    const auto profile = static_cast<sm64::Profile>(exportProfile_);
    const sm64::ProfileInfo& info = sm64::profileInfo(profile);

    ImGui::TextWrapped("Exports write a copy of the current data and never change the open file. Every EEPROM profile writes the "
                       "same canonical 512 bytes; no device-specific byte rewriting is invented.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 30.0F * em()));
    const std::string current = std::string(info.name) + (info.verified ? "" : "  [experimental]");
    if (ImGui::BeginCombo("Profile", current.c_str())) {
        for (std::size_t i = 0; i < sm64::kProfileCount; ++i) {
            const sm64::ProfileInfo& p = sm64::profileInfo(static_cast<sm64::Profile>(i));
            const std::string label = std::string(p.name) + (p.verified ? "" : "  [experimental]");
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == exportProfile_)) {
                exportProfile_ = static_cast<int>(i);
                exportPath_.fill('\0');
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();

    if (ImGui::BeginTable("##profileinfo", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 11.0F * em());
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        labelValue("Output size", std::to_string(info.outputSize) + " bytes");
        labelValue("Byte order", info.byteOrder);
        labelValue("Padding", info.padding);
        labelValue("Extension", info.extension);
        labelValue("Intended for", info.device);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Status");
        ImGui::TableNextColumn();
        statusLabel(info.verified ? Status::Good : Status::Warning,
                    info.verified ? "Verified: the output is the documented canonical form" : "Experimental: not verified for the named device");
        labelValue("Notes", info.note);
        ImGui::EndTable();
    }
    ImGui::Spacing();

    if (exportPath_[0] == '\0') {
        std::filesystem::path suggested = path_.empty() ? std::filesystem::path("sm64-export") : path_;
        suggested.replace_extension("");
        suggested += "-export";
        suggested += info.extension;
        std::snprintf(exportPath_.data(), exportPath_.size(), "%s", suggested.string().c_str());
    }
    ImGui::SetNextItemWidth(std::max(10.0F * em(), ImGui::GetContentRegionAvail().x - 9.0F * em()));
    ImGui::InputText("##exportpath", exportPath_.data(), exportPath_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        const auto chosen = platform_.saveFileDialog("Export SM64 save", exportPath_.data(), {info.extension});
        if (chosen) std::snprintf(exportPath_.data(), exportPath_.size(), "%s", chosen->string().c_str());
    }

    const sm64::Report report = core_.validate();
    ImGui::SeparatorText("Export preview");
    if (ImGui::BeginTable("##exportpreview", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 11.0F * em());
        ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
        labelValue("Destination", exportPath_.data());
        labelValue("Profile", info.name);
        labelValue("Size", std::to_string(info.outputSize) + " bytes");
        labelValue("Transform", profile == sm64::Profile::SRA ? "16-bit swap of the 512 EEPROM bytes, then 0xFF padding"
                                                              : "None (bytes written exactly as they are)");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Source validation");
        ImGui::TableNextColumn();
        statusLabel(report.valid ? Status::Good : Status::Warning,
                    report.valid ? "All blocks valid" : "Some blocks are invalid; they will be exported as they are");
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        mutedText("Final checksums");
        ImGui::TableNextColumn();
        statusLabel(report.valid ? Status::Good : Status::Warning,
                    report.valid ? "Correct in the exported file" : "Not corrected by the export");
        ImGui::EndTable();
    }
    if (profile == sm64::Profile::SRA) {
        ImGui::Spacing();
        statusLabel(Status::Warning, "Retail SM64 uses EEPROM, not SRAM. This wrapper is a compatibility option only.");
    }
    ImGui::Spacing();
    if (ImGui::Button("Export now")) {
        const std::filesystem::path target(exportPath_.data());
        std::error_code ec;
        if (std::filesystem::exists(target, ec)) {
            overwritePath_ = target;
            overwriteAction_ = [this](const std::filesystem::path& p) { exportTo(p); };
            openDialog(Dialog::Overwrite);
        } else {
            exportTo(target);
        }
    }
}

// ---------------------------------------------------------------------------
// GameShark
// ---------------------------------------------------------------------------
void App::pageGameShark() {
    if (!gsBaseInit_) {
        std::snprintf(gsBaseText_.data(), gsBaseText_.size(), "%08X", static_cast<unsigned>(settings_.gsBase));
        gsBaseInit_ = true;
    }
    statusLabel(Status::Warning, "The RDRAM address of the save buffer differs between ROM regions, revisions, hacks and ports.");
    ImGui::TextWrapped("No region presets are offered because no address has been verified for every version. Enter the address that "
                       "matches your ROM. The default below is only a placeholder from an earlier version of this tool.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(9.0F * em());
    if (ImGui::InputText("RDRAM address (hex)", gsBaseText_.data(), gsBaseText_.size(),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase)) {
        settings_.gsBase = static_cast<std::uint32_t>(std::strtoul(gsBaseText_.data(), nullptr, 16));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) persistSettings();
    ImGui::Checkbox("16-bit writes (type 81)", &settings_.gsUse16);
    ImGui::SameLine();
    ImGui::Checkbox("Include every byte (not only changes)", &settings_.gsIncludeAll);
    ImGui::SameLine();
    ImGui::Checkbox("Header comments", &settings_.gsComments);

    sm64::GameSharkOptions options;
    options.base = settings_.gsBase;
    options.use16Bit = settings_.gsUse16;
    options.includeAll = settings_.gsIncludeAll;
    options.comments = settings_.gsComments;
    const sm64::GameSharkResult result = core_.gameSharkCodes(options);

    ImGui::Spacing();
    if (!result.ok) {
        statusLabel(Status::Bad, result.error.c_str());
        return;
    }
    statusLabel(Status::Info, (std::to_string(result.codes.size()) + " code(s): " + std::to_string(result.byte8Count) +
                               " 8-bit, " + std::to_string(result.byte16Count) + " 16-bit").c_str());
    if (!result.codes.empty()) {
        mutedText("Affects EEPROM offsets 0x%03zX - 0x%03zX, RDRAM 0x%08X - 0x%08X", result.firstOffset, result.lastOffset,
                            static_cast<unsigned>(settings_.gsBase + result.firstOffset),
                            static_cast<unsigned>(settings_.gsBase + result.lastOffset));
    }
    FlowRow row;
    if (row.button("Copy to clipboard", !result.codes.empty())) {
        ImGui::SetClipboardText(result.text.c_str());
        toast("GameShark codes copied.", Status::Good);
    }
    if (row.button("Export to text file", !result.codes.empty())) writeReport("sm64-gameshark.txt", ".txt", result.text);

    const float height = std::clamp(ImGui::GetContentRegionAvail().y * 0.4F, 6.0F * ImGui::GetTextLineHeightWithSpacing(),
                                    14.0F * ImGui::GetTextLineHeightWithSpacing());
    ImGui::InputTextMultiline("##gscodes", const_cast<char*>(result.text.c_str()), result.text.size() + 1U,
                              ImVec2(-FLT_MIN, height), ImGuiInputTextFlags_ReadOnly);

    ImGui::SeparatorText("Before / after");
    const sm64::EepromBytes& now = core_.view();
    const sm64::EepromBytes& orig = core_.original();
    int shown = 0;
    int differing = 0;
    for (std::size_t i = 0; i < sm64::Size; ++i) differing += now[i] != orig[i] ? 1 : 0;
    if (differing > 0 &&
        ImGui::BeginTable("##gsdiff", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0F, textTableHeight(std::min(differing, 12))))) {
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 5.0F * em());
        ImGui::TableSetupColumn("Before", ImGuiTableColumnFlags_WidthFixed, 5.0F * em());
        ImGui::TableSetupColumn("After", ImGuiTableColumnFlags_WidthFixed, 5.0F * em());
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < sm64::Size && shown < 96; ++i) {
            if (now[i] == orig[i]) continue;
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("0x%03zX", i);
            ImGui::TableNextColumn();
            ImGui::Text("%02X", orig[i]);
            ImGui::TableNextColumn();
            ImGui::Text("%02X", now[i]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted((sm64::describeOffset(i).name + ": " + sm64::describeOffset(i).field).c_str());
            ImGui::PopID();
            ++shown;
        }
        ImGui::EndTable();
    }
    if (shown == 0) mutedText("No bytes differ from the image as it was loaded.");
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void App::pageSettings() {
    bool changed = false;
    ImGui::SeparatorText("Appearance");
    int theme = settings_.darkTheme ? 0 : 1;
    if (ImGui::RadioButton("Dark", &theme, 0)) { settings_.darkTheme = true; markStyleDirty(); changed = true; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Light", &theme, 1)) { settings_.darkTheme = false; markStyleDirty(); changed = true; }

    static const char* accents[] = {"Star yellow", "Wing Cap red", "Metal Cap green", "Vanish Cap blue"};
    ImGui::SetNextItemWidth(14.0F * em());
    if (ImGui::Combo("Accent colour", &settings_.accent, accents, 4)) { markStyleDirty(); changed = true; }

    ImGui::SetNextItemWidth(14.0F * em());
    float percent = settings_.userScale * 100.0F;
    if (ImGui::SliderFloat("UI size", &percent, sm64::kMinUserScale * 100.0F, sm64::kMaxUserScale * 100.0F, "%.0f%%")) {
        settings_.userScale = std::clamp(percent / 100.0F, sm64::kMinUserScale, sm64::kMaxUserScale);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { markStyleDirty(); changed = true; }
    mutedText("This multiplies the operating-system display scaling; it does not replace it.");
    FlowRow scales;
    for (const float s : {1.0F, 1.25F, 1.5F, 1.75F, 2.0F}) {
        char label[16]{};
        std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(s * 100.0F + 0.5F));
        ImGui::PushID(static_cast<int>(s * 100.0F));
        if (scales.button(label)) { settings_.userScale = s; markStyleDirty(); changed = true; }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Editing mode");
    int mode = settings_.expert ? 1 : 0;
    if (ImGui::RadioButton("Beginner (friendly names, no hex)", &mode, 0)) { setExpert(false); changed = true; }
    if (ImGui::RadioButton("Expert (raw data, GameShark, validation details)", &mode, 1)) { setExpert(true); changed = true; }

    ImGui::SeparatorText("File safety");
    changed |= ImGui::Checkbox("Create a .bak copy before overwriting a file", &settings_.backupOnSave);
    changed |= ImGui::Checkbox("Auto-repair blocks that have one valid copy when opening a file", &settings_.autoRepairOnLoad);
    mutedText("Auto-repair shows up as an unsaved, undoable change; nothing is written until you save.");
    changed |= ImGui::Checkbox("Read-only forensic mode (no edits, no saving over files)", &settings_.forensic);

    ImGui::SeparatorText("Other");
    if (ImGui::Button("Show the welcome tour again")) {
        tutorialStep_ = 0;
        openDialog(Dialog::Welcome);
    }
    ImGui::BeginDisabled(settings_.recent.empty());
    if (ImGui::Button("Clear recent files")) {
        settings_.recent.clear();
        changed = true;
    }
    ImGui::EndDisabled();
    if (!settingsPath_.empty()) mutedText("Settings file: %s", settingsPath_.string().c_str());

    if (changed) persistSettings();
}

// ---------------------------------------------------------------------------
// About
// ---------------------------------------------------------------------------
void App::pageAbout() {
    ImGui::TextUnformatted("SM64 Ultimate Save Editor");
    mutedText("Version 2.0.0");
    ImGui::Spacing();
    ImGui::TextWrapped("A save editor for Super Mario 64 EEPROM files (512 bytes). It is an independent fan tool and is not "
                       "affiliated with or endorsed by Nintendo. It ships no ROMs, game assets or firmware.");
    ImGui::SeparatorText("What the save format looks like");
    ImGui::TextWrapped("Four save slots (A-D) and one menu block, each stored twice (primary + backup). A slot is 56 bytes: lost-cap "
                       "data, a 32-bit flags word, 25 course-star bytes, 15 coin high scores, then a 'DA' signature and a "
                       "big-endian checksum. The menu block is 32 bytes with an 'HI' signature. The checksum is the sum of every "
                       "byte except the last two.");
    ImGui::SeparatorText("Things you may wonder");
    ImGui::TextWrapped("Lives are not editable because the retail game does not store a lives counter in the save file. The total star "
                       "count is not stored either; it is calculated from the star flags.");
    ImGui::TextWrapped("Validation can tell you whether a block is internally consistent and whether the two copies agree. It cannot "
                       "know whether the data inside a consistent block is what you intended.");
    ImGui::TextWrapped("GameShark addresses vary between ROM regions and revisions, so this tool asks you for the address instead of "
                       "assuming one.");
    ImGui::SeparatorText("Credits");
    ImGui::TextWrapped("Built with Dear ImGui, SDL2 and portable-file-dialogs. The save layout was cross-checked against the "
                       "n64decomp/sm64 decompilation.");
}

}  // namespace sm64ui
