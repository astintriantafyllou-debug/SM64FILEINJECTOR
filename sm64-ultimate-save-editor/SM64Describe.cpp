// SM64Describe.cpp - human-readable descriptions of EEPROM offsets and diffs.
#include "SM64SystemCore.hpp"

#include <algorithm>
#include <cstdio>

namespace sm64 {
namespace {

// Names of courseStars[15..24] (the bonus stages), per levels/course_defines.h.
constexpr std::array<const char*, 10> kBonusByteNames{
    "Bowser in the Dark World", "Bowser in the Fire Sea",    "Bowser in the Sky",         "Princess's Secret Slide",
    "Cavern of the Metal Cap",  "Tower of the Wing Cap",     "Vanish Cap Under the Moat", "Wing Mario Over the Rainbow",
    "The Secret Aquarium",      "Unused (cake ending entry)"};

std::string fmt(const char* format, unsigned a = 0, unsigned b = 0, unsigned c = 0) {
    char text[160]{};
    std::snprintf(text, sizeof(text), format, a, b, c);
    return text;
}

std::string courseStarByteName(std::size_t index) {
    if (index < kMainCourseCount) return Courses[index];
    return kBonusByteNames[index - kMainCourseCount];
}

std::string slotLetterName(int slot) {
    return std::string(1U, static_cast<char>('A' + slot));
}

std::string onOff(bool v) {
    return v ? "on" : "off";
}

std::string starsText(std::uint8_t byte) {
    return std::to_string(popcount7(byte)) + " star(s) [" + fmt("%02X", byte & 0x7FU) + "]";
}

}  // namespace

RegionInfo describeOffset(std::size_t offset) {
    RegionInfo info;
    if (offset >= Size) {
        info.name = "Outside the EEPROM";
        return info;
    }
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo block = blockInfo(id);
        if (offset < block.offset || offset >= block.offset + block.size) continue;
        info.blockId = id;
        info.relative = offset - block.offset;
        const std::size_t r = info.relative;

        if (block.isMenu) {
            info.name = block.isBackup ? "Menu block backup" : "Menu block primary";
            if (r < 16U)
                info.field = "Coin-score ages, file " + slotLetterName(static_cast<int>(r / 4U)) + " (byte " +
                             std::to_string(r % 4U) + " of 4)";
            else if (r < 18U)
                info.field = "Sound mode (u16)";
            else if (r < 28U)
                info.field = "Padding (EU builds keep the language in bytes 18-19)";
            else if (r < 30U)
                info.field = "Signature 'HI' (0x4849)";
            else
                info.field = "Checksum (big-endian u16)";
            return info;
        }

        info.name = "Slot " + slotLetterName(block.slot) + (block.isBackup ? " backup" : " primary");
        if (r == slot_field::CapLevel)
            info.field = "Lost-cap level";
        else if (r == slot_field::CapArea)
            info.field = "Lost-cap area";
        else if (r < slot_field::Flags)
            info.field = "Lost-cap position (x/y/z s16)";
        else if (r < slot_field::CourseStars) {
            info.field = "Flags (byte " + std::to_string(r - slot_field::Flags) + " of 4, big-endian)";
        } else if (r < slot_field::CoinScores) {
            const std::size_t i = r - slot_field::CourseStars;
            info.field = "Stars: " + courseStarByteName(i) + " (bits 0-6 stars, bit 7 cannon flag)";
        } else if (r < slot_field::Signature) {
            info.field = std::string("Coin high score: ") + Courses[r - slot_field::CoinScores];
        } else if (r < slot_field::Signature + 2U) {
            info.field = "Signature 'DA' (0x4441)";
        } else {
            info.field = "Checksum (big-endian u16)";
        }
        return info;
    }
    return info;
}

