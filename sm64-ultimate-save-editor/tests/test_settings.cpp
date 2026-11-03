#include "SM64Settings.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

using namespace sm64;

TEST("settings round trip through text") {
    Settings in;
    in.darkTheme = false;
    in.accent = 2;
    in.userScale = 1.5F;
    in.expert = true;
    in.backupOnSave = false;
    in.forensic = true;
    in.gsBase = 0x80123456U;
    in.gsUse16 = false;
    in.recent = {"C:\\saves\\a.eep", "/home/me/b.eep"};
    const Settings out = parseSettings(serializeSettings(in));
    CHECK(!out.darkTheme);
    CHECK_EQ(out.accent, 2);
    CHECK_EQ(out.userScale, 1.5F);
    CHECK(out.expert);
    CHECK(!out.backupOnSave);
    CHECK(out.forensic);
    CHECK_EQ(out.gsBase, std::uint32_t{0x80123456U});
    CHECK(!out.gsUse16);
    CHECK_EQ(out.recent.size(), std::size_t{2});
    CHECK_EQ(out.recent[0], std::string("C:\\saves\\a.eep"));
}

TEST("settings ignore garbage, clamp scale and keep defaults") {
    const Settings s = parseSettings("scale=99\nbogus=1\naccent=17\nexpert=maybe\ndark=0\n\n=\nnoequals\n");
    CHECK_EQ(s.userScale, kMaxUserScale);
    CHECK_EQ(s.accent, 0);
    CHECK(!s.expert);
    CHECK(!s.darkTheme);
    CHECK_EQ(parseSettings("scale=0.1\n").userScale, kMinUserScale);
    CHECK_EQ(parseSettings("gs_base=zzzz\n").gsBase, std::uint32_t{0x80207700U});
}

TEST("recent files are de-duplicated, ordered and capped") {
    Settings s;
    for (int i = 0; i < 12; ++i) addRecentFile(s, "f" + std::to_string(i));
    CHECK_EQ(s.recent.size(), kMaxRecentFiles);
    CHECK_EQ(s.recent.front(), std::string("f11"));
    addRecentFile(s, "f8");
    CHECK_EQ(s.recent.front(), std::string("f8"));
    CHECK_EQ(s.recent.size(), kMaxRecentFiles);
    int count = 0;
    for (const auto& r : s.recent) count += r == "f8" ? 1 : 0;
    CHECK_EQ(count, 1);
}

TEST("settings save and load via the filesystem") {
    const auto dir = std::filesystem::temp_directory_path() / "sm64_settings_test";
    std::filesystem::remove_all(dir);
    Settings s;
    s.accent = 3;
    std::string error;
    CHECK(saveSettings(dir / "nested" / "settings.cfg", s, error));
    Settings loaded;
    CHECK(loadSettings(dir / "nested" / "settings.cfg", loaded));
    CHECK_EQ(loaded.accent, 3);
    CHECK(!loadSettings(dir / "missing.cfg", loaded));
    std::filesystem::remove_all(dir);
}
