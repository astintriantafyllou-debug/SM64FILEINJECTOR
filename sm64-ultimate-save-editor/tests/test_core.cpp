// Engine tests. Expected values are hand-computed or transcribed independently
// from the n64decomp/sm64 sources; they are never derived from the code under test.
#include "SM64Presets.hpp"
#include "SM64SystemCore.hpp"
#include "test_framework.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace sm64;
namespace fs = std::filesystem;

namespace {

// ---- independent reference: save_file.c calc_checksum, transcribed verbatim ----
std::uint16_t referenceChecksum(const std::uint8_t* data, int size) {
    std::uint16_t chksum = 0;
    while (size-- > 2) chksum = static_cast<std::uint16_t>(chksum + *data++);
    return chksum;
}

// Builds a block the way add_save_block_signature() does, independently of signBlock().
void referenceSign(std::uint8_t* block, int size, std::uint16_t magic) {
    block[size - 4] = static_cast<std::uint8_t>(magic >> 8);
    block[size - 3] = static_cast<std::uint8_t>(magic & 0xFF);
    const std::uint16_t sum = referenceChecksum(block, size);
    block[size - 2] = static_cast<std::uint8_t>(sum >> 8);
    block[size - 1] = static_cast<std::uint8_t>(sum & 0xFF);
}

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path() / ("sm64tests_" + std::to_string(++counter) + "_" +
                                            std::to_string(reinterpret_cast<std::uintptr_t>(this) % 100000U));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeFile(const fs::path& p, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A realistic file built from the decompilation's own layout, using only
// reference helpers (no engine code): slot flags = 1, menu with 'HI' magic.
EepromBytes authenticFile() {
    EepromBytes b{};
    for (int slot = 0; slot < 4; ++slot) {
        const std::size_t p = P[static_cast<std::size_t>(slot)];
        b[p + 11] = 0x01;                       // flags = FILE_EXISTS
        b[p + 12] = 0x7F;                       // BOB: all seven stars
        b[p + 37] = static_cast<std::uint8_t>(10 + slot);  // BOB coin score
        referenceSign(b.data() + p, 56, 0x4441);
        std::copy_n(b.begin() + static_cast<std::ptrdiff_t>(p), 56,
                    b.begin() + static_cast<std::ptrdiff_t>(B[static_cast<std::size_t>(slot)]));
    }
    b[0x1C0 + 17] = 0x01;  // soundMode = 1 (headset)
    referenceSign(b.data() + 0x1C0, 32, 0x4849);
    std::copy_n(b.begin() + 0x1C0, 32, b.begin() + 0x1E0);
    return b;
}

EepromBytes swap16Bytes(EepromBytes b) {
    for (std::size_t i = 0; i < b.size(); i += 2) std::swap(b[i], b[i + 1]);
    return b;
}
EepromBytes swap32Bytes(EepromBytes b) {
    for (std::size_t i = 0; i < b.size(); i += 4) {
        std::swap(b[i], b[i + 3]);
        std::swap(b[i + 1], b[i + 2]);
    }
    return b;
}

std::size_t diffCount(const EepromBytes& a, const EepromBytes& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) n += a[i] != b[i] ? 1U : 0U;
    return n;
}

Core loadedFrom(const EepromBytes& b, bool autoRepair = false) {
    Core core;
    std::string error;
    CHECK(core.loadBytes(b, autoRepair, error));
    return core;
}

}  // namespace

// ===========================================================================
// Format facts and checksum
// ===========================================================================
TEST("layout: block offsets tile the 512-byte file exactly") {
    std::size_t cursor = 0;
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo info = blockInfo(id);
        CHECK_EQ(info.offset, cursor);
        cursor += info.size;
    }
    CHECK_EQ(cursor, std::size_t{512});
    CHECK_EQ(P[1], std::size_t{0x70});
    CHECK_EQ(B[3], std::size_t{0x188});
    CHECK_EQ(MenuPrimaryOffset, std::size_t{0x1C0});
}

TEST("signature offsets: slots DA at 52-53, menu HI at 28-29, checksum last two bytes") {
    const EepromBytes blank = makeBlankEeprom();
    for (std::size_t slot = 0; slot < 4; ++slot) {
        for (std::size_t base : {P[slot], B[slot]}) {
            CHECK_EQ(blank[base + 52], std::uint8_t{0x44});
            CHECK_EQ(blank[base + 53], std::uint8_t{0x41});
        }
    }
    CHECK_EQ(blank[0x1C0 + 28], std::uint8_t{0x48});
    CHECK_EQ(blank[0x1C0 + 29], std::uint8_t{0x49});
    CHECK_EQ(blank[0x1E0 + 28], std::uint8_t{0x48});
    CHECK_EQ(blank[0x1E0 + 29], std::uint8_t{0x49});
    // The slot signature is NOT at bytes 0-1.
    CHECK(!(blank[0] == 0x44 && blank[1] == 0x41));
}

TEST("checksum: hand-computed known vectors") {
    // Empty slot: only the magic bytes contribute: 0x44 + 0x41 = 0x85.
    SlotImage empty{};
    signBlock(empty.data(), Block, kSlotMagic);
    CHECK_EQ(empty[54], std::uint8_t{0x00});
    CHECK_EQ(empty[55], std::uint8_t{0x85});

    // flags = 1 adds 1: 0x86.
    SlotImage occupied{};
    occupied[11] = 1;
    signBlock(occupied.data(), Block, kSlotMagic);
    CHECK_EQ(occupied[54], std::uint8_t{0x00});
    CHECK_EQ(occupied[55], std::uint8_t{0x86});

    // 15 coin scores of 255 = 3825 (0x0EF1) + 0x85 = 0x0F76: checksum is big-endian.
    SlotImage coins{};
    for (std::size_t i = 0; i < 15; ++i) coins[37 + i] = 255;
    signBlock(coins.data(), Block, kSlotMagic);
    CHECK_EQ(coins[54], std::uint8_t{0x0F});
    CHECK_EQ(coins[55], std::uint8_t{0x76});

    // Blank menu block: ages 3FFFFFFF/2AAAAAAA/15555555 sum to 1656, +0x48+0x49 (145) = 1801 = 0x0709.
    const EepromBytes blank = makeBlankEeprom();
    CHECK_EQ(blank[0x1C0 + 30], std::uint8_t{0x07});
    CHECK_EQ(blank[0x1C0 + 31], std::uint8_t{0x09});
}