std::vector<FieldChange> describeChanges(const EepromBytes& before, const EepromBytes& after) {
    std::vector<FieldChange> changes;

    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        SlotImage a{};
        SlotImage b{};
        std::copy_n(before.begin() + static_cast<std::ptrdiff_t>(P[slot]), Block, a.begin());
        std::copy_n(after.begin() + static_cast<std::ptrdiff_t>(P[slot]), Block, b.begin());
        const std::string prefix = "Slot " + slotLetterName(static_cast<int>(slot)) + ": ";
        bool contentChanged = false;

        const std::uint32_t fa = slotFlags(a);
        const std::uint32_t fb = slotFlags(b);
        for (const FlagInfo& flag : kFlagTable) {
            if ((fa & flag.mask) != (fb & flag.mask)) {
                changes.push_back({prefix + flag.name, onOff((fa & flag.mask) != 0U), onOff((fb & flag.mask) != 0U)});
                contentChanged = true;
            }
        }
        // Castle (Toad / MIPS) star bits, named from the secret-star table.
        for (const SecretStar& star : kSecretStars) {
            if (star.storage != StarStorage::CastleFlag) continue;
            if (secretStarSet(a, star) != secretStarSet(b, star)) {
                changes.push_back({prefix + star.name, secretStarSet(a, star) ? "collected" : "not collected",
                                   secretStarSet(b, star) ? "collected" : "not collected"});
                contentChanged = true;
            }
        }
        // Any other flag bits (unmapped) are reported once, raw.
        std::uint32_t known = save_flag::CastleStarMask;
        for (const FlagInfo& flag : kFlagTable) known |= flag.mask;
        if ((fa & ~known) != (fb & ~known)) {
            changes.push_back(
                {prefix + "Other flag bits", "0x" + fmt("%08X", fa & ~known), "0x" + fmt("%08X", fb & ~known)});
            contentChanged = true;
        }

        for (std::size_t i = 0; i < slot_field::CourseStarCount; ++i) {
            const std::uint8_t ba = courseByte(a, i);
            const std::uint8_t bb = courseByte(b, i);
            if (ba == bb) continue;
            std::string label = prefix + courseStarByteName(i);
            if ((ba & kStarBitsMask) != (bb & kStarBitsMask)) {
                changes.push_back({label + " stars", starsText(ba), starsText(bb)});
            }
            if ((ba & kCannonBit) != (bb & kCannonBit)) {
                changes.push_back(
                    {label + " byte: cannon flag", onOff((ba & kCannonBit) != 0U), onOff((bb & kCannonBit) != 0U)});
            }
            contentChanged = true;
        }
        for (std::size_t i = 0; i < slot_field::CoinScoreCount; ++i) {
            if (coinScore(a, i) != coinScore(b, i)) {
                changes.push_back({prefix + Courses[i] + " coin score", std::to_string(coinScore(a, i)),
                                   std::to_string(coinScore(b, i))});
                contentChanged = true;
            }
        }
        for (std::size_t i = slot_field::CapLevel; i < slot_field::Flags; ++i) {
            if (a[i] != b[i]) {
                changes.push_back(
                    {prefix + "Lost-cap location byte " + std::to_string(i), fmt("0x%02X", a[i]), fmt("0x%02X", b[i])});
                contentChanged = true;
            }
        }

        // Backup copy reporting: only when the copies end up different, or a
        // desynchronized pair becomes synchronized.
        const std::size_t pb = B[slot];
        const bool beforeSync = std::equal(before.begin() + static_cast<std::ptrdiff_t>(P[slot]),
                                           before.begin() + static_cast<std::ptrdiff_t>(P[slot] + Block),
                                           before.begin() + static_cast<std::ptrdiff_t>(pb));
        const bool afterSync = std::equal(after.begin() + static_cast<std::ptrdiff_t>(P[slot]),
                                          after.begin() + static_cast<std::ptrdiff_t>(P[slot] + Block),
                                          after.begin() + static_cast<std::ptrdiff_t>(pb));
        if (!afterSync) {
            std::size_t n = 0;
            for (std::size_t i = 0; i < Block; ++i) {
                if (after[P[slot] + i] != after[pb + i]) ++n;
            }
            changes.push_back({prefix + "Primary vs backup copy", beforeSync ? "in sync" : "different",
                               std::to_string(n) + " byte(s) differ"});
        } else if (!beforeSync) {
            changes.push_back({prefix + "Primary vs backup copy", "different", "in sync"});
        }

        if (!contentChanged) {
            const bool sigChanged =
                !std::equal(a.begin() + slot_field::Signature, a.end(), b.begin() + slot_field::Signature);
            if (sigChanged) {
                changes.push_back({prefix + "Signature / checksum", fmt("%04X", readBe16(a.data() + 54)),
                                   fmt("%04X", readBe16(b.data() + 54))});
            }
        }
    }

    // Menu block: summarised as a byte count.
    std::size_t menuBytes = 0;
    for (std::size_t i = MenuPrimaryOffset; i < Size; ++i) {
        if (before[i] != after[i]) ++menuBytes;
    }
    if (menuBytes != 0U) {
        changes.push_back({"Menu blocks (sound mode, coin-score ages, signatures)", "unchanged",
                           std::to_string(menuBytes) + " byte(s) changed"});
    }
    return changes;
}

}  // namespace sm64
