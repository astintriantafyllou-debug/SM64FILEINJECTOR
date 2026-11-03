#include "SM64Report.hpp"
#include "test_framework.hpp"

#include <string>
#include <vector>

using namespace sm64;

TEST("crc32 matches the standard check vector and the empty input") {
    const std::string text = "123456789";
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    CHECK_EQ(crc32(bytes), std::uint32_t{0xCBF43926U});
    CHECK_EQ(crc32(std::span<const std::uint8_t>{}), std::uint32_t{0});
}

TEST("jsonEscape escapes quotes, backslashes and control characters") {
    CHECK_EQ(jsonEscape("a\"b\\c\n\t"), std::string("a\\\"b\\\\c\\n\\t"));
    CHECK_EQ(jsonEscape(std::string(1, '\x01')), std::string("\\u0001"));
}

TEST("JSON report is well formed and reports validity and star totals") {
    Core core;
    CHECK(core.setSecretStarFlag(2, 0, true));
    const std::string json = buildJsonReport(core.view(), "C:\\saves\\my \"file\".eep", core.detectedOrder());
    int depthBrace = 0;
    int depthBracket = 0;
    bool inString = false;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const char ch = json[i];
        if (inString) {
            if (ch == '\\') ++i;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') inString = true;
        else if (ch == '{') ++depthBrace;
        else if (ch == '}') --depthBrace;
        else if (ch == '[') ++depthBracket;
        else if (ch == ']') --depthBracket;
        CHECK(depthBrace >= 0 && depthBracket >= 0);
    }
    CHECK_EQ(depthBrace, 0);
    CHECK_EQ(depthBracket, 0);
    CHECK(!inString);
    CHECK(json.find("\"valid\": true") != std::string::npos);
    CHECK(json.find("\"size\": 512") != std::string::npos);
    CHECK(json.find("my \\\"file\\\".eep") != std::string::npos);
    CHECK(json.find("\"slot\": \"C\"") != std::string::npos);
    CHECK(json.find("\"totalStars\": 1") != std::string::npos);
}

TEST("validation text names every block and flags a broken one") {
    Core core;
    CHECK(core.set(5, 0x77));  // raw edit breaks slot A primary
    const std::string text = buildValidationText(core.view(), "broken.eep");
    CHECK(text.find("PROBLEMS FOUND") != std::string::npos);
    CHECK(text.find("slot-A-primary") != std::string::npos);
    CHECK(text.find("menu-backup") != std::string::npos);
    CHECK(text.find("checksum BAD") != std::string::npos);
    CHECK(text.find("restored from the valid backup") != std::string::npos);
}