TEST("checksum: matches the decompilation's calc_checksum on varied data") {
    std::uint32_t state = 12345;
    for (int round = 0; round < 50; ++round) {
        std::array<std::uint8_t, 56> block{};
        for (auto& v : block) {
            state = state * 1664525U + 1013904223U;
            v = static_cast<std::uint8_t>(state >> 24);
        }
        auto expected = block;
        referenceSign(expected.data(), 56, 0x4441);
        auto actual = block;
        signBlock(actual.data(), 56, kSlotMagic);
        CHECK(expected == actual);
    }
}

TEST("checksum: bytes are summed individually (not as 16-bit words) and include the magic") {
    SlotImage a{};
    a[0] = 0xFF;
    a[1] = 0xFF;
    // Byte-sum: 0xFF + 0xFF = 0x01FE. (Summing 16-bit words would give 0xFFFF.)
    CHECK_EQ(checksumOf(a.data(), Block), std::uint16_t{0x01FE});
    SlotImage withMagic{};
    withMagic[52] = 0x44;
    withMagic[53] = 0x41;
    CHECK_EQ(checksumOf(withMagic.data(), Block), std::uint16_t{0x0085});
}

// ===========================================================================
// Blank files, round trips, authentic layout (menu magic regression)
// ===========================================================================
TEST("new blank EEPROM validates and is 512 bytes") {
    Core core;
    CHECK_EQ(core.view().size(), std::size_t{512});
    const Report report = core.validate();
    CHECK(report.valid);
    for (const PairReport& pair : report.pairs) CHECK(pair.status == PairStatus::BothValidIdentical);
    CHECK(!core.dirty());
}

TEST("regression: an authentic game-layout file (menu magic 'HI') validates") {
    // v1.0 checked the menu blocks for 'DA' and reported authentic files as invalid.
    const EepromBytes file = authenticFile();
    const Report report = validateBytes(file);
    CHECK(report.valid);
    CHECK(report.menu[0].magic);
    CHECK(report.menu[1].magic);
}

TEST("regression: saving never rewrites the menu signature") {
    TempDir dir;
    const EepromBytes file = authenticFile();
    Core core = loadedFrom(file);
    std::string error;
    CHECK(core.save(dir.path / "a.eep", error));
    const auto written = readFile(dir.path / "a.eep");
    CHECK_EQ(written.size(), std::size_t{512});
    CHECK(std::equal(written.begin(), written.end(), file.begin()));
    CHECK_EQ(written[0x1C0 + 28], std::uint8_t{0x48});
}

TEST("regression: save does not silently rebuild checksums of a raw-edited block") {
    TempDir dir;
    Core core = loadedFrom(authenticFile());
    CHECK(core.set(12, 0x00));  // raw edit of slot A BOB stars, checksum now stale
    CHECK(!core.validate().slots[0].valid());
    std::string error;
    CHECK(core.save(dir.path / "raw.eep", error));
    const auto written = readFile(dir.path / "raw.eep");
    CHECK_EQ(written[12], std::uint8_t{0x00});
    Core reloaded;
    CHECK(reloaded.load(dir.path / "raw.eep", false, error));
    CHECK(!reloaded.validate().slots[0].valid());  // still visibly invalid
}

TEST("save/load round trip preserves all 512 bytes") {
    TempDir dir;
    EepromBytes file = authenticFile();
    Core core = loadedFrom(file);
    std::string error;
    CHECK(core.save(dir.path / "rt.eep", error));
    Core other;
    CHECK(other.load(dir.path / "rt.eep", false, error));
    CHECK(other.view() == file);
    CHECK(other.detectedOrder() == ByteOrder::Native);
}

// ===========================================================================
// File size and byte order
// ===========================================================================
TEST("incorrect file sizes are rejected") {
    TempDir dir;
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{511}, std::size_t{513}, std::size_t{1024}}) {
        writeFile(dir.path / "bad.bin", std::vector<std::uint8_t>(size, 0));
        Core core;
        const EepromBytes before = core.view();
        std::string error;
        CHECK(!core.load(dir.path / "bad.bin", false, error));
        CHECK(!error.empty());
        CHECK(core.view() == before);  // failed load leaves the engine untouched
    }
    Core core;
    std::string error;
    CHECK(!core.load(dir.path / "does-not-exist.eep", false, error));
}

TEST("16-bit swapped input is detected and converted") {
    const EepromBytes canonical = authenticFile();
    Core core = loadedFrom(swap16Bytes(canonical));
    CHECK(core.detectedOrder() == ByteOrder::Swapped16);
    CHECK(core.view() == canonical);
}

TEST("32-bit swapped input is detected and converted") {
    const EepromBytes canonical = authenticFile();
    Core core = loadedFrom(swap32Bytes(canonical));
    CHECK(core.detectedOrder() == ByteOrder::Swapped32);
    CHECK(core.view() == canonical);
}

TEST("saving a swapped input writes canonical byte order") {
    TempDir dir;
    const EepromBytes canonical = authenticFile();
    Core core = loadedFrom(swap16Bytes(canonical));
    std::string error;
    CHECK(core.save(dir.path / "canon.eep", error));
    const auto written = readFile(dir.path / "canon.eep");
    CHECK(std::equal(written.begin(), written.end(), canonical.begin()));
}

