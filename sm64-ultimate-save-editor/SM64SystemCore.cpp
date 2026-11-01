#include "SM64HexEditorUI.hpp"

#include <imgui.h>
#include <portable-file-dialogs.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
constexpr ImVec4 GoodColor{
    0.20F,
    1.00F,
    0.30F,
    1.00F
};

constexpr ImVec4 BadColor{
    1.00F,
    0.30F,
    0.20F,
    1.00F
};

std::uint32_t readBigEndian32(
    const sm64::Core::Bytes& bytes,
    std::size_t offset
)
{
    return
        (static_cast<std::uint32_t>(bytes.at(offset)) << 24U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 1U)) << 16U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 2U)) << 8U) |
        static_cast<std::uint32_t>(bytes.at(offset + 3U));
}
}

UI::UI(sm64::Core& core)
    : c(core)
{
    codes = c.gameShark(
        addr,
        word,
        all
    );

    std::snprintf(
        edit.data(),
        edit.size(),
        "%02X",
        0
    );
}

void UI::open()
{
    const auto selectedFiles = pfd::open_file(
        "Open SM64 EEPROM",
        "",
        {
            "SM64 EEPROM files",
            "*.eep *.EEP *.sav *.bin",
            "All files",
            "*"
        },
        pfd::opt::none
    ).result();

    if (selectedFiles.empty())
    {
        return;
    }

    std::string error;

    if (!c.load(
            selectedFiles.front(),
            autoRepair,
            error
        ))
    {
        note = error;
        return;
    }

    codes = c.gameShark(
        addr,
        word,
        all
    );

    note =
        "The save file was loaded successfully. "
        "Open Validation & Repair in Expert mode to inspect its blocks.";
}

void UI::save()
{
    const std::string selectedPath = pfd::save_file(
        "Save SM64 EEPROM",
        "sm64-edited.eep",
        {
            "SM64 EEPROM",
            "*.eep",
            "Binary save",
            "*.bin",
            "All files",
            "*"
        },
        pfd::opt::none
    ).result();

    if (selectedPath.empty())
    {
        return;
    }

    std::string error;

    if (!c.save(
            selectedPath,
            error
        ))
    {
        note = error;
        return;
    }

    codes = c.gameShark(
        addr,
        word,
        all
    );

    note =
        "The 512-byte EEPROM was saved with regenerated "
        "signatures and checksums.";
}

void UI::dashboard()
{
    const sm64::Core::Bytes bytes = c.bytes();
    const sm64::Report report = c.validate();

    ImGui::TextWrapped(
        "Use Beginner Save Editor for normal star, coin, key and "
        "cap changes. Enable Expert mode for raw hexadecimal editing, "
        "repair, hardware export and GameShark generation."
    );

    ImGui::Separator();

    const float availableWidth =
        ImGui::GetContentRegionAvail().x;

    const float cardWidth =
        std::max(
            260.0F,
            (availableWidth - 16.0F) * 0.5F
        );

    for (int slotIndex = 0; slotIndex < 4; ++slotIndex)
    {
        int starCount = 0;

        const std::size_t slotOffset =
            sm64::P.at(
                static_cast<std::size_t>(slotIndex)
            );

        for (int courseIndex = 0; courseIndex < 25; ++courseIndex)
        {
            const std::uint8_t flags =
                bytes.at(
                    slotOffset +
                    12U +
                    static_cast<std::size_t>(courseIndex)
                );

            starCount += std::popcount(
                static_cast<unsigned int>(flags & 0x7FU)
            );
        }

        const sm64::BlockState& primary =
            report.slots.at(
                static_cast<std::size_t>(slotIndex * 2)
            );

        const sm64::BlockState& backup =
            report.slots.at(
                static_cast<std::size_t>(slotIndex * 2 + 1)
            );

        const bool healthy =
            primary.magic &&
            primary.checksum &&
            backup.magic &&
            backup.checksum;

        ImGui::PushID(slotIndex);

        ImGui::BeginChild(
            "SlotSummary",
            ImVec2(
                cardWidth,
                110.0F
            ),
            true
        );

        ImGui::Text(
            "SAVE SLOT %c",
            static_cast<char>('A' + slotIndex)
        );

        ImGui::Text(
            "%d collected star flags",
            starCount
        );

        ImGui::TextColored(
            healthy ? GoodColor : BadColor,
            "Primary: %s   Backup: %s",
            primary.checksum ? "OK" : "CHECK",
            backup.checksum ? "OK" : "CHECK"
        );

        ImGui::TextDisabled(
            healthy
                ? "Both mirrored blocks are valid."
                : "Open Validation & Repair for details."
        );

        ImGui::EndChild();
        ImGui::PopID();

        if ((slotIndex % 2) == 0)
        {
            ImGui::SameLine();
        }
    }
}

