// SM64SaveFormat.hpp
//
// Verified layout and bit maps for the retail Super Mario 64 512-byte EEPROM.
//
// Every constant in this file was cross-checked against the n64decomp/sm64
// decompilation (src/game/save_file.c, src/game/save_file.h,
// include/course_table.h, levels/course_defines.h, src/game/interaction.c).
// See SAVE_FORMAT.md for the evidence table. Nothing here is guessed.
//
// All multi-byte values in the EEPROM are big-endian. Nothing in this file
// depends on host endianness or struct packing.
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sm64 {

// --------------------------------------------------------------------------
// File layout
// --------------------------------------------------------------------------
inline constexpr std::size_t Size = 512;   // whole EEPROM (4 Kbit)
inline constexpr std::size_t Block = 56;   // one save-slot copy (struct SaveFile)
inline constexpr std::size_t kSlotCount = 4;
inline constexpr std::size_t kMenuBlockSize = 32;  // struct MainMenuSaveData
inline constexpr std::size_t MenuPrimaryOffset = 0x1C0;
inline constexpr std::size_t MenuBackupOffset = 0x1E0;

// Slot copies: primary at P[n], backup at B[n].
inline constexpr std::array<std::size_t, 4> P{0x000, 0x070, 0x0E0, 0x150};
inline constexpr std::array<std::size_t, 4> B{0x038, 0x0A8, 0x118, 0x188};

// Block signatures (u16 magic followed by u16 checksum, both big-endian).
// MENU_DATA_MAGIC = 0x4849 ("HI"), SAVE_FILE_MAGIC = 0x4441 ("DA").
inline constexpr std::uint16_t kSlotMagic = 0x4441;
inline constexpr std::uint16_t kMenuMagic = 0x4849;

// 10 blocks in file order: A-primary, A-backup, B-primary, ... D-backup,
// menu-primary, menu-backup.
inline constexpr std::size_t kBlockCount = 10;
inline constexpr std::size_t kPairCount = 5;  // 4 slots + menu

struct BlockInfo {
    std::size_t offset;
    std::size_t size;
    std::uint16_t magic;
    bool isMenu;
    int slot;      // 0-3, or -1 for the menu block
    bool isBackup;
};

[[nodiscard]] constexpr BlockInfo blockInfo(std::size_t id) noexcept {
    if (id < 8U) {
        const std::size_t slot = id / 2U;
        const bool backup = (id % 2U) != 0U;
        return BlockInfo{backup ? B[slot] : P[slot], Block, kSlotMagic, false,
                         static_cast<int>(slot), backup};
    }
    const bool backup = id == 9U;
    return BlockInfo{backup ? MenuBackupOffset : MenuPrimaryOffset, kMenuBlockSize, kMenuMagic,
                     true, -1, backup};
}

// --------------------------------------------------------------------------
// Offsets inside one 56-byte save slot (struct SaveFile)
// --------------------------------------------------------------------------
namespace slot_field {
inline constexpr std::size_t CapLevel = 0;    // u8
inline constexpr std::size_t CapArea = 1;     // u8
inline constexpr std::size_t CapPos = 2;      // Vec3s, 3 x s16 big-endian
inline constexpr std::size_t Flags = 8;       // u32 big-endian
inline constexpr std::size_t CourseStars = 12;  // u8[25]
inline constexpr std::size_t CourseStarCount = 25;
inline constexpr std::size_t CoinScores = 37;  // u8[15]
inline constexpr std::size_t CoinScoreCount = 15;
inline constexpr std::size_t Signature = 52;  // u16 magic, u16 checksum
}  // namespace slot_field

static_assert(slot_field::Flags + 4U == slot_field::CourseStars);
static_assert(slot_field::CourseStars + slot_field::CourseStarCount == slot_field::CoinScores);
static_assert(slot_field::CoinScores + slot_field::CoinScoreCount == slot_field::Signature);
static_assert(slot_field::Signature + 4U == Block);
static_assert(8U * Block + 2U * kMenuBlockSize == Size);
static_assert(P[3] + Block == B[3] && B[3] + Block == MenuPrimaryOffset);
static_assert(MenuPrimaryOffset + kMenuBlockSize == MenuBackupOffset);
static_assert(MenuBackupOffset + kMenuBlockSize == Size);

// Bits of the 32-bit flags word (SAVE_FLAG_* in save_file.h).
namespace save_flag {
inline constexpr std::uint32_t FileExists = 1U << 0;
inline constexpr std::uint32_t HaveWingCap = 1U << 1;
inline constexpr std::uint32_t HaveMetalCap = 1U << 2;
inline constexpr std::uint32_t HaveVanishCap = 1U << 3;
inline constexpr std::uint32_t HaveKey1 = 1U << 4;  // basement key (Bowser in the Dark World)
inline constexpr std::uint32_t HaveKey2 = 1U << 5;  // upstairs key (Bowser in the Fire Sea)
inline constexpr std::uint32_t UnlockedBasementDoor = 1U << 6;
inline constexpr std::uint32_t UnlockedUpstairsDoor = 1U << 7;
inline constexpr std::uint32_t DddMovedBack = 1U << 8;
inline constexpr std::uint32_t MoatDrained = 1U << 9;
inline constexpr std::uint32_t UnlockedPssDoor = 1U << 10;   // 1-star door (upper)
inline constexpr std::uint32_t UnlockedWfDoor = 1U << 11;    // 1-star door
inline constexpr std::uint32_t UnlockedCcmDoor = 1U << 12;   // 3-star door
inline constexpr std::uint32_t UnlockedJrbDoor = 1U << 13;   // 3-star door
inline constexpr std::uint32_t UnlockedBitdwDoor = 1U << 14;  // 8-star door
inline constexpr std::uint32_t UnlockedBitfsDoor = 1U << 15;  // 30-star door
inline constexpr std::uint32_t CapOnGround = 1U << 16;
inline constexpr std::uint32_t CapOnKlepto = 1U << 17;
inline constexpr std::uint32_t CapOnUkiki = 1U << 18;
inline constexpr std::uint32_t CapOnMrBlizzard = 1U << 19;
inline constexpr std::uint32_t Unlocked50StarDoor = 1U << 20;
// Bits 24-30 hold the "castle secret" star flags (SAVE_FLAG_TO_STAR_FLAG).
inline constexpr unsigned CastleStarShift = 24U;
inline constexpr std::uint32_t CastleStarMask = 0x7FU << CastleStarShift;
}  // namespace save_flag

struct FlagInfo {
    std::uint32_t mask;
    const char* name;
    const char* tooltip;
};

inline constexpr std::array<FlagInfo, 21> kFlagTable{{
    {save_flag::FileExists, "File occupied",
     "Marks the slot as an existing save file. Empty slots show 'NEW' on the file-select screen."},
    {save_flag::HaveWingCap, "Wing Cap switch",
     "Pressed the red switch: the Wing Cap blocks appear. Mario does not wear a cap permanently, and "
     "flying still needs a triple jump or a cannon launch."},
    {save_flag::HaveMetalCap, "Metal Cap switch",
     "Pressed the green switch: the Metal Cap blocks appear. It only unlocks the blocks."},
    {save_flag::HaveVanishCap, "Vanish Cap switch",
     "Pressed the blue switch: the Vanish Cap blocks appear. It only unlocks the blocks."},
    {save_flag::HaveKey1, "Basement key (held)",
     "Mario is carrying the basement key. The game clears this when the door is unlocked."},
    {save_flag::HaveKey2, "Upstairs key (held)",
     "Mario is carrying the upstairs key. The game clears this when the door is unlocked."},
    {save_flag::UnlockedBasementDoor, "Basement door unlocked",
     "The basement door has been opened with the key."},
    {save_flag::UnlockedUpstairsDoor, "Upstairs door unlocked",
     "The upstairs door has been opened with the key."},
    {save_flag::DddMovedBack, "Dire, Dire Docks moved",
     "The Dire, Dire Docks painting has moved back, revealing the Bowser in the Fire Sea area."},
    {save_flag::MoatDrained, "Castle moat drained",
     "The moat water has been drained after the Water Pillar event."},
    {save_flag::UnlockedPssDoor, "Slide door unlocked (1-star)",
     "Star-door flag for the upper 1-star door."},
    {save_flag::UnlockedWfDoor, "Whomp's door unlocked (1-star)",
     "Star-door flag for the 1-star door in the lobby."},
    {save_flag::UnlockedCcmDoor, "Cool, Cool door unlocked (3-star)",
     "Star-door flag for the 3-star door by Cool, Cool Mountain."},
    {save_flag::UnlockedJrbDoor, "Jolly Roger door unlocked (3-star)",
     "Star-door flag for the 3-star door by Jolly Roger Bay."},
    {save_flag::UnlockedBitdwDoor, "Dark World door unlocked (8-star)",
     "Star-door flag for the 8-star door."},
    {save_flag::UnlockedBitfsDoor, "Fire Sea door unlocked (30-star)",
     "Star-door flag for the 30-star door."},
    {save_flag::Unlocked50StarDoor, "50-star door unlocked",
     "Star-door flag for the 50-star door to the third floor."},
    {save_flag::CapOnGround, "Lost cap: on the ground",
     "Mario's cap was lost in a stage and is lying on the ground."},
    {save_flag::CapOnKlepto, "Lost cap: Klepto has it",
     "Klepto the condor is carrying Mario's cap."},
    {save_flag::CapOnUkiki, "Lost cap: Ukiki has it",
     "Ukiki the monkey has Mario's cap."},
    {save_flag::CapOnMrBlizzard, "Lost cap: Mr. Blizzard has it",
     "Mr. Blizzard the snowman has Mario's cap."},
}};

// --------------------------------------------------------------------------
// Courses and stars
// --------------------------------------------------------------------------
inline constexpr std::size_t kMainCourseCount = 15;
// courseStars[0..14] = the 15 main courses (COURSE_BOB .. COURSE_RR).
// courseStars[15..23] = the 9 bonus stages; [24] = cake end (unused).
inline constexpr std::array<const char*, 15> Courses{
    "Bob-omb Battlefield", "Whomp's Fortress",  "Jolly Roger Bay",
    "Cool, Cool Mountain", "Big Boo's Haunt",   "Hazy Maze Cave",
    "Lethal Lava Land",    "Shifting Sand Land", "Dire, Dire Docks",
    "Snowman's Land",      "Wet-Dry World",     "Tall, Tall Mountain",
    "Tiny-Huge Island",    "Tick Tock Clock",   "Rainbow Ride"};

// Bit 7 of a course byte is the cannon-open flag. The game indexes it with the
// 1-based course number, so the cannon flag for course N lives in the star byte
// that follows course N. It is NOT a star: star bits are 0-6 (mask 0x7F).
inline constexpr std::uint8_t kStarBitsMask = 0x7F;
inline constexpr std::uint8_t kCannonBit = 0x80;
inline constexpr int kStarsPerMainCourse = 7;  // six missions + the 100-coin star

enum class StarStorage : std::uint8_t { CourseByte, CastleFlag };

struct SecretStar {
    const char* group;
    const char* name;
    StarStorage storage;
    std::uint8_t byteIndex;  // courseStars[] index (CourseByte only)
    std::uint8_t bit;        // bit in that byte, or bit in the 32-bit flags word (CastleFlag)
};

// The 15 stars that complete a 120-star file: 105 main + 15 below = 120.
inline constexpr std::size_t kSecretStarCount = 15;
inline constexpr std::array<SecretStar, kSecretStarCount> kSecretStars{{
    {"Bowser courses", "Bowser in the Dark World", StarStorage::CourseByte, 15, 0},
    {"Bowser courses", "Bowser in the Fire Sea", StarStorage::CourseByte, 16, 0},
    {"Bowser courses", "Bowser in the Sky", StarStorage::CourseByte, 17, 0},
    {"Princess's Secret Slide", "Slide star 1", StarStorage::CourseByte, 18, 0},
    {"Princess's Secret Slide", "Slide star 2", StarStorage::CourseByte, 18, 1},
    {"Cap stages", "Cavern of the Metal Cap", StarStorage::CourseByte, 19, 0},
    {"Cap stages", "Tower of the Wing Cap", StarStorage::CourseByte, 20, 0},
    {"Cap stages", "Vanish Cap Under the Moat", StarStorage::CourseByte, 21, 0},
    {"Other secret stages", "Wing Mario Over the Rainbow", StarStorage::CourseByte, 22, 0},
    {"Other secret stages", "The Secret Aquarium", StarStorage::CourseByte, 23, 0},
    {"Toad", "Toad star 1", StarStorage::CastleFlag, 0, 24},
    {"Toad", "Toad star 2", StarStorage::CastleFlag, 0, 25},
    {"Toad", "Toad star 3", StarStorage::CastleFlag, 0, 26},
    {"MIPS the Rabbit", "MIPS star 1", StarStorage::CastleFlag, 0, 27},
    {"MIPS the Rabbit", "MIPS star 2", StarStorage::CastleFlag, 0, 28},
}};

inline constexpr int kMaxStars = 120;
static_assert(static_cast<int>(kMainCourseCount) * kStarsPerMainCourse +
                      static_cast<int>(kSecretStarCount) ==
                  kMaxStars,
              "105 main-course stars + 15 secret stars must equal 120");

// --------------------------------------------------------------------------
// Slot image helpers (pure functions over one 56-byte slot)
// --------------------------------------------------------------------------
using SlotImage = std::array<std::uint8_t, Block>;
using EepromBytes = std::array<std::uint8_t, Size>;

[[nodiscard]] constexpr std::uint32_t readBe32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24U) | (static_cast<std::uint32_t>(p[1]) << 16U) |
           (static_cast<std::uint32_t>(p[2]) << 8U) | static_cast<std::uint32_t>(p[3]);
}