// ===========================================================================
// Structured editing
// ===========================================================================
TEST("structured edit changes only the intended bytes (plus checksums)") {
    Core core = loadedFrom(authenticFile());
    const EepromBytes before = core.view();
    CHECK(core.setFlag(0, save_flag::HaveWingCap, true));
    const EepromBytes& after = core.view();
    for (std::size_t i = 0; i < Size; ++i) {
        const bool expected = i == P[0] + 11 || i == B[0] + 11 ||      // flags low byte
                              i == P[0] + 55 || i == B[0] + 55 ||      // checksum low byte
                              i == P[0] + 54 || i == B[0] + 54;        // checksum high byte (may be unchanged)
        if (!expected) CHECK_EQ(after[i], before[i]);
    }
    CHECK_EQ(after[P[0] + 11], std::uint8_t{0x03});
    CHECK(core.validate().valid);
}

TEST("structured edits update primary and backup identically") {
    Core core = loadedFrom(authenticFile());
    CHECK(core.setCoinScore(2, 5, 77));
    CHECK_EQ(core.view()[P[2] + 37 + 5], std::uint8_t{77});
    CHECK_EQ(core.view()[B[2] + 37 + 5], std::uint8_t{77});
    CHECK(std::equal(core.view().begin() + static_cast<std::ptrdiff_t>(P[2]),
                     core.view().begin() + static_cast<std::ptrdiff_t>(P[2] + 56),
                     core.view().begin() + static_cast<std::ptrdiff_t>(B[2])));
    CHECK(core.validate().valid);
}

TEST("raw mirrored edit writes both copies without touching checksums") {
    Core core = loadedFrom(authenticFile());
    CHECK(core.setMirrored(1, 30, 0xAB));
    CHECK_EQ(core.view()[P[1] + 30], std::uint8_t{0xAB});
    CHECK_EQ(core.view()[B[1] + 30], std::uint8_t{0xAB});
    CHECK(!core.validate().slots[2].valid());  // stale checksum is visible
}

TEST("coin scores clamp to 0..255") {
    Core core;
    CHECK(core.setCoinScore(0, 0, 999));
    CHECK_EQ(core.view()[P[0] + 37], std::uint8_t{255});
    CHECK(core.setCoinScore(0, 0, -5));
    CHECK_EQ(core.view()[P[0] + 37], std::uint8_t{0});
    CHECK(core.setCoinScore(0, 14, 255));
    CHECK_EQ(core.view()[P[0] + 37 + 14], std::uint8_t{255});
    CHECK(!core.setCoinScore(0, 15, 10));  // only 15 courses have coin scores
}

TEST("every main-course star maps to exactly the intended bit") {
    for (int slot = 0; slot < 4; ++slot) {
        for (std::size_t course = 0; course < 15; ++course) {
            for (int star = 0; star < 7; ++star) {
                Core core;
                const EepromBytes before = core.view();
                CHECK(core.setCourseStar(slot, course, star, true));
                const auto s = static_cast<std::size_t>(slot);
                // Expected: byte 12+course gains bit `star`, in both copies.
                CHECK_EQ(core.view()[P[s] + 12 + course], static_cast<std::uint8_t>(1U << star));
                CHECK_EQ(core.view()[B[s] + 12 + course], static_cast<std::uint8_t>(1U << star));
                // No other byte except the two checksum bytes of each copy changed.
                std::size_t changed = diffCount(before, core.view());
                CHECK(changed <= 6);  // star byte x2, checksum (up to 2 bytes) x2
                CHECK(core.validate().valid);
                CHECK_EQ(core.stars(slot).mainStars, 1);
                CHECK_EQ(core.stars(slot).total, 1);
            }
        }
    }
}

TEST("every secret star maps to its verified byte/bit (independent table)") {
    struct Expected { std::size_t index; bool castle; std::size_t byteIndex; unsigned bit; };
    // Written by hand from course_defines.h / save_file.h, not read from kSecretStars.
    const std::array<Expected, 15> expected{{
        {0, false, 15, 0}, {1, false, 16, 0}, {2, false, 17, 0},   // BITDW, BITFS, BITS
        {3, false, 18, 0}, {4, false, 18, 1},                      // PSS stars 1 and 2
        {5, false, 19, 0}, {6, false, 20, 0}, {7, false, 21, 0},   // COTMC, TOTWC, VCUTM
        {8, false, 22, 0}, {9, false, 23, 0},                      // WMOTR, SA
        {10, true, 0, 24}, {11, true, 0, 25}, {12, true, 0, 26},   // Toad stars 1-3
        {13, true, 0, 27}, {14, true, 0, 28},                      // MIPS stars 1-2
    }};
    for (const Expected& e : expected) {
        Core core;
        CHECK(core.setSecretStarFlag(0, e.index, true));
        const SlotImage image = core.slotImage(0);
        if (e.castle) {
            CHECK_EQ(slotFlags(image), std::uint32_t{1} << e.bit);
            for (std::size_t i = 0; i < 25; ++i) CHECK_EQ(courseByte(image, i), std::uint8_t{0});
        } else {
            CHECK_EQ(courseByte(image, e.byteIndex), static_cast<std::uint8_t>(1U << e.bit));
            CHECK_EQ(slotFlags(image), std::uint32_t{0});
        }
        CHECK_EQ(core.stars(0).total, 1);
        CHECK_EQ(core.stars(0).secretStars, 1);
        CHECK_EQ(core.stars(0).mappedSecret, 1);
        CHECK(core.validate().valid);
        CHECK(core.setSecretStarFlag(0, e.index, false));
        CHECK_EQ(core.stars(0).total, 0);
    }
    CHECK(!Core().setSecretStarFlag(0, 15, true));
}

