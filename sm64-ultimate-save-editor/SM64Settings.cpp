#include "SM64Settings.hpp"

#include "SM64SystemCore.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace sm64 {
namespace {

bool parseBool(const std::string& v, bool& out) {
    if (v == "1" || v == "true") { out = true; return true; }
    if (v == "0" || v == "false") { out = false; return true; }
    return false;
}

bool parseInt(const std::string& v, int base, long long& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(v.c_str(), &end, base);
    if (end == v.c_str() || *end != '\0') return false;
    out = parsed;
    return true;
}

}  // namespace

Settings parseSettings(const std::string& text) {
    Settings s;
    std::istringstream stream(text);
    std::string line;
    bool firstRecent = true;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        bool b = false;
        long long n = 0;
        if (key == "dark" && parseBool(value, b)) s.darkTheme = b;
        else if (key == "accent" && parseInt(value, 10, n) && n >= 0 && n <= 3) s.accent = static_cast<int>(n);
        else if (key == "scale") {
            char* end = nullptr;
            const float f = std::strtof(value.c_str(), &end);
            if (end != value.c_str() && *end == '\0') s.userScale = std::clamp(f, kMinUserScale, kMaxUserScale);
        }
        else if (key == "expert" && parseBool(value, b)) s.expert = b;
        else if (key == "autorepair" && parseBool(value, b)) s.autoRepairOnLoad = b;
        else if (key == "backup" && parseBool(value, b)) s.backupOnSave = b;
        else if (key == "tutorial" && parseBool(value, b)) s.tutorialSeen = b;
        else if (key == "forensic" && parseBool(value, b)) s.forensic = b;
        else if (key == "gs_base" && parseInt(value, 16, n) && n >= 0 && n <= 0xFFFFFFFFLL) s.gsBase = static_cast<std::uint32_t>(n);
        else if (key == "gs_16bit" && parseBool(value, b)) s.gsUse16 = b;
        else if (key == "gs_all" && parseBool(value, b)) s.gsIncludeAll = b;
        else if (key == "gs_comments" && parseBool(value, b)) s.gsComments = b;
        else if (key == "recent" && !value.empty()) {
            (void)firstRecent;
            if (s.recent.size() < kMaxRecentFiles) s.recent.push_back(value);
        }
    }
    return s;
}

std::string serializeSettings(const Settings& s) {
    std::ostringstream out;
    char base[16]{};
    std::snprintf(base, sizeof(base), "%08X", static_cast<unsigned>(s.gsBase));
    out << "# SM64 Ultimate Save Editor settings\n";
    out << "dark=" << (s.darkTheme ? 1 : 0) << "\n";
    out << "accent=" << s.accent << "\n";
    out << "scale=" << s.userScale << "\n";
    out << "expert=" << (s.expert ? 1 : 0) << "\n";
    out << "autorepair=" << (s.autoRepairOnLoad ? 1 : 0) << "\n";
    out << "backup=" << (s.backupOnSave ? 1 : 0) << "\n";
    out << "tutorial=" << (s.tutorialSeen ? 1 : 0) << "\n";
    out << "forensic=" << (s.forensic ? 1 : 0) << "\n";
    out << "gs_base=" << base << "\n";
    out << "gs_16bit=" << (s.gsUse16 ? 1 : 0) << "\n";
    out << "gs_all=" << (s.gsIncludeAll ? 1 : 0) << "\n";
    out << "gs_comments=" << (s.gsComments ? 1 : 0) << "\n";
    for (const std::string& path : s.recent) out << "recent=" << path << "\n";
    return out.str();
}

bool loadSettings(const std::filesystem::path& path, Settings& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out = parseSettings(text);
    return true;
}

bool saveSettings(const std::filesystem::path& path, const Settings& settings, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    const std::string text = serializeSettings(settings);
    AtomicWriteOptions options;
    options.makeBackup = false;
    return atomicWriteFile(
        path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
        options, error);
}

void addRecentFile(Settings& settings, const std::string& path) {
    settings.recent.erase(std::remove(settings.recent.begin(), settings.recent.end(), path),
                          settings.recent.end());
    settings.recent.insert(settings.recent.begin(), path);
    if (settings.recent.size() > kMaxRecentFiles) settings.recent.resize(kMaxRecentFiles);
}

}  // namespace sm64
