// SM64Theme.hpp - palettes, ImGui style scaling and small drawn icons.
//
// Icons are drawn with ImDrawList primitives so the project needs no icon font
// or image assets. Status is always conveyed by shape AND text, never colour alone.
#pragma once

#include <imgui.h>

namespace sm64ui {

enum class Status { Good, Warning, Bad, Info };

struct Palette {
    ImVec4 good;
    ImVec4 warning;
    ImVec4 bad;
    ImVec4 info;
    ImVec4 wing;    // red cap switch
    ImVec4 metal;   // green cap switch
    ImVec4 vanish;  // blue cap switch
    ImVec4 accent;
    ImVec4 muted;
    ImVec4 changed;  // "differs from the saved file" highlight
    ImVec4 star;
};

[[nodiscard]] const Palette& palette();

// Rebuilds the ImGui style from scratch (so repeated calls never compound the
// scale) using `totalScale` = operating-system DPI scale x user scale.
void applyTheme(bool dark, int accentIndex, float totalScale);

// Replaces the default font with one rasterized at the requested scale.
// The caller must refresh the renderer's font texture afterwards.
void configureFonts(float totalScale);

[[nodiscard]] ImVec4 statusColor(Status status);
[[nodiscard]] const char* statusWord(Status status);

// Draws a status glyph (check / triangle / cross / info) sized to the text line.
void statusIcon(Status status);
// Icon followed by colored text on the same line.
void statusLabel(Status status, const char* text);

void drawStar(ImDrawList* list, ImVec2 center, float radius, ImU32 color);
void drawRing(ImDrawList* list, ImVec2 center, float radius, float thickness, float fraction, ImU32 trackColor,
              ImU32 fillColor);

}  // namespace sm64ui