TEST("star totals follow the game: 7-bit popcount, cannon bit excluded, castle bits included") {
    SlotImage image{};
    image[slot_field::CourseStars + 0] = 0xFF;   // bit 7 is the cannon flag, not a star
    image[slot_field::CourseStars + 3] = 0b0101;  // two stars
    image[slot_field::CourseStars + 24] = 0x01;   // unused byte still counts in the game's total
    setSlotFlags(image, (0b11U << 24) | save_flag::FileExists);
    const StarSummary s = summarizeStars(image);
    CHECK_EQ(s.mainStars, 9);
    CHECK_EQ(s.secretStars, 3);
    CHECK_EQ(s.total, 12);
    CHECK_EQ(s.mappedSecret, 2);   // the two castle stars; byte 24 is unmapped
    CHECK_EQ(s.unmappedExtra, 1);
    CHECK(s.exceedsRetail());
}

TEST("course star edits preserve the cannon bit") {
    Core core;
    CHECK(core.setMirrored(0, 12, 0x80));  // cannon flag set via raw edit
    CHECK(core.setCourseStars(0, 0, 0x7F));
    CHECK_EQ(core.slotImage(0)[12], std::uint8_t{0xFF});
    CHECK(core.setCourseStars(0, 0, 0));
    CHECK_EQ(core.slotImage(0)[12], std::uint8_t{0x80});
}

// ===========================================================================
// Validation and repair
// ===========================================================================
TEST("one corrupted primary repairs from the valid backup") {
    EepromBytes file = authenticFile();
    file[P[1] + 20] ^= 0x55;
    Core core = loadedFrom(file);
    CHECK(core.validate().pairs[1].status == PairStatus::OnlyBackupValid);
    const auto actions = core.repair();
    CHECK_EQ(actions.size(), std::size_t{1});
    CHECK(core.validate().valid);
    CHECK(core.view() == authenticFile());
}

TEST("one corrupted backup repairs from the valid primary") {
    EepromBytes file = authenticFile();
    file[B[3] + 40] ^= 0x01;
    Core core = loadedFrom(file);
    CHECK(core.validate().pairs[3].status == PairStatus::OnlyPrimaryValid);
    CHECK_EQ(core.repair().size(), std::size_t{1});
    CHECK(core.view() == authenticFile());
}

TEST("menu block corruption is repaired from the other copy") {
    EepromBytes file = authenticFile();
    file[0x1C0 + 3] ^= 0xFF;
    Core core = loadedFrom(file);
    CHECK(core.validate().pairs[4].status == PairStatus::OnlyBackupValid);
    CHECK_EQ(core.repair().size(), std::size_t{1});
    CHECK(core.view() == authenticFile());
}

TEST("two valid but different copies are detected and never auto-resolved") {
    EepromBytes file = authenticFile();
    // Make slot C's backup a different but properly signed save.
    file[B[2] + 12] = 0x01;
    referenceSign(file.data() + B[2], 56, 0x4441);
    Core core = loadedFrom(file, /*autoRepair=*/true);
    const Report report = core.validate();
    CHECK(report.pairs[2].status == PairStatus::BothValidDifferent);
    CHECK(!report.pairs[2].recoverable);
    CHECK(!report.pairs[2].differingOffsets.empty());
    CHECK(core.planRepair().empty());
    CHECK(core.repair().empty());
    CHECK(core.view() == file);  // auto-repair on load did not pick a side

    std::string error;
    CHECK(core.resolvePair(2, /*usePrimary=*/false, error));  // user chooses the backup
    CHECK_EQ(core.view()[P[2] + 12], std::uint8_t{0x01});
    CHECK(core.validate().valid);
}

TEST("two invalid copies are not falsely reported as repaired") {
    EepromBytes file = authenticFile();
    file[P[0] + 5] ^= 0x10;
    file[B[0] + 6] ^= 0x20;
    Core core = loadedFrom(file, true);
    const Report report = core.validate();
    CHECK(report.pairs[0].status == PairStatus::BothInvalid);
    CHECK(!report.pairs[0].recoverable);
    CHECK(core.repair().empty());
    CHECK(!core.validate().valid);
    std::string error;
    CHECK(!core.resolvePair(0, true, error));  // cannot use an invalid copy as the source
    CHECK(!error.empty());
    // Rebuilding the checksum is an explicit action and only fixes the signature.
    CHECK(core.rebuildChecksum(0));
    CHECK(core.validate().slots[0].valid());
}

TEST("auto-repair on load restores single-copy damage but leaves history undoable") {
    EepromBytes file = authenticFile();
    file[P[2] + 1] ^= 0x01;
    Core core = loadedFrom(file, true);
    CHECK(core.validate().valid);
    CHECK(core.dirty());  // repaired content differs from the file on disk
    CHECK(core.undo());
    CHECK(core.view() == file);
}

// ===========================================================================
// Presets
// ===========================================================================
TEST("every preset applied to a suitable valid file yields a valid file and one history entry") {
    for (const PresetInfo& info : presetCatalog()) {
        // Start from an authentic file; tailor it so that each preset has real work to do.
        EepromBytes file = authenticFile();
        if (info.id == PresetId::RepairAllRecoverable) file[B[0] + 9] ^= 0x01;  // one bad copy
        if (info.id == PresetId::SyncPrimaryToBackup || info.id == PresetId::RestoreFromBackup) {
            // Backup is valid but different (coin score changed and re-signed).
            file[B[0] + 37] = 99;
            referenceSign(file.data() + B[0], 56, 0x4441);
        }
        Core core = loadedFrom(file);

        PresetRequest request;
        request.id = info.id;
        request.slot = 0;
        request.destSlot = 1;
        request.seed = 42;
        const std::size_t historyBefore = core.undoStack().size();
        std::string error;
        CHECK(applyPreset(core, request, error));
        CHECK_EQ(core.undoStack().size(), historyBefore + 1);
        CHECK(core.validate().valid);
        CHECK(core.undo());
        CHECK_EQ(core.undoStack().size(), historyBefore);
        CHECK(core.view() == file);  // undo restores the exact prior bytes
    }
}

