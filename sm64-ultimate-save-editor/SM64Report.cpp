#include "SM64Report.hpp"

#include <array>
#include <cstdio>

namespace sm64 {
namespace {

std::string hex(unsigned value, int width) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%0*X", width, value);
    return text;
}

const char* statusName(PairStatus status) {
    switch (status) {
        case PairStatus::BothValidIdentical:
            return "both-valid-identical";
        case PairStatus::BothValidDifferent:
            return "both-valid-different";
        case PairStatus::OnlyPrimaryValid:
            return "only-primary-valid";
        case PairStatus::OnlyBackupValid:
            return "only-backup-valid";
        case PairStatus::BothInvalid:
            return "both-invalid";
    }
    return "unknown";
}

std::string blockName(std::size_t id) {
    const BlockInfo info = blockInfo(id);
    if (info.isMenu) return info.isBackup ? "menu-backup" : "menu-primary";
    return std::string("slot-") + static_cast<char>('A' + info.slot) + (info.isBackup ? "-backup" : "-primary");
}

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256U; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::uint8_t byte : data) crc = table[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
    return crc ^ 0xFFFFFFFFU;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    out += "\\u" + hex(static_cast<unsigned char>(ch), 4);
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string buildJsonReport(const EepromBytes& bytes, std::string_view fileName, ByteOrder order) {
    const Report report = validateBytes(bytes);
    std::string j = "{\n";
    j += "  \"tool\": \"SM64 Ultimate Save Editor\",\n";
    j += "  \"file\": \"" + jsonEscape(fileName) + "\",\n";
    j += "  \"size\": " + std::to_string(bytes.size()) + ",\n";
    j += "  \"crc32\": \"" + hex(crc32(bytes), 8) + "\",\n";
    j += "  \"byteOrder\": \"" + jsonEscape(byteOrderName(order)) + "\",\n";
    j += std::string("  \"valid\": ") + (report.valid ? "true" : "false") + ",\n";

    j += "  \"blocks\": [\n";
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo info = blockInfo(id);
        const BlockState& s = report.block(id);
        j += "    {\"name\": \"" + blockName(id) + "\", \"offset\": " + std::to_string(info.offset) +
             ", \"size\": " + std::to_string(info.size) + ", \"magicOk\": " + (s.magic ? "true" : "false") +
             ", \"checksumOk\": " + (s.checksum ? "true" : "false") + ", \"stored\": \"" + hex(s.stored, 4) +
             "\", \"computed\": \"" + hex(s.computed, 4) + "\"}" + (id + 1U < kBlockCount ? "," : "") + "\n";
    }
    j += "  ],\n  \"pairs\": [\n";
    for (std::size_t p = 0; p < kPairCount; ++p) {
        j += std::string("    {\"name\": \"") +
             (p < kSlotCount ? std::string("slot-") + static_cast<char>('A' + p) : "menu") + "\", \"status\": \"" +
             statusName(report.pairs[p].status) +
             "\", \"differingBytes\": " + std::to_string(report.pairs[p].differingOffsets.size()) + "}" +
             (p + 1U < kPairCount ? "," : "") + "\n";
    }
    j += "  ],\n  \"slots\": [\n";
    for (int slot = 0; slot < 4; ++slot) {
        SlotImage image{};
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(P[static_cast<std::size_t>(slot)]), Block,
                    image.begin());
        const StarSummary stars = summarizeStars(image);
        const std::uint32_t flags = slotFlags(image);
        j += std::string("    {\"slot\": \"") + static_cast<char>('A' + slot) + "\", \"flags\": \"" + hex(flags, 8) +
             "\", \"occupied\": " + ((flags & save_flag::FileExists) != 0U ? "true" : "false") +
             ", \"totalStars\": " + std::to_string(stars.total) +
             ", \"mainStars\": " + std::to_string(stars.mainStars) +
             ", \"secretStars\": " + std::to_string(stars.secretStars) + ", \"coinScores\": [";
        for (std::size_t c = 0; c < slot_field::CoinScoreCount; ++c) {
            j += std::to_string(coinScore(image, c)) + (c + 1U < slot_field::CoinScoreCount ? ", " : "");
        }
        j += std::string("]}") + (slot < 3 ? "," : "") + "\n";
    }
    j += "  ]\n}\n";
    return j;
}

std::string buildValidationText(const EepromBytes& bytes, std::string_view fileName) {
    const Report report = validateBytes(bytes);
    std::string t = "SM64 Ultimate Save Editor - validation report\n";
    t += "File: " + std::string(fileName) + "\n";
    t += "Overall: " + std::string(report.valid ? "all blocks valid" : "PROBLEMS FOUND") + "\n\n";
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockState& s = report.block(id);
        const BlockInfo info = blockInfo(id);
        t += blockName(id) + " @0x" + hex(static_cast<unsigned>(info.offset), 3) + ": signature " +
             (s.magic ? "OK" : "BAD") + " (0x" + hex(s.storedMagic, 4) + "), checksum " + (s.checksum ? "OK" : "BAD") +
             " (stored " + hex(s.stored, 4) + ", computed " + hex(s.computed, 4) + ")\n";
    }
    t += "\n";
    for (std::size_t p = 0; p < kPairCount; ++p) {
        t += std::string(p < kSlotCount ? std::string("Slot ") + static_cast<char>('A' + p) : "Menu") + ": " +
             report.pairs[p].text + "\n";
    }
    return t;
}

}  // namespace sm64
