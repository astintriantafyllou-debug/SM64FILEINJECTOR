#include "SM64Theme.hpp"


#include <algorithm>
#include <cmath>

namespace sm64ui {
namespace {

Palette gPalette{};

ImVec4 rgba(int r, int g, int b, float a = 1.0F) {
    return ImVec4(static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
                  static_cast<float>(b) / 255.0F, a);
}

ImVec4 withAlpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

ImVec4 scaleRgb(ImVec4 c, float f) {
    return ImVec4(std::clamp(c.x * f, 0.0F, 1.0F), std::clamp(c.y * f, 0.0F, 1.0F),
                  std::clamp(c.z * f, 0.0F, 1.0F), c.w);
}

}  // namespace

const Palette& palette() { return gPalette; }

void applyTheme(bool dark, int accentIndex, float totalScale) {
    // Accent choices follow the in-game cap colours (plus the star yellow default).
    static const ImVec4 accentsDark[4] = {rgba(247, 195, 37), rgba(232, 74, 74), rgba(70, 185, 110),
                                          rgba(80, 140, 235)};
    static const ImVec4 accentsLight[4] = {rgba(176, 124, 0), rgba(190, 40, 40), rgba(30, 130, 70),
                                           rgba(30, 90, 190)};
    const int idx = std::clamp(accentIndex, 0, 3);

    Palette p{};
    if (dark) {
        p.good = rgba(80, 220, 120);
        p.warning = rgba(250, 190, 50);
        p.bad = rgba(255, 95, 85);
        p.info = rgba(100, 165, 255);
        p.wing = rgba(235, 80, 80);
        p.metal = rgba(80, 200, 120);
        p.vanish = rgba(90, 150, 245);
        p.accent = accentsDark[idx];
        p.muted = rgba(150, 156, 170);
        p.changed = rgba(250, 190, 50, 0.28F);
        p.star = rgba(250, 205, 60);
    } else {
        p.good = rgba(20, 130, 60);
        p.warning = rgba(170, 100, 0);
        p.bad = rgba(190, 30, 30);
        p.info = rgba(20, 90, 200);
        p.wing = rgba(200, 40, 40);
        p.metal = rgba(25, 135, 70);
        p.vanish = rgba(30, 95, 200);
        p.accent = accentsLight[idx];
        p.muted = rgba(95, 100, 112);
        p.changed = rgba(235, 165, 20, 0.35F);
        p.star = rgba(200, 145, 0);
    }
    gPalette = p;

    ImGuiStyle fresh;  // default sizes, so ScaleAllSizes never compounds
    ImGuiStyle& style = ImGui::GetStyle();
    style = fresh;
    if (dark) ImGui::StyleColorsDark(&style); else ImGui::StyleColorsLight(&style);

    style.WindowRounding = 6.0F;
    style.ChildRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(8.0F, 5.0F);
    style.ItemSpacing = ImVec2(8.0F, 7.0F);
    style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
    style.CellPadding = ImVec2(6.0F, 4.0F);
    style.ScrollbarSize = 14.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.SeparatorTextBorderSize = 2.0F;

    ImVec4* c = style.Colors;
    const ImVec4 a = p.accent;
    if (dark) {
        c[ImGuiCol_WindowBg] = rgba(20, 22, 28);
        c[ImGuiCol_ChildBg] = rgba(26, 29, 37);
        c[ImGuiCol_PopupBg] = rgba(30, 33, 42);
        c[ImGuiCol_Border] = rgba(54, 60, 74);
        c[ImGuiCol_FrameBg] = rgba(38, 42, 54);
        c[ImGuiCol_FrameBgHovered] = rgba(50, 55, 70);
        c[ImGuiCol_FrameBgActive] = rgba(60, 66, 84);
        c[ImGuiCol_MenuBarBg] = rgba(16, 18, 23);
        c[ImGuiCol_TableRowBgAlt] = rgba(255, 255, 255, 0.03F);
        c[ImGuiCol_TableHeaderBg] = rgba(36, 40, 52);
        c[ImGuiCol_TableBorderStrong] = rgba(60, 66, 82);
        c[ImGuiCol_TableBorderLight] = rgba(44, 49, 62);
        c[ImGuiCol_Text] = rgba(232, 235, 242);
        c[ImGuiCol_TextDisabled] = p.muted;
    } else {
        c[ImGuiCol_WindowBg] = rgba(243, 244, 247);
        c[ImGuiCol_ChildBg] = rgba(252, 252, 254);
        c[ImGuiCol_PopupBg] = rgba(255, 255, 255);
        c[ImGuiCol_Border] = rgba(205, 209, 220);
        c[ImGuiCol_FrameBg] = rgba(231, 234, 242);
        c[ImGuiCol_FrameBgHovered] = rgba(220, 225, 237);
        c[ImGuiCol_FrameBgActive] = rgba(206, 213, 230);
        c[ImGuiCol_MenuBarBg] = rgba(232, 234, 240);
        c[ImGuiCol_TableRowBgAlt] = rgba(0, 0, 0, 0.035F);
        c[ImGuiCol_TableHeaderBg] = rgba(226, 230, 240);
        c[ImGuiCol_Text] = rgba(28, 31, 40);
        c[ImGuiCol_TextDisabled] = p.muted;
    }
    c[ImGuiCol_CheckMark] = a;
    c[ImGuiCol_SliderGrab] = a;
    c[ImGuiCol_SliderGrabActive] = scaleRgb(a, 1.15F);
    c[ImGuiCol_Button] = withAlpha(a, dark ? 0.22F : 0.18F);
    c[ImGuiCol_ButtonHovered] = withAlpha(a, dark ? 0.38F : 0.32F);
    c[ImGuiCol_ButtonActive] = withAlpha(a, dark ? 0.55F : 0.48F);
    c[ImGuiCol_Header] = withAlpha(a, 0.22F);
    c[ImGuiCol_HeaderHovered] = withAlpha(a, 0.36F);
    c[ImGuiCol_HeaderActive] = withAlpha(a, 0.50F);
    c[ImGuiCol_Tab] = withAlpha(a, 0.16F);
    c[ImGuiCol_TabHovered] = withAlpha(a, 0.45F);
    c[ImGuiCol_TabSelected] = withAlpha(a, 0.40F);
    c[ImGuiCol_NavCursor] = a;
    c[ImGuiCol_TextSelectedBg] = withAlpha(a, 0.35F);
    c[ImGuiCol_SeparatorHovered] = a;

    style.ScaleAllSizes(std::max(0.5F, totalScale));
}

void configureFonts(float totalScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config;
    config.SizePixels = std::round(15.0F * std::max(0.5F, totalScale));
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = true;
    io.Fonts->AddFontDefault(&config);
    io.Fonts->Build();
}

ImVec4 statusColor(Status status) {
    switch (status) {
        case Status::Good: return gPalette.good;
        case Status::Warning: return gPalette.warning;
        case Status::Bad: return gPalette.bad;
        case Status::Info: return gPalette.info;
    }
    return gPalette.info;
}

const char* statusWord(Status status) {
    switch (status) {
        case Status::Good: return "OK";
        case Status::Warning: return "Warning";
        case Status::Bad: return "Problem";
        case Status::Info: return "Info";
    }
    return "";
}

void statusIcon(Status status) {
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(statusColor(status));
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImVec2 c(pos.x + size * 0.5F, pos.y + size * 0.5F);
    const float r = size * 0.46F;
    const float t = std::max(1.5F, size * 0.11F);
    switch (status) {
        case Status::Good:
            dl->AddCircleFilled(c, r, col, 20);
            dl->AddLine(ImVec2(c.x - r * 0.5F, c.y), ImVec2(c.x - r * 0.1F, c.y + r * 0.42F), white, t);
            dl->AddLine(ImVec2(c.x - r * 0.1F, c.y + r * 0.42F), ImVec2(c.x + r * 0.55F, c.y - r * 0.4F), white, t);
            break;
        case Status::Warning:
            dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 1.05F, c.y + r * 0.85F),
                                  ImVec2(c.x - r * 1.05F, c.y + r * 0.85F), col);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.2F), ImVec2(c.x, c.y + r * 0.32F), white, t);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.62F), t * 0.65F, white, 8);
            break;
        case Status::Bad:
            dl->AddCircleFilled(c, r, col, 20);
            dl->AddLine(ImVec2(c.x - r * 0.38F, c.y - r * 0.38F), ImVec2(c.x + r * 0.38F, c.y + r * 0.38F), white, t);
            dl->AddLine(ImVec2(c.x - r * 0.38F, c.y + r * 0.38F), ImVec2(c.x + r * 0.38F, c.y - r * 0.38F), white, t);
            break;
        case Status::Info:
            dl->AddCircleFilled(c, r, col, 20);
            dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.45F), t * 0.7F, white, 8);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.1F), ImVec2(c.x, c.y + r * 0.5F), white, t);
            break;
    }
}

void statusLabel(Status status, const char* text) {
    statusIcon(status);
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(statusColor(status), "%s", text);
}

void drawStar(ImDrawList* list, ImVec2 center, float radius, ImU32 color) {
    constexpr float kPi = 3.14159265358979F;
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float angle = -kPi / 2.0F + static_cast<float>(i) * kPi / 5.0F;
        const float r = (i % 2 == 0) ? radius : radius * 0.42F;
        pts[i] = ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r);
    }
    list->AddConvexPolyFilled(pts, 10, color);
}

void drawRing(ImDrawList* list, ImVec2 center, float radius, float thickness, float fraction,
              ImU32 trackColor, ImU32 fillColor) {
    constexpr float kPi = 3.14159265358979F;
    list->AddCircle(center, radius, trackColor, 48, thickness);
    const float f = std::clamp(fraction, 0.0F, 1.0F);
    if (f <= 0.0F) return;
    const float a0 = -kPi / 2.0F;
    list->PathArcTo(center, radius, a0, a0 + f * 2.0F * kPi, 48);
    list->PathStroke(fillColor, ImDrawFlags_None, thickness);
}

}  // namespace sm64ui
