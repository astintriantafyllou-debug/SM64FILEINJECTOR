#include "SM64HexEditorUI.hpp"

#include <imgui.h>
#include <portable-file-dialogs.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr ImVec4 kGood{0.20F, 1.00F, 0.30F, 1.00F};
constexpr ImVec4 kBad{1.00F, 0.30F, 0.20F, 1.00F};

std::uint32_t readBE32(const sm64::Core::Bytes& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes.at(offset)) << 24U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 16U) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 8U) |
           static_cast<std::uint32_t>(bytes.at(offset + 3U));
}
}

UI::UI(sm64::Core& core) : c(core) {
    codes = c.gameShark(addr, word, all);
    std::snprintf(edit.data(), edit.size(), "%02X", 0U);
}

void UI::open() {
    const auto files = pfd::open_file(
        "Open SM64 EEPROM",
        "",
        {"SM64 EEPROM", "*.eep *.EEP *.sav *.bin", "All files", "*"},
        pfd::opt::none
    ).result();

    if (files.empty()) return;

    std::string error;
    if (!c.load(files.front(), autoRepair, error)) {
        note = error;
        return;
    }

    codes = c.gameShark(addr, word, all);
    note = "Save loaded successfully.";
}

void UI::save() {
    const std::string path = pfd::save_file(
        "Save SM64 EEPROM",
        "sm64-edited.eep",
        {"SM64 EEPROM", "*.eep", "All files", "*"},
        pfd::opt::none
    ).result();

    if (path.empty()) return;

    std::string error;
    if (!c.save(path, error)) {
        note = error;
        return;
    }

    codes = c.gameShark(addr, word, all);
    note = "Save written with updated signatures and checksums.";
}

void UI::dashboard() {
    const auto bytes = c.bytes();
    const auto report = c.validate();

    ImGui::TextWrapped(
        "Use Beginner Save Editor for normal editing. Enable Expert mode for raw hex, validation, exports, and GameShark codes."
    );
    ImGui::Separator();

    const float width = std::max(260.0F, (ImGui::GetContentRegionAvail().x - 16.0F) * 0.5F);

    for (int slotIndex = 0; slotIndex < 4; ++slotIndex) {
        const std::size_t base = sm64::P.at(static_cast<std::size_t>(slotIndex));
        int stars = 0;

        for (int i = 0; i < 25; ++i) {
            const auto value = bytes.at(base + 12U + static_cast<std::size_t>(i));
            stars += std::popcount(static_cast<unsigned int>(value & 0x7FU));
        }

        const auto& primary = report.slots.at(static_cast<std::size_t>(slotIndex * 2));
        const auto& backup = report.slots.at(static_cast<std::size_t>(slotIndex * 2 + 1));
        const bool healthy = primary.magic && primary.checksum && backup.magic && backup.checksum;

        ImGui::PushID(slotIndex);
        ImGui::BeginChild("SlotCard", ImVec2(width, 105.0F), true);
        ImGui::Text("SAVE SLOT %c", static_cast<char>('A' + slotIndex));
        ImGui::Text("%d collected star flags", stars);
        ImGui::TextColored(
            healthy ? kGood : kBad,
            "Primary: %s   Backup: %s",
            primary.checksum ? "OK" : "BAD",
            backup.checksum ? "OK" : "BAD"
        );
        ImGui::EndChild();
        ImGui::PopID();

        if ((slotIndex % 2) == 0) ImGui::SameLine();
    }
}