constexpr void writeBe32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 24U);
    p[1] = static_cast<std::uint8_t>(v >> 16U);
    p[2] = static_cast<std::uint8_t>(v >> 8U);
    p[3] = static_cast<std::uint8_t>(v);
}

[[nodiscard]] constexpr std::uint16_t readBe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8U) | p[1]);
}

constexpr void writeBe16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8U);
    p[1] = static_cast<std::uint8_t>(v & 0xFFU);
}

[[nodiscard]] constexpr std::uint32_t slotFlags(const SlotImage& s) noexcept {
    return readBe32(s.data() + slot_field::Flags);
}
constexpr void setSlotFlags(SlotImage& s, std::uint32_t v) noexcept {
    writeBe32(s.data() + slot_field::Flags, v);
}
[[nodiscard]] constexpr std::uint8_t courseByte(const SlotImage& s, std::size_t index) noexcept {
    return index < slot_field::CourseStarCount ? s[slot_field::CourseStars + index] : std::uint8_t{0};
}
[[nodiscard]] constexpr std::uint8_t coinScore(const SlotImage& s, std::size_t course) noexcept {
    return course < slot_field::CoinScoreCount ? s[slot_field::CoinScores + course] : std::uint8_t{0};
}

// Replace the 7 star bits of a course byte while preserving the cannon bit.
constexpr void setCourseStarBits(SlotImage& s, std::size_t index, std::uint8_t starBits) noexcept {
    if (index >= slot_field::CourseStarCount) return;
    auto& byte = s[slot_field::CourseStars + index];
    byte = static_cast<std::uint8_t>((byte & kCannonBit) | (starBits & kStarBitsMask));
}