TEST("presets that would change nothing say so instead of pretending") {
    Core core = loadedFrom(authenticFile());  // primaries and backups already identical
    std::string error;
    PresetRequest request;
    request.slot = 0;
    request.id = PresetId::SyncPrimaryToBackup;
    CHECK(!applyPreset(core, request, error));
    CHECK(!error.empty());
    request.id = PresetId::RestoreFromBackup;
    CHECK(!applyPreset(core, request, error));
    request.id = PresetId::RepairAllRecoverable;
    CHECK(!applyPreset(core, request, error));  // nothing recoverable
    CHECK(core.undoStack().empty());
}

TEST("sync and restore presets copy in the documented direction") {
    EepromBytes file = authenticFile();
    file[B[1] + 37] = 99;
    referenceSign(file.data() + B[1], 56, 0x4441);
    std::string error;
    PresetRequest request;
    request.slot = 1;

    Core sync = loadedFrom(file);
    request.id = PresetId::SyncPrimaryToBackup;
    CHECK(applyPreset(sync, request, error));
    CHECK_EQ(sync.view()[B[1] + 37], std::uint8_t{11});  // backup now equals primary (BOB coins = 10+slot)

    Core restore = loadedFrom(file);
    request.id = PresetId::RestoreFromBackup;
    CHECK(applyPreset(restore, request, error));
    CHECK_EQ(restore.view()[P[1] + 37], std::uint8_t{99});  // primary now equals backup
}

TEST("120-star preset produces exactly 120 stars and 70-star preset exactly 70") {
    Core core;
    std::string error;
    PresetRequest request;
    request.id = PresetId::Complete120Stars;
    request.slot = 2;
    CHECK(applyPreset(core, request, error));
    CHECK_EQ(core.stars(2).total, 120);
    CHECK_EQ(core.stars(2).mainStars, 105);
    CHECK_EQ(core.stars(2).secretStars, 15);
    CHECK_EQ(core.stars(2).mappedSecret, 15);
    CHECK_EQ(core.stars(2).unmappedExtra, 0);
    CHECK(!core.stars(2).exceedsRetail());
    CHECK(core.validate().valid);
    for (std::size_t c = 0; c < 15; ++c) CHECK(core.slotImage(2)[37 + c] >= 100);
    // Other slots untouched.
    CHECK_EQ(core.stars(0).total, 0);
    const std::uint32_t flags = slotFlags(core.slotImage(2));
    CHECK((flags & save_flag::FileExists) != 0U);
    CHECK((flags & (save_flag::HaveKey1 | save_flag::HaveKey2)) == 0U);

    request.id = PresetId::Progress70Stars;
    request.slot = 1;
    CHECK(applyPreset(core, request, error));
    CHECK_EQ(core.stars(1).total, 70);
    CHECK(core.validate().valid);
}

TEST("120-star preset replaces stray unmapped star bits so the total stays 120") {
    Core core;
    CHECK(core.setMirrored(0, 12 + 23, 0x7F));  // junk bits in the Secret Aquarium byte
    core.normalize();
    CHECK_EQ(core.stars(0).total, 7);
    CHECK_EQ(core.stars(0).unmappedExtra, 6);  // only bit 0 of that byte is a real star
    std::string error;
    PresetRequest request;
    request.id = PresetId::Complete120Stars;
    CHECK(applyPreset(core, request, error));
    CHECK_EQ(core.stars(0).total, 120);
}

TEST("presets: preview lists field-level old and new values and writes nothing") {
    const EepromBytes current = makeBlankEeprom();
    PresetRequest request;
    request.id = PresetId::UnlockAllCaps;
    request.slot = 3;
    const PresetPlan plan = planPreset(current, request);
    CHECK(plan.ok);
    CHECK(!plan.unchanged);
    CHECK(plan.after != current);
    bool sawWing = false;
    for (const FieldChange& change : plan.changes) {
        if (change.field == "Slot D: Wing Cap switch") {
            sawWing = true;
            CHECK_EQ(change.before, std::string("off"));
            CHECK_EQ(change.after, std::string("on"));
        }
    }
    CHECK(sawWing);
}

TEST("presets: copy slot, restore, sync and invalid parameters") {
    Core core;
    std::string error;
    PresetRequest request;
    request.id = PresetId::Complete120Stars;
    request.slot = 0;
    CHECK(applyPreset(core, request, error));

    request.id = PresetId::CopySlot;
    request.slot = 0;
    request.destSlot = 3;
    CHECK(applyPreset(core, request, error));
    CHECK_EQ(core.stars(3).total, 120);
    CHECK(core.validate().valid);

    request.destSlot = 0;  // same slot
    CHECK(!applyPreset(core, request, error));
    request.slot = 9;      // out of range
    CHECK(!applyPreset(core, request, error));

    // Restore from an invalid backup is refused.
    EepromBytes file = core.bytes();
    file[B[0] + 3] ^= 1;
    Core broken = loadedFrom(file);
    PresetRequest restore;
    restore.id = PresetId::RestoreFromBackup;
    restore.slot = 0;
    CHECK(!applyPreset(broken, restore, error));
}

