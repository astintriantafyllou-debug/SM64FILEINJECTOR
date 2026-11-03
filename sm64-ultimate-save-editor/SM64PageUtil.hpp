// SM64PageUtil.hpp - small helpers shared by the page implementation files.
#pragma once

#include "SM64App.hpp"
#include "SM64Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace sm64ui::detail {

// Muted text that wraps at the window edge instead of being clipped.
inline void mutedText(const char* fmt, ...) IM_FMTARGS(1);
inline void mutedText(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextV(fmt, args);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    va_end(args);
}

// Outer height for a table of plain text rows: `rows` rows plus a header, plus room for a
// horizontal scrollbar. Used so a table never grows a pointless inner vertical scrollbar.
inline float textTableHeight(int rows) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float row = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0F;
    return row * static_cast<float>(rows + 1) + style.ScrollbarSize + 4.0F;
}

inline float em() { return ImGui::GetFontSize(); }

inline const char* slotName(int slot) {
    static const char* names[] = {"Slot A", "Slot B", "Slot C", "Slot D"};
    return names[std::clamp(slot, 0, 3)];
}

inline std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

inline sm64::SlotImage slotOf(const sm64::EepromBytes& bytes, int slot) {
    sm64::SlotImage image{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(sm64::P[static_cast<std::size_t>(slot)]),
                sm64::Block, image.begin());
    return image;
}

inline const sm64::FlagInfo* findFlag(std::uint32_t mask) {
    for (const sm64::FlagInfo& info : sm64::kFlagTable) {
        if (info.mask == mask) return &info;
    }
    return nullptr;
}

// Lays out buttons left to right and wraps to a new line when the row is full.
class FlowRow {
public:
    FlowRow() : limit_(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {}
    bool button(const char* label, bool enabled = true) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0F;
        if (!first_ && lastMax_ + style.ItemSpacing.x + width <= limit_) ImGui::SameLine();
        first_ = false;
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(label);
        ImGui::EndDisabled();
        lastMax_ = ImGui::GetItemRectMax().x;
        return clicked;
    }

private:
    float limit_;
    float lastMax_{0.0F};
    bool first_{true};
};

inline void tooltipText(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0F);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline void labelValue(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::TextWrapped("%s", value.c_str());
}

inline Status pairStatus(sm64::PairStatus status) {
    switch (status) {
        case sm64::PairStatus::BothValidIdentical: return Status::Good;
        case sm64::PairStatus::OnlyPrimaryValid:
        case sm64::PairStatus::OnlyBackupValid:
        case sm64::PairStatus::BothValidDifferent: return Status::Warning;
        case sm64::PairStatus::BothInvalid: return Status::Bad;
    }
    return Status::Info;
}

inline const char* pairStatusText(sm64::PairStatus status) {
    switch (status) {
        case sm64::PairStatus::BothValidIdentical: return "Healthy";
        case sm64::PairStatus::OnlyPrimaryValid: return "Backup copy invalid (recoverable)";
        case sm64::PairStatus::OnlyBackupValid: return "Primary copy invalid (recoverable)";
        case sm64::PairStatus::BothValidDifferent: return "Copies differ (your choice needed)";
        case sm64::PairStatus::BothInvalid: return "Both copies invalid";
    }
    return "";
}

inline std::string pairName(std::size_t pair) {
    return pair < sm64::kSlotCount ? std::string("Slot ") + static_cast<char>('A' + pair) : std::string("Menu block");
}

inline std::string blockName(std::size_t id) {
    const sm64::BlockInfo info = sm64::blockInfo(id);
    if (info.isMenu) return info.isBackup ? "Menu backup" : "Menu primary";
    return std::string("Slot ") + static_cast<char>('A' + info.slot) + (info.isBackup ? " backup" : " primary");
}

inline std::string hex(unsigned value, int width) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%0*X", width, value);
    return text;
}


}  // namespace sm64ui::detail
