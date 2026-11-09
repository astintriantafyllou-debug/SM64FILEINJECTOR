// SM64Settings.hpp - persisted user preferences (plain key=value text file).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sm64 {

struct Settings {
    bool darkTheme{true};
    int accent{0};          // 0 star yellow, 1 wing red, 2 metal green, 3 vanish blue
    float userScale{1.0F};  // multiplied with the operating-system DPI scale
    bool expert{false};
    bool autoRepairOnLoad{true};
    bool backupOnSave{true};
    bool tutorialSeen{false};
    bool forensic{false};  // read-only mode: no edits, no saving over the source
    std::uint32_t gsBase{0x80207700U};
    bool gsUse16{true};
    bool gsIncludeAll{false};
    bool gsComments{true};
    std::vector<std::string> recent;  // most recent first
};

inline constexpr std::size_t kMaxRecentFiles = 8;
inline constexpr float kMinUserScale = 0.75F;
inline constexpr float kMaxUserScale = 2.0F;

// Unknown keys and malformed values are ignored (the default is kept).
[[nodiscard]] Settings parseSettings(const std::string& text);
[[nodiscard]] std::string serializeSettings(const Settings& settings);
[[nodiscard]] bool loadSettings(const std::filesystem::path& path, Settings& out);
[[nodiscard]] bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error);
void addRecentFile(Settings& settings, const std::string& path);

}  // namespace sm64