TEST("presets: random challenge is deterministic for a seed") {
    PresetRequest request;
    request.id = PresetId::RandomChallenge;
    request.slot = 0;
    request.seed = 1234;
    const PresetPlan a = planPreset(makeBlankEeprom(), request);
    const PresetPlan b = planPreset(makeBlankEeprom(), request);
    request.seed = 4321;
    const PresetPlan c = planPreset(makeBlankEeprom(), request);
    CHECK(a.ok && b.ok && c.ok);
    CHECK(a.after == b.after);
    CHECK(a.after != c.after);
    CHECK(validateBytes(a.after).valid);
}

TEST("presets: new blank and occupied slot A") {
    PresetRequest request;
    request.id = PresetId::NewOccupiedSlotA;
    const PresetPlan plan = planPreset(authenticFile(), request);
    CHECK(plan.ok);
    CHECK(validateBytes(plan.after).valid);
    SlotImage slotA{};
    std::copy_n(plan.after.begin(), Block, slotA.begin());
    CHECK_EQ(slotFlags(slotA), save_flag::FileExists);
    CHECK_EQ(summarizeStars(slotA).total, 0);
    SlotImage slotB{};
    std::copy_n(plan.after.begin() + static_cast<std::ptrdiff_t>(P[1]), Block, slotB.begin());
    CHECK_EQ(slotFlags(slotB), std::uint32_t{0});
    CHECK(plan.after != makeBlankEeprom());  // occupied differs from blank

    request.id = PresetId::NewBlankEeprom;
    const PresetPlan blank = planPreset(authenticFile(), request);
    CHECK(blank.ok);
    CHECK(blank.after == makeBlankEeprom());
}

// ===========================================================================
// History
// ===========================================================================
TEST("undo restores the exact previous 512-byte state; redo restores the next") {
    Core core = loadedFrom(authenticFile());
    const EepromBytes s0 = core.bytes();
    CHECK(core.setFlag(0, save_flag::HaveMetalCap, true));
    const EepromBytes s1 = core.bytes();
    CHECK(core.setCourseStar(0, 4, 3, true));
    const EepromBytes s2 = core.bytes();

    CHECK(core.undo());
    CHECK(core.view() == s1);
    CHECK(core.undo());
    CHECK(core.view() == s0);
    CHECK(!core.undo());
    CHECK(core.redo());
    CHECK(core.view() == s1);
    CHECK(core.redo());
    CHECK(core.view() == s2);
    CHECK(!core.redo());
}

TEST("a new edit clears the redo stack; no-op edits create no history") {
    Core core;
    CHECK(core.setFlag(0, save_flag::FileExists, true));
    CHECK(core.undo());
    CHECK(core.canRedo());
    CHECK(!core.setFlag(0, save_flag::FileExists, false));  // already off: nothing to do
    CHECK(core.canRedo());                                   // redo survives a no-op
    CHECK(core.setFlag(1, save_flag::FileExists, true));
    CHECK(!core.canRedo());
    CHECK_EQ(core.undoStack().size(), std::size_t{1});
}

TEST("typing a coin score coalesces into one history entry per widget") {
    Core core;
    CHECK(core.setCoinScore(0, 2, 1));
    CHECK(core.setCoinScore(0, 2, 12));
    CHECK(core.setCoinScore(0, 2, 123));
    CHECK_EQ(core.undoStack().size(), std::size_t{1});
    CHECK(core.setCoinScore(0, 3, 5));  // a different widget: new entry
    CHECK_EQ(core.undoStack().size(), std::size_t{2});
    CHECK(core.undo());
    CHECK(core.undo());
    CHECK_EQ(core.slotImage(0)[37 + 2], std::uint8_t{0});
}

TEST("dirty tracking follows the saved image, including undo back to saved state") {
    TempDir dir;
    Core core;
    CHECK(!core.dirty());
    CHECK(core.setFlag(0, save_flag::FileExists, true));
    CHECK(core.dirty());
    CHECK(core.undo());
    CHECK(!core.dirty());
    CHECK(core.redo());
    std::string error;
    CHECK(core.save(dir.path / "d.eep", error));
    CHECK(!core.dirty());
    CHECK(core.setCoinScore(0, 0, 3));
    CHECK(core.dirty());
    CHECK(!core.pendingChanges().empty());
}

TEST("history is capped and never accesses outside the buffer") {
    Core core;
    for (int i = 0; i < 400; ++i) CHECK(core.setCourseStar(0, static_cast<std::size_t>(i % 15), i % 7, (i / 105) % 2 == 0));
    CHECK(core.undoStack().size() <= 256);
    for (std::size_t i = 0; i < 400 && core.canUndo(); ++i) CHECK(core.undo());
}

// ===========================================================================
// Bounds
// ===========================================================================
TEST("no edit API accesses bytes outside the 512-byte buffer") {
    Core core;
    const EepromBytes before = core.view();
    CHECK(!core.set(512, 1));
    CHECK(!core.set(static_cast<std::size_t>(-1), 1));
    CHECK(core.set(511, 1));
    CHECK(core.undo());
    CHECK(!core.setMirrored(-1, 0, 1));
    CHECK(!core.setMirrored(4, 0, 1));
    CHECK(!core.setMirrored(0, 56, 1));
    CHECK(!core.setFlag(7, save_flag::FileExists, true));
    CHECK(!core.setCourseStar(0, 15, 0, true));
    CHECK(!core.setCourseStar(0, 0, 7, true));
    CHECK(!core.setCourseStar(0, 0, -1, true));
    CHECK(!core.setCourseStars(0, 15, 1));
    CHECK(!core.rebuildChecksum(10));
    const std::vector<std::uint8_t> five(5, 0xAA);
    CHECK(!core.pasteBytes(510, five));
    CHECK(!core.pasteBytes(512, five));
    CHECK(!core.pasteBytes(0, std::span<const std::uint8_t>{}));
    CHECK(core.pasteBytes(507, five));
    CHECK_EQ(core.view()[511], std::uint8_t{0xAA});
    CHECK(core.undo());
    CHECK(core.view() == before);
    CHECK_EQ(core.slotImage(-1)[0], std::uint8_t{0});
    CHECK_EQ(core.slotImage(4)[0], std::uint8_t{0});
    CHECK_EQ(describeOffset(512).name, std::string("Outside the EEPROM"));
}