void UI::beginner()
{
    if (!ImGui::BeginTabBar("SaveSlotTabs"))
    {
        return;
    }

    for (int slotIndex = 0; slotIndex < 4; ++slotIndex)
    {
        char tabName[16]{};

        std::snprintf(
            tabName,
            sizeof(tabName),
            "Slot %c",
            static_cast<char>('A' + slotIndex)
        );

        if (!ImGui::BeginTabItem(tabName))
        {
            continue;
        }

        const sm64::Core::Bytes bytes = c.bytes();

        const std::size_t slotOffset =
            sm64::P.at(
                static_cast<std::size_t>(slotIndex)
            );

        std::uint32_t progressFlags =
            readBigEndian32(
                bytes,
                slotOffset + 8U
            );

        const auto writeProgressFlags =
            [this, slotIndex](std::uint32_t value)
            {
                for (std::size_t byteIndex = 0;
                     byteIndex < 4U;
                     ++byteIndex)
                {
                    const unsigned int shift =
                        static_cast<unsigned int>(
                            24U - byteIndex * 8U
                        );

                    const std::uint8_t outputByte =
                        static_cast<std::uint8_t>(
                            (value >> shift) & 0xFFU
                        );

                    c.setMirrored(
                        slotIndex,
                        8U + byteIndex,
                        outputByte
                    );
                }
            };

        const auto renderFlag =
            [&progressFlags, &writeProgressFlags](
                const char* label,
                std::uint32_t mask
            )
            {
                bool enabled =
                    (progressFlags & mask) != 0U;

                if (ImGui::Checkbox(
                        label,
                        &enabled
                    ))
                {
                    if (enabled)
                    {
                        progressFlags |= mask;
                    }
                    else
                    {
                        progressFlags &= ~mask;
                    }

                    writeProgressFlags(progressFlags);
                }
            };

        int totalStars = 0;

        for (int index = 0; index < 25; ++index)
        {
            const std::uint8_t courseFlags =
                bytes.at(
                    slotOffset +
                    12U +
                    static_cast<std::size_t>(index)
                );

            totalStars += std::popcount(
                static_cast<unsigned int>(
                    courseFlags & 0x7FU
                )
            );
        }

        ImGui::Text(
            "Total saved star flags: %d",
            totalStars
        );

        ImGui::SameLine();

        ImGui::TextDisabled(
            "Lives are runtime state and are not stored "
            "in the retail SM64 EEPROM."
        );

        if (ImGui::CollapsingHeader(
                "Caps, keys and castle progress",
                ImGuiTreeNodeFlags_DefaultOpen
            ))
        {
            renderFlag(
                "File occupied",
                1U << 0U
            );

            renderFlag(
                "Wing Cap switch",
                1U << 1U
            );

            renderFlag(
                "Metal Cap switch",
                1U << 2U
            );

            renderFlag(
                "Vanish Cap switch",
                1U << 3U
            );

            renderFlag(
                "Basement key",
                1U << 4U
            );

            renderFlag(
                "Upstairs key",
                1U << 5U
            );

            renderFlag(
                "Basement door unlocked",
                1U << 6U
            );

            renderFlag(
                "Upstairs door unlocked",
                1U << 7U
            );

            renderFlag(
                "Dire, Dire Docks moved",
                1U << 8U
            );

            renderFlag(
                "Castle moat drained",
                1U << 9U
            );

            renderFlag(
                "50-star door unlocked",
                1U << 20U
            );
        }

        if (ImGui::BeginTable(
                "CourseProgressTable",
                9,
                ImGuiTableFlags_Borders |
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingStretchProp,
                ImVec2(
                    0.0F,
                    445.0F
                )
            ))
        {
            ImGui::TableSetupScrollFreeze(
                1,
                1
            );

            ImGui::TableSetupColumn(
                "Course",
                ImGuiTableColumnFlags_WidthStretch,
                3.0F
            );

            ImGui::TableSetupColumn(
                "Coins",
                ImGuiTableColumnFlags_WidthFixed,
                70.0F
            );

            for (int starIndex = 1;
                 starIndex <= 7;
                 ++starIndex)
            {
                char starHeading[8]{};

                std::snprintf(
                    starHeading,
                    sizeof(starHeading),
                    "S%d",
                    starIndex
                );

                ImGui::TableSetupColumn(
                    starHeading,
                    ImGuiTableColumnFlags_WidthFixed,
                    36.0F
                );
            }

            ImGui::TableHeadersRow();

            for (int courseIndex = 0;
                 courseIndex < 15;
                 ++courseIndex)
            {
                ImGui::PushID(courseIndex);

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);

                ImGui::TextUnformatted(
                    sm64::Courses.at(
                                       )
                    )
                );

                ImGui::TableSetColumnIndex(1);

                int coinScore =
                    static_cast<int>(
                        bytes.at(
                            slotOffset +
                            37U +
                            static_cast<std::size_t>(
                                courseIndex
                            )
                        )
                    );

                ImGui::SetNextItemWidth(-1.0F);

                if (ImGui::InputInt(
                        "##CoinScore",
                        &coinScore,
                        0,
                        0
                    ))
                {
                    coinScore = std::clamp(
                        coinScore,
                        0,
                        255
                    );

                    c.setMirrored(
                        slotIndex,
                        37U +
                            static_cast<std::size_t>(
                                courseIndex
                            ),
                        static_cast<std::uint8_t>(
                            coinScore
                        )
                    );
                }

                std::uint8_t starMask =
                    bytes.at(
                        slotOffset +
                        12U +
                        static_cast<std::size_t>(
                            courseIndex
                        )
                    );

                for (int starIndex = 0;
                     starIndex < 7;
                     ++starIndex)
                {
                    ImGui::TableSetColumnIndex(
                        starIndex + 2
                    );

                    const std::uint8_t bit =
                        static_cast<std::uint8_t>(
                            1U << static_cast<unsigned int>(
                                starIndex
                            )
                        );

                    bool collected =
                        (starMask & bit) != 0U;

                    if (ImGui::Checkbox(
                            "##Collected",
                            &collected
                        ))
                    {
                        if (collected)
                        {
                            starMask =
                                static_cast<std::uint8_t>(
                                    starMask | bit
                                );
                        }
                        else
                        {
                            starMask =
                                static_cast<std::uint8_t>(
                                    starMask &
                                    static_cast<std::uint8_t>(
                                        ~bit
                                    )
                                );
                        }

                        c.setMirrored(
                            slotIndex,
                            12U +
                                static_cast<std::size_t>(
                                    courseIndex
                                ),
                            starMask
                        );
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

void UI::hex()
{
    const sm64::Core::Bytes bytes = c.bytes();

    ImGui::TextWrapped(
        "Select a byte, enter a two-digit hexadecimal value, "
        "then press Enter. Raw changes can invalidate checksums."
    );

    ImGuiListClipper clipper;
    clipper.Begin(32);

    if (ImGui::BeginTable(
            "HexEditorTable",
            18,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingFixedFit,
            ImVec2(
                0.0F,
                500.0F
            )
        ))
    {
        ImGui::TableSetupScrollFreeze(
            1,
            1
        );

        ImGui::TableSetupColumn(
            "Offset",
            ImGuiTableColumnFlags_WidthFixed,
            62.0F
        );

        for (int column = 0;
             column < 16;
             ++column)
        {
            char heading[4]{};

            std::snprintf(
                heading,
                sizeof(heading),
                "%X",
                column
            );

            ImGui::TableSetupColumn(
                heading,
                ImGuiTableColumnFlags_WidthFixed,
                30.0F
            );
        }

        ImGui::TableSetupColumn(
            "ASCII",
            ImGuiTableColumnFlags_WidthStretch
        );

        ImGui::TableHeadersRow();

        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart;
                 row < clipper.DisplayEnd;
                 ++row)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::Text(
                    "%03X",
                    row * 16
                );

                std::string ascii;
                ascii.reserve(16U);

                for (int column = 0;
                     column < 16;
                     ++column)
                {
                    const int offset =
                        row * 16 + column;

                    ImGui::TableSetColumnIndex(
                        column + 1
                    );

                    ImGui::PushID(offset);

                    char byteText[3]{};

                    std::snprintf(
                        byteText,
                        sizeof(byteText),
                        "%02X",
                        bytes.at(
                            static_cast<std::size_t>(
                                offset
                            )
                        )
                    );

                    if (ImGui::Selectable(
                            byteText,
                            offset == selected,
                            ImGuiSelectableFlags_None,
                            ImVec2(
                                25.0F,
                                0.0F
                            )
                        ))
                    {
                        selected = offset;

                        std::snprintf(
                            edit.data(),
                            edit.size(),
                            "%02X",
                            bytes.at(
                                static_cast<std::size_t>(
                                    offset
                                )
                            )
                        );
                    }

                    ImGui::PopID();

                    const unsigned char value =
                        bytes.at(
                            static_cast<std::size_t>(
                                offset
                            )
                        );

                    ascii.push_back(
                        std::isprint(value) != 0
                            ? static_cast<char>(value)
                            : '.'
                    );
                }

                ImGui::TableSetColumnIndex(17);

                ImGui::TextUnformatted(
                    ascii.c_str()
                );
            }
        }

        ImGui::EndTable();
    }

    ImGui::Text(
        "Selected offset: 0x%03X",
        selected
    );

    ImGui::SameLine();

    ImGui::SetNextItemWidth(64.0F);

    const bool valueSubmitted =
        ImGui::InputText(
            "##ByteValue",
            edit.data(),
            edit.size(),
            ImGuiInputTextFlags_CharsHexadecimal |
                ImGuiInputTextFlags_EnterReturnsTrue
        );

    if (valueSubmitted)
    {
        unsigned int value = 0U;

        if (
            std::sscanf(
                edit.data(),
                "%x",
                &value
            ) == 1 &&
            value <= 0xFFU
        )
        {
            c.set(
                static_cast<std::size_t>(
                    selected
                ),
                static_cast<std::uint8_t>(
                    value
                )
            );
        }
        else
        {
            note =
                "Enter one byte from 00 through FF.";
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(
            "Recalculate signatures and checksums"
        ))
    {
        c.normalize();

        note =
            "All EEPROM block signatures and checksums "
            "were regenerated.";
    }
}

void UI::validation()
{
    const sm64::Report report = c.validate();

    ImGui::TextColored(
        report.valid ? GoodColor : BadColor,
        "%s",
        report.valid
            ? "All EEPROM blocks are valid."
            : "One or more EEPROM blocks failed validation."
    );

    if (ImGui::Button(
            "Repair from valid mirrored blocks"
        ))
    {
        const std::vector<std::string> actions =
            c.repair();

        if (actions.empty())
        {
            note =
                "No recoverable primary/backup mismatch "
                "was found.";
        }
        else
        {
            note.clear();

            for (const std::string& action : actions)
            {
                if (!note.empty())
                {
                    note += "\n";
                }

                note += action;
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button(
            "Rebuild all checksums"
        ))
    {
        c.normalize();

        note =
            "All signatures and checksums were rebuilt.";
    }

    ImGui::Separator();

    if (ImGui::BeginTable(
            "ValidationTable",
            6,
            ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp
        ))
    {
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Copy");
        ImGui::TableSetupColumn("Magic");
        ImGui::TableSetupColumn("Checksum");
        ImGui::TableSetupColumn("Stored");
        ImGui::TableSetupColumn("Calculated");

        ImGui::TableHeadersRow();

        for (int index = 0;
             index < 8;
             ++index)
        {
            const sm64::BlockState& state =
                report.slots.at(
                    static_cast<std::size_t>(
                        index
                    )
                );

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            ImGui::Text(
                "Slot %c",
                static_cast<char>(
                    'A' + index / 2
                )
            );

            ImGui::TableSetColumnIndex(1);

            ImGui::TextUnformatted(
                (index % 2) == 0
                    ? "Primary"
                    : "Backup"
            );

            ImGui::TableSetColumnIndex(2);

            ImGui::TextColored(
                state.magic
                    ? GoodColor
                    : BadColor,
                "%s",
                state.magic
                    ? "OK"
                    : "BAD"
            );

            ImGui::TableSetColumnIndex(3);

            ImGui::TextColored(
                state.checksum
                    ? GoodColor
                    : BadColor,
                "%s",
                state.checksum
                    ? "OK"
                    : "BAD"
            );

            ImGui::TableSetColumnIndex(4);

            ImGui::Text(
                "%04X",
                state.stored
            );

            ImGui::TableSetColumnIndex(5);

            ImGui::Text(
                "%04X",
                state.computed
            );
        }

        ImGui::EndTable();
    }
}

void UI::exports()
{
    static constexpr const char* ProfileNames[] = {
        "Raw 512-byte EEPROM",
        "EverDrive-64 V1",
        "EverDrive-64 V2",
        "EverDrive-64 X/Pro",
        "ED64Plus",
        "Retrode 2",
        "Emulator .eep",
        "Emulator .sav",
        "32 KiB .sra compatibility wrapper"
    };

    ImGui::Combo(
        "Export profile",
        &profile,
        ProfileNames,
        IM_ARRAYSIZE(ProfileNames)
    );

    ImGui::TextWrapped(
        "SM64 natively uses a 512-byte EEPROM. "
        "The .sra option is an explicit padded compatibility "
        "wrapper, not SM64's native save medium."
    );

    if (ImGui::Button(
            "Export save image"
        ))
    {
        const std::string selectedPath =
            pfd::save_file(
                "Export SM64 Save",
                profile == 8
                    ? "sm64-export.sra"
                    : "sm64-export.eep",
                {
                    "Save image",
                    "*",
                    "All files",
                    "*"
                },
                pfd::opt::none
            ).result();

        if (!selectedPath.empty())
        {
            std::string error;

            if (!c.exportFile(
                    selectedPath,
                    static_cast<sm64::Profile>(
                        profile
                    ),
                    error
                ))
            {
                note = error;
            }
            else
            {
                note =
                    "The hardware/emulator save image "
                    "was exported successfully.";
            }
        }
    }
}

void UI::cheats()
{
    ImGui::TextWrapped(
        "GameShark codes write the edited EEPROM buffer into RDRAM. "
        "The buffer address must match the exact SM64 region and ROM revision."
    );

    ImGui::InputScalar(
        "RDRAM save-buffer base",
        ImGuiDataType_U32,
        &addr,
        nullptr,
        nullptr,
        "%08X",
        ImGuiInputTextFlags_CharsHexadecimal
    );

    ImGui::Checkbox(
        "Use aligned 16-bit codes where possible",
        &word
    );

    ImGui::Checkbox(
        "Include unchanged bytes",
        &all
    );

    if (ImGui::Button(
            "Generate GameShark codes"
        ))
    {
        codes = c.gameShark(
            addr,
            word,
            all
        );
    }

    ImGui::SameLine();

    if (ImGui::Button(
            "Copy codes"
        ))
    {
        ImGui::SetClipboardText(
            codes.c_str()
        );
    }

    ImGui::InputTextMultiline(
        "##GameSharkCodes",
        codes.data(),
        codes.size() + 1U,
        ImVec2(
            -1.0F,
            -1.0F
        ),
        ImGuiInputTextFlags_ReadOnly
    );
}

void UI::render()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(
                    "Open...",
                    "Ctrl+O"
                ))
            {
                open();
            }

            if (ImGui::MenuItem(
                    "Save As...",
                    "Ctrl+S"
                ))
            {
                save();
            }

            ImGui::EndMenu();
        }

        ImGui::Checkbox(
            "Expert mode",
            &expert
        );

        ImGui::SameLine();

        ImGui::Checkbox(
            "Auto-repair when loading",
            &autoRepair
        );

        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(
        ImVec2(
            0.0F,
            ImGui::GetFrameHeight()
        ),
        ImGuiCond_Always
    );

    ImGui::SetNextWindowSize(
        ImVec2(
            ImGui::GetIO().DisplaySize.x,
            ImGui::GetIO().DisplaySize.y -
                ImGui::GetFrameHeight()
        ),
        ImGuiCond_Always
    );

    ImGui::Begin(
        "SM64 Ultimate Save Editor",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings
    );

    if (ImGui::BeginTabBar("MainTabs"))
    {
        if (ImGui::BeginTabItem("Dashboard"))
        {
            dashboard();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Beginner Save Editor"
            ))
        {
            beginner();
            ImGui::EndTabItem();
        }

        if (expert)
        {
            if (ImGui::BeginTabItem("Raw Hex"))
            {
                hex();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    "Validation & Repair"
                ))
            {
                validation();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    "Hardware Export"
                ))
            {
                exports();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(
                    "GameShark"
                ))
            {
                cheats();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    if (!note.empty())
    {
        ImGui::OpenPopup("Notice");
    }

    if (ImGui::BeginPopupModal(
            "Notice",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize
        ))
    {
        ImGui::TextWrapped(
            "%s",
            note.c_str()
        );

        if (ImGui::Button(
                "OK",
                ImVec2(
                    100.0F,
                    0.0F
                )
            ))
        {
            note.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}