[[nodiscard]] constexpr bool secretStarSet(const SlotImage& s, const SecretStar& star) noexcept {
    if (star.storage == StarStorage::CourseByte) {
        return (courseByte(s, star.byteIndex) & (1U << star.bit)) != 0U;
    }
    return (slotFlags(s) & (1U << star.bit)) != 0U;
}

constexpr void setSecretStar(SlotImage& s, const SecretStar& star, bool on) noexcept {
    if (star.storage == StarStorage::CourseByte) {
        auto& byte = s[slot_field::CourseStars + star.byteIndex];
        const auto mask = static_cast<std::uint8_t>(1U << star.bit);
        byte = on ? static_cast<std::uint8_t>(byte | mask)
                  : static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
    } else {
        const std::uint32_t mask = 1U << star.bit;
        const std::uint32_t flags = slotFlags(s);
        setSlotFlags(s, on ? (flags | mask) : (flags & ~mask));
    }
}

struct StarSummary {
    int mainStars = 0;      // courseStars[0..14] star bits
    int secretStars = 0;    // courseStars[15..24] star bits + castle star bits
    int total = 0;          // what the game itself shows (main + secret)
    int mappedSecret = 0;   // secret stars that are one of the 15 documented ones
    int unmappedExtra = 0;  // star bits set that no retail star corresponds to
    [[nodiscard]] bool exceedsRetail() const noexcept { return total > kMaxStars || unmappedExtra > 0; }
};

[[nodiscard]] constexpr int popcount7(std::uint32_t v) noexcept {
    return std::popcount(v & 0x7FU);
}

[[nodiscard]] constexpr StarSummary summarizeStars(const SlotImage& s) noexcept {
    StarSummary out;
    for (std::size_t i = 0; i < kMainCourseCount; ++i) out.mainStars += popcount7(courseByte(s, i));
    for (std::size_t i = kMainCourseCount; i < slot_field::CourseStarCount; ++i) {
        out.secretStars += popcount7(courseByte(s, i));
    }
    out.secretStars += popcount7(slotFlags(s) >> save_flag::CastleStarShift);
    for (const SecretStar& star : kSecretStars) {
        if (secretStarSet(s, star)) ++out.mappedSecret;
    }
    out.total = out.mainStars + out.secretStars;
    out.unmappedExtra = out.secretStars - out.mappedSecret;
    return out;
}

[[nodiscard]] constexpr int courseStarCount(const SlotImage& s, std::size_t index) noexcept {
    return popcount7(courseByte(s, index));
}

}  // namespace sm64