TEST("describeOffset names the mapped fields") {
    CHECK_EQ(describeOffset(0x000).name, std::string("Slot A primary"));
    CHECK_EQ(describeOffset(0x0A8).name, std::string("Slot B backup"));
    CHECK_EQ(describeOffset(0x1C0).name, std::string("Menu block primary"));
    CHECK(describeOffset(0x00B).field.find("Flags") != std::string::npos);
    CHECK(describeOffset(P[0] + 12).field.find("Bob-omb") != std::string::npos);
    CHECK(describeOffset(P[0] + 37 + 14).field.find("Rainbow Ride") != std::string::npos);
    CHECK(describeOffset(P[0] + 52).field.find("'DA'") != std::string::npos);
    CHECK(describeOffset(0x1C0 + 28).field.find("'HI'") != std::string::npos);
}

// ===========================================================================
// Atomic writes and backups
// ===========================================================================
TEST("atomic write: a failure before commit preserves the original file") {
    TempDir dir;
    const fs::path target = dir.path / "keep.eep";
    const std::vector<std::uint8_t> original(512, 0x5A);
    writeFile(target, original);

    Core core;
    CHECK(core.setFlag(0, save_flag::FileExists, true));
    Core::SaveOptions options;
    options.failBeforeCommit = true;
    std::string error;
    CHECK(!core.save(target, options, error));
    CHECK(!error.empty());
    CHECK(readFile(target) == original);
    CHECK(!fs::exists(target.string() + ".tmp"));
    CHECK(!fs::exists(target.string() + ".bak"));
    CHECK(core.dirty());  // a failed save does not clear the unsaved indicator
}

TEST("atomic write: an unwritable temp location leaves the original untouched") {
    TempDir dir;
    const fs::path target = dir.path / "keep2.eep";
    const std::vector<std::uint8_t> original(512, 0x33);
    writeFile(target, original);
    fs::create_directories(dir.path / "keep2.eep.tmp");  // a directory squats on the temp name
    Core core;
    std::string error;
    CHECK(!core.save(target, error));
    CHECK(readFile(target) == original);
}

TEST("atomic write: renaming onto a directory fails without data loss") {
    TempDir dir;
    fs::create_directories(dir.path / "folder.eep");
    writeFile(dir.path / "folder.eep" / "inner.bin", {1, 2, 3});
    Core core;
    std::string error;
    CHECK(!core.save(dir.path / "folder.eep", error));
    CHECK(fs::exists(dir.path / "folder.eep" / "inner.bin"));
    CHECK(!fs::exists(dir.path / "folder.eep.tmp"));
}

TEST("saving creates a .bak of the previous file and can be disabled") {
    TempDir dir;
    const fs::path target = dir.path / "game.eep";
    const std::vector<std::uint8_t> original(512, 0x11);
    writeFile(target, original);

    Core core;
    std::string error;
    CHECK(core.save(target, error));
    CHECK(fs::exists(backupPathFor(target)));
    CHECK(readFile(backupPathFor(target)) == original);
    CHECK_EQ(readFile(target).size(), std::size_t{512});
    CHECK(!fs::exists(target.string() + ".tmp"));

    const fs::path other = dir.path / "nobak.eep";
    writeFile(other, original);
    Core::SaveOptions options;
    options.makeBackup = false;
    CHECK(core.save(other, options, error));
    CHECK(!fs::exists(backupPathFor(other)));

    // A first-time save (no existing file) creates no backup and no error.
    const fs::path fresh = dir.path / "fresh.eep";
    CHECK(core.save(fresh, error));
    CHECK(!fs::exists(backupPathFor(fresh)));
}

// ===========================================================================
// Exports
// ===========================================================================
TEST("EEPROM exports are exactly 512 bytes and identical to the image") {
    TempDir dir;
    Core core = loadedFrom(authenticFile());
    for (std::size_t i = 0; i < kProfileCount; ++i) {
        const auto profile = static_cast<Profile>(i);
        if (profile == Profile::SRA) continue;
        std::string error;
        const fs::path file = dir.path / ("export" + std::to_string(i) + profileInfo(profile).extension);
        CHECK(core.exportFile(file, profile, error));
        const auto bytes = readFile(file);
        CHECK_EQ(bytes.size(), std::size_t{512});
        CHECK(std::equal(bytes.begin(), bytes.end(), core.view().begin()));
        CHECK_EQ(profileInfo(profile).outputSize, std::size_t{512});
    }
}

TEST("export profiles describe themselves and never invent transforms") {
    for (std::size_t i = 0; i < kProfileCount; ++i) {
        const ProfileInfo& info = profileInfo(static_cast<Profile>(i));
        CHECK(info.id == static_cast<Profile>(i));
        CHECK(info.name != nullptr && info.extension != nullptr && info.note != nullptr);
        CHECK(info.extension[0] == '.');
    }
    CHECK(profileInfo(Profile::Raw).verified);
    CHECK(!profileInfo(Profile::EverDriveV2).verified);
    CHECK(!profileInfo(Profile::SRA).verified);
}