void UI::beginner() {
    if (!ImGui::BeginTabBar("SaveSlots")) return;

    for (int slotIndex = 0; slotIndex < 4; ++slotIndex) {
        char tabName[16]{};
        std::snprintf(tabName, sizeof(tabName), "Slot %c", static_cast<char>('A' + slotIndex));

        if (!ImGui::BeginTabItem(tabName)) continue;

        const auto bytes = c.bytes();
        const std::size_t base = sm64::P.at(static_cast<std::size_t>(slotIndex));
        std::uint32_t flags = readBE32(bytes, base + 8U);

        const auto writeFlags = [&](std::uint32_t value) {
            for (std::size_t i = 0; i < 4U; ++i) {
                const unsigned int shift = static_cast<unsigned int>(24U - i * 8U);
                c.setMirrored(
                    slotIndex,
                    8U + i,
                    static_cast<std::uint8_t>((value >> shift) & 0xFFU)
                );
            }
        };

        const auto drawFlag = [&](const char* label, std::uint32_t mask) {
            bool enabled = (flags & mask) != 0U;
            if (ImGui::Checkbox(label, &enabled)) {
                flags = enabled ? (flags | mask) : (flags & ~mask);
                writeFlags(flags);
            }
        };

        int totalStars = 0;
        for (int i = 0; i < 25; ++i) {
            const auto value = bytes.at(base + 12U + static_cast<std::size_t>(i));
            totalStars += std::popcount(static_cast<unsigned int>(value & 0x7FU));
        }

        ImGui::Text("Total saved star flags: %d", totalStars);
        ImGui::TextDisabled("Lives are runtime state and are not stored in retail SM64 EEPROM.");

        if (ImGui::CollapsingHeader("Caps, keys and castle progress", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawFlag("File occupied", 1U << 0U);
            drawFlag("Wing Cap switch", 1U << 1U);
            drawFlag("Metal Cap switch", 1U << 2U);
            drawFlag("Vanish Cap switch", 1U << 3U);
            drawFlag("Basement key", 1U << 4U);
            drawFlag("Upstairs key", 1U << 5U);
            drawFlag("Basement door unlocked", 1U << 6U);
            drawFlag("Upstairs door unlocked", 1U << 7U);
            drawFlag("Dire, Dire Docks moved", 1U << 8U);
            drawFlag("Castle moat drained", 1U << 9U);
            drawFlag("50-star door unlocked", 1U << 20U);
        }

        if (ImGui::BeginTable(
                "CourseTable",
                9,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0.0F, 430.0F))) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Course", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Coins", ImGuiTableColumnFlags_WidthFixed, 70.0F);

            for (int star = 1; star <= 7; ++star) {
                char heading[4]{};
                std::snprintf(heading, sizeof(heading), "S%d", star);
                ImGui::TableSetupColumn(heading, ImGuiTableColumnFlags_WidthFixed, 36.0F);
            }

            ImGui::TableHeadersRow();

            for (int course = 0; course < 15; ++course) {
                ImGui::PushID(course);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(sm64::Courses.at(static_cast<std::size_t>(course)));

                ImGui::TableSetColumnIndex(1);
                int coins = static_cast<int>(bytes.at(base + 37U + static_cast<std::size_t>(course)));
                ImGui::SetNextItemWidth(-1.0F);
                if (ImGui::InputInt("##Coins", &coins, 0, 0)) {
                    coins = std::clamp(coins, 0, 255);
                    c.setMirrored(
                        slotIndex,
                        37U + static_cast<std::size_t>(course),
                        static_cast<std::uint8_t>(coins)
                    );
                }

                std::uint8_t mask = bytes.at(base + 12U + static_cast<std::size_t>(course));
                for (int star = 0; star < 7; ++star) {
                    ImGui::TableSetColumnIndex(star + 2);
                    const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(star));
                    bool collected = (mask & bit) != 0U;

                    if (ImGui::Checkbox("##Star", &collected)) {
                        if (collected) {
                            mask = static_cast<std::uint8_t>(mask | bit);
                        } else {
                            mask = static_cast<std::uint8_t>(mask & static_cast<std::uint8_t>(~bit));
                        }
                        c.setMirrored(slotIndex, 12U + static_cast<std::size_t>(course), mask);
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}

void UI::hex() {
    const auto bytes = c.bytes();
    ImGuiListClipper clipper;
    clipper.Begin(32);

    if (ImGui::BeginTable(
            "HexTable",
            18,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
            ImVec2(0.0F, 500.0F))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Offset");

        for (int column = 0; column < 16; ++column) {
            char heading[4]{};
            std::snprintf(heading, sizeof(heading), "%X", column);
            ImGui::TableSetupColumn(heading);
        }

        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%03X", row * 16);
                std::string ascii;

                for (int column = 0; column < 16; ++column) {
                    const int offset = row * 16 + column;
                    const auto value = bytes.at(static_cast<std::size_t>(offset));
                    ImGui::TableSetColumnIndex(column + 1);
                    ImGui::PushID(offset);

                    char text[3]{};
                    std::snprintf(text, sizeof(text), "%02X", value);
                    if (ImGui::Selectable(text, offset == selected, ImGuiSelectableFlags_None, ImVec2(25.0F, 0.0F))) {
                        selected = offset;
                        std::snprintf(edit.data(), edit.size(), "%02X", value);
                    }

                    ImGui::PopID();
                    ascii.push_back(std::isprint(static_cast<unsigned char>(value)) != 0 ? static_cast<char>(value) : '.');
                }

                ImGui::TableSetColumnIndex(17);
                ImGui::TextUnformatted(ascii.c_str());
            }
        }

        ImGui::EndTable();
    }

    ImGui::Text("Selected offset: 0x%03X", selected);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64.0F);

    if (ImGui::InputText(
            "##ByteValue",
            edit.data(),
            edit.size(),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        unsigned int value = 0U;
        if (std::sscanf(edit.data(), "%x", &value) == 1 && value <= 0xFFU) {
            c.set(static_cast<std::size_t>(selected), static_cast<std::uint8_t>(value));
        } else {
            note = "Enter a hexadecimal byte from 00 to FF.";
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Recalculate checksums")) {
        c.normalize();
        note = "All signatures and checksums were recalculated.";
    }
}

void UI::validation() {
    const auto report = c.validate();
    ImGui::TextColored(report.valid ? kGood : kBad, "%s", report.valid ? "All EEPROM blocks are valid." : "Validation problems found.");

    if (ImGui::Button("Repair from valid mirrors")) {
        const auto actions = c.repair();
        if (actions.empty()) {
            note = "No recoverable mirrored corruption was found.";
        } else {
            note.clear();
            for (const auto& action : actions) {
                if (!note.empty()) note += "\n";
                note += action;
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Rebuild checksums")) {
        c.normalize();
        note = "All signatures and checksums were rebuilt.";
    }

    if (ImGui::BeginTable("ValidationTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Copy");
        ImGui::TableSetupColumn("Magic");
        ImGui::TableSetupColumn("Checksum");
        ImGui::TableSetupColumn("Stored");
        ImGui::TableSetupColumn("Computed");
        ImGui::TableHeadersRow();

        for (int i = 0; i < 8; ++i) {
            const auto& state = report.slots.at(static_cast<std::size_t>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Slot %c", static_cast<char>('A' + i / 2));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted((i % 2) == 0 ? "Primary" : "Backup");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(state.magic ? kGood : kBad, "%s", state.magic ? "OK" : "BAD");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextColored(state.checksum ? kGood : kBad, "%s", state.checksum ? "OK" : "BAD");
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%04X", state.stored);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%04X", state.computed);
        }

        ImGui::EndTable();
    }
}

void UI::exports() {
    static const char* names[] = {
        "Raw EEPROM",
        "EverDrive V1",
        "EverDrive V2",
        "EverDrive X/Pro",
        "ED64Plus",
        "Retrode 2",
        "Emulator .eep",
        "Emulator .sav",
        "32 KiB .sra wrapper"
    };

    ImGui::Combo("Profile", &profile, names, IM_ARRAYSIZE(names));
    ImGui::TextWrapped("SM64 natively uses a 512-byte EEPROM. The SRA option is only a compatibility wrapper.");

    if (ImGui::Button("Export...")) {
        const std::string path = pfd::save_file(
            "Export SM64 save",
            profile == 8 ? "sm64-export.sra" : "sm64-export.eep",
            {"Save image", "*", "All files", "*"},
            pfd::opt::none
        ).result();

        if (!path.empty()) {
            std::string error;
            if (!c.exportFile(path, static_cast<sm64::Profile>(profile), error)) {
                note = error;
            } else {
                note = "Export completed successfully.";
            }
        }
    }
}

void UI::cheats() {
    ImGui::TextWrapped("Verify the RDRAM save-buffer address for the exact ROM region and revision.");
    ImGui::InputScalar("RDRAM base", ImGuiDataType_U32, &addr, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::Checkbox("Use aligned 16-bit codes", &word);
    ImGui::Checkbox("Include unchanged bytes", &all);

    if (ImGui::Button("Generate")) codes = c.gameShark(addr, word, all);
    ImGui::SameLine();
    if (ImGui::Button("Copy")) ImGui::SetClipboardText(codes.c_str());

    ImGui::InputTextMultiline(
        "##Codes",
        codes.data(),
        codes.size() + 1U,
        ImVec2(-1.0F, -1.0F),
        ImGuiInputTextFlags_ReadOnly
    );
}

void UI::render() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) open();
            if (ImGui::MenuItem("Save As...", "Ctrl+S")) save();
            ImGui::EndMenu();
        }

        ImGui::Checkbox("Expert mode", &expert);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-repair on load", &autoRepair);
        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(0.0F, ImGui::GetFrameHeight()), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y - ImGui::GetFrameHeight()),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "SM64 Ultimate Save Editor",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
    );

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Dashboard")) {
            dashboard();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Beginner Save Editor")) {
            beginner();
            ImGui::EndTabItem();
        }

        if (expert) {
            if (ImGui::BeginTabItem("Raw Hex")) {
                hex();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Validation & Repair")) {
                validation();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Hardware Export")) {
                exports();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("GameShark")) {
                cheats();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    if (!note.empty()) ImGui::OpenPopup("Notice");
    if (ImGui::BeginPopupModal("Notice", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", note.c_str());
        if (ImGui::Button("OK", ImVec2(100.0F, 0.0F))) {
            note.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