TEST("SRA wrapper is exactly 32768 bytes: 16-bit swapped EEPROM then 0xFF padding") {
    const EepromBytes image = authenticFile();
    const auto out = renderExport(image, Profile::SRA);
    CHECK_EQ(out.size(), std::size_t{32768});
    CHECK_EQ(profileInfo(Profile::SRA).outputSize, std::size_t{32768});
    for (std::size_t i = 0; i < 512; i += 2) {
        CHECK_EQ(out[i], image[i + 1]);
        CHECK_EQ(out[i + 1], image[i]);
    }
    for (std::size_t i = 512; i < out.size(); ++i) CHECK_EQ(out[i], std::uint8_t{0xFF});
}

TEST("exporting does not modify the loaded data") {
    TempDir dir;
    EepromBytes broken = authenticFile();
    broken[12] ^= 1;  // invalid checksum on purpose
    Core core = loadedFrom(broken);
    std::string error;
    CHECK(core.exportFile(dir.path / "x.eep", Profile::Raw, error));
    CHECK(core.view() == broken);
    CHECK(readFile(dir.path / "x.eep")[12] == broken[12]);
}

// ===========================================================================
// GameShark
// ===========================================================================
TEST("GameShark 8-bit writes have the correct addresses and values") {
    Core core;
    CHECK(core.setMirrored(0, 11, 0x03));  // changes file offsets 0x00B and 0x043
    GameSharkOptions options;
    options.base = 0x80207700U;
    options.use16Bit = false;
    options.includeAll = false;
    options.comments = false;
    const GameSharkResult result = core.gameSharkCodes(options);
    CHECK(result.ok);
    CHECK_EQ(result.codes.size(), std::size_t{2});
    CHECK_EQ(result.codes[0], std::string("8020770B 0003"));
    CHECK_EQ(result.codes[1], std::string("80207743 0003"));
    CHECK_EQ(result.byte8Count, std::size_t{2});
    CHECK_EQ(result.byte16Count, std::size_t{0});
    CHECK_EQ(result.codeOffsets[0], std::size_t{0x00B});
}

TEST("GameShark 16-bit writes have the correct addresses and values") {
    Core core;
    CHECK(core.set(0x08, 0x12));
    CHECK(core.set(0x09, 0x34));
    CHECK(core.set(0x0B, 0x56));  // odd offset, lone byte: must fall back to an 8-bit code
    GameSharkOptions options;
    options.base = 0x80207700U;
    options.use16Bit = true;
    options.comments = false;
    const GameSharkResult result = core.gameSharkCodes(options);
    CHECK(result.ok);
    CHECK_EQ(result.codes.size(), std::size_t{2});
    CHECK_EQ(result.codes[0], std::string("81207708 1234"));
    CHECK_EQ(result.codes[1], std::string("8020770B 0056"));
    CHECK_EQ(result.byte16Count, std::size_t{1});
    CHECK_EQ(result.byte8Count, std::size_t{1});
    CHECK_EQ(result.firstOffset, std::size_t{8});
}

TEST("GameShark full-buffer generation covers every byte") {
    Core core;
    GameSharkOptions options;
    options.base = 0x80000000U;
    options.use16Bit = false;
    options.includeAll = true;
    options.comments = false;
    CHECK_EQ(core.gameSharkCodes(options).codes.size(), std::size_t{512});
    options.use16Bit = true;
    const GameSharkResult words = core.gameSharkCodes(options);
    CHECK_EQ(words.codes.size(), std::size_t{256});
    CHECK_EQ(words.codes.front().substr(0, 2), std::string("81"));
    // Last word is offsets 510-511 = menu-backup checksum 0x0709 (hand-computed above), at 0x1FE.
    CHECK_EQ(words.codes.back(), std::string("810001FE 0709"));
}

TEST("GameShark rejects invalid RDRAM ranges and misaligned 16-bit bases") {
    Core core;
    GameSharkOptions options;
    for (std::uint32_t bad : {0x00000000U, 0x7FFFFFFFU, 0x80800000U, 0x807FFE01U, 0xFFFFFFFFU, 0x807FFFFFU}) {
        options.base = bad;
        options.use16Bit = false;
        CHECK(!core.gameSharkCodes(options).ok);
        CHECK(!core.gameSharkCodes(options).error.empty());
    }
    options.base = 0x807FFE00U;  // highest base where all 512 bytes still fit
    CHECK(core.gameSharkCodes(options).ok);
    options.base = 0x80000000U;
    CHECK(core.gameSharkCodes(options).ok);
    options.base = 0x80207701U;
    options.use16Bit = true;
    CHECK(!core.gameSharkCodes(options).ok);
    options.use16Bit = false;
    CHECK(core.gameSharkCodes(options).ok);
    // v1.0 wrapper still returns a comment line for invalid input.
    CHECK(core.gameShark(0x10U, false, false).rfind("# ", 0) == 0);
}

TEST("GameShark with no changes produces no codes") {
    Core core;
    GameSharkOptions options;
    const GameSharkResult result = core.gameSharkCodes(options);
    CHECK(result.ok);
    CHECK(result.codes.empty());
}

// ===========================================================================
// Describing changes
// ===========================================================================
TEST("describeChanges names flags, courses, coins and secret stars") {
    Core core;
    const EepromBytes before = core.bytes();
    CHECK(core.setFlag(1, save_flag::MoatDrained, true));
    CHECK(core.setCoinScore(1, 3, 44));
    CHECK(core.setCourseStars(1, 0, 0x07));
    CHECK(core.setSecretStarFlag(1, 12, true));  // Toad star 3
    const auto changes = describeChanges(before, core.view());
    auto has = [&](const std::string& field) {
        return std::any_of(changes.begin(), changes.end(),
                           [&](const FieldChange& c) { return c.field == field; });
    };
    CHECK(has("Slot B: Castle moat drained"));
    CHECK(has("Slot B: Cool, Cool Mountain coin score"));
    CHECK(has("Slot B: Bob-omb Battlefield stars"));
    CHECK(has("Slot B: Toad star 3"));
}
