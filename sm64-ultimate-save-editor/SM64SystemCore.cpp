#include "SM64SystemCore.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace sm64 {
namespace {

std::string slotLetter(int slot) {
    return std::string(1U, static_cast<char>('A' + slot));
}

std::string hex(std::size_t value, int width) {
    char text[16]{};
    std::snprintf(text, sizeof(text), "%0*zX", width, value);
    return text;
}

std::string blockTitle(std::size_t id) {
    const BlockInfo info = blockInfo(id);
    if (info.isMenu) return info.isBackup ? "menu backup" : "menu primary";
    return "slot " + slotLetter(info.slot) + (info.isBackup ? " backup" : " primary");
}

std::string pairTitle(std::size_t pair) {
    return pair < kSlotCount ? "slot " + slotLetter(static_cast<int>(pair)) : "menu block";
}

std::size_t primaryId(std::size_t pair) {
    return pair < kSlotCount ? pair * 2U : 8U;
}
std::size_t backupId(std::size_t pair) {
    return pair < kSlotCount ? pair * 2U + 1U : 9U;
}

EepromBytes swap16(EepromBytes bytes) noexcept {
    for (std::size_t i = 0; i + 1U < bytes.size(); i += 2U) std::swap(bytes[i], bytes[i + 1U]);
    return bytes;
}

EepromBytes swap32(EepromBytes bytes) noexcept {
    for (std::size_t i = 0; i + 3U < bytes.size(); i += 4U) {
        std::swap(bytes[i], bytes[i + 3U]);
        std::swap(bytes[i + 1U], bytes[i + 2U]);
    }
    return bytes;
}

int validBlockCount(const EepromBytes& bytes) noexcept {
    int count = 0;
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo info = blockInfo(id);
        if (inspectBlock(bytes.data() + info.offset, info.size, info.magic).valid()) ++count;
    }
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// Block signatures
// ---------------------------------------------------------------------------
std::uint16_t checksumOf(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size < 2U) return 0U;
    std::uint16_t result = 0U;
    for (std::size_t i = 0; i < size - 2U; ++i) {
        result = static_cast<std::uint16_t>(result + data[i]);
    }
    return result;
}

void signBlock(std::uint8_t* data, std::size_t size, std::uint16_t magic) noexcept {
    if (data == nullptr || size < 4U) return;
    writeBe16(data + size - 4U, magic);
    writeBe16(data + size - 2U, checksumOf(data, size));
}

BlockState inspectBlock(const std::uint8_t* data, std::size_t size, std::uint16_t magic) noexcept {
    BlockState state{};
    if (data == nullptr || size < 4U) return state;
    state.storedMagic = readBe16(data + size - 4U);
    state.stored = readBe16(data + size - 2U);
    state.computed = checksumOf(data, size);
    state.magic = state.storedMagic == magic;
    state.checksum = state.stored == state.computed;
    return state;
}

EepromBytes makeBlankEeprom() {
    EepromBytes bytes{};
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        signBlock(bytes.data() + P[slot], Block, kSlotMagic);
        signBlock(bytes.data() + B[slot], Block, kSlotMagic);
    }
    // wipe_main_menu_data(): coin-score ages 3,2,1,0 for the four files.
    writeBe32(bytes.data() + MenuPrimaryOffset + 0U, 0x3FFFFFFFU);
    writeBe32(bytes.data() + MenuPrimaryOffset + 4U, 0x2AAAAAAAU);
    writeBe32(bytes.data() + MenuPrimaryOffset + 8U, 0x15555555U);
    signBlock(bytes.data() + MenuPrimaryOffset, kMenuBlockSize, kMenuMagic);
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(MenuPrimaryOffset), kMenuBlockSize,
                bytes.begin() + static_cast<std::ptrdiff_t>(MenuBackupOffset));
    return bytes;
}

const char* byteOrderName(ByteOrder order) noexcept {
    switch (order) {
        case ByteOrder::Native:
            return "Canonical (big-endian, as the game stores it)";
        case ByteOrder::Swapped16:
            return "16-bit byte-swapped (converted on load)";
        case ByteOrder::Swapped32:
            return "32-bit byte-swapped (converted on load)";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------
Report validateBytes(const EepromBytes& bytes) {
    Report report{};
    report.valid = true;

    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo info = blockInfo(id);
        const BlockState state = inspectBlock(bytes.data() + info.offset, info.size, info.magic);
        if (id < 8U)
            report.slots[id] = state;
        else
            report.menu[id - 8U] = state;
        if (!state.valid()) {
            report.valid = false;
            report.notes.push_back(blockTitle(id) + ": " + (!state.magic ? "signature is not " : "checksum mismatch") +
                                   (!state.magic ? ("0x" + hex(info.magic, 4)) : ""));
        }
    }

    for (std::size_t pair = 0; pair < kPairCount; ++pair) {
        const BlockInfo p = blockInfo(primaryId(pair));
        const BlockInfo b = blockInfo(backupId(pair));
        const bool pv = report.block(primaryId(pair)).valid();
        const bool bv = report.block(backupId(pair)).valid();
        PairReport& out = report.pairs[pair];

        for (std::size_t i = 0; i < p.size; ++i) {
            if (bytes[p.offset + i] != bytes[b.offset + i]) out.differingOffsets.push_back(i);
        }
        const bool identical = out.differingOffsets.empty();
        const std::string title = pairTitle(pair);

        if (pv && bv && identical) {
            out.status = PairStatus::BothValidIdentical;
            out.text = "Primary and backup are valid and identical.";
        } else if (pv && bv) {
            out.status = PairStatus::BothValidDifferent;
            out.text = "Both copies are valid but differ in " + std::to_string(out.differingOffsets.size()) +
                       " byte(s). This editor cannot tell which is newer, so you must choose. "
                       "(The game itself reads the primary copy.)";
        } else if (pv) {
            out.status = PairStatus::OnlyPrimaryValid;
            out.recoverable = true;
            out.text = "The backup copy is invalid. It can be restored from the valid primary copy.";
        } else if (bv) {
            out.status = PairStatus::OnlyBackupValid;
            out.recoverable = true;
            out.text = "The primary copy is invalid. It can be restored from the valid backup copy.";
        } else {
            out.status = PairStatus::BothInvalid;
            out.text =
                "Neither copy is valid, so there is nothing trustworthy to restore from. "
                "On boot the game would erase this " +
                title + ".";
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// Core: construction, load, save
// ---------------------------------------------------------------------------
Core::Core() {
    data_ = makeBlankEeprom();
    original_ = data_;
    saved_ = data_;
}

void Core::adopt(const Bytes& bytes, ByteOrder order) {
    data_ = bytes;
    original_ = bytes;
    saved_ = bytes;
    order_ = order;
    undo_.clear();
    redo_.clear();
    coalesceOpen_ = false;
}

bool Core::loadBytes(std::span<const std::uint8_t> input, bool autoRepair, std::string& error) {
    if (input.size() != Size) {
        error = "Input must be exactly 512 bytes (got " + std::to_string(input.size()) + ").";
        return false;
    }

    Bytes incoming{};
    std::copy(input.begin(), input.end(), incoming.begin());

    Bytes best = incoming;
    ByteOrder order = ByteOrder::Native;
    int bestScore = validBlockCount(best);

    const Bytes s16 = swap16(incoming);
    const int score16 = validBlockCount(s16);
    if (score16 > bestScore) {
        best = s16;
        bestScore = score16;
        order = ByteOrder::Swapped16;
    }
    const Bytes s32 = swap32(incoming);
    if (validBlockCount(s32) > bestScore) {
        best = s32;
        order = ByteOrder::Swapped32;
    }

    adopt(best, order);
    lastAction_ = std::string("Loaded file (") + byteOrderName(order) + ")";
    error.clear();

    if (autoRepair) {
        const auto actions = repair();
        if (!actions.empty())
            lastAction_ = "Loaded file and auto-repaired " + std::to_string(actions.size()) + " block pair(s)";
    }
    return true;
}

bool Core::load(const std::filesystem::path& path, bool autoRepair, std::string& error) {
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (!ec && fileSize != Size) {
        error = "This file is " + std::to_string(fileSize) + " bytes. A retail SM64 EEPROM save is exactly 512 bytes.";
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Cannot open the selected file.";
        return false;
    }
    std::vector<std::uint8_t> buffer((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return loadBytes(buffer, autoRepair, error);
}

bool Core::save(const std::filesystem::path& path, std::string& error) {
    return save(path, SaveOptions{}, error);
}

bool Core::save(const std::filesystem::path& path, const SaveOptions& options, std::string& error) {
    AtomicWriteOptions write{};
    write.makeBackup = options.makeBackup;
    write.failBeforeCommit = options.failBeforeCommit;
    if (!atomicWriteFile(path, data_, write, error)) return false;

    // Post-save verification: read the file back and compare every byte.
    std::ifstream verify(path, std::ios::binary);
    std::vector<std::uint8_t> readBack((std::istreambuf_iterator<char>(verify)), std::istreambuf_iterator<char>());
    if (readBack.size() != Size || !std::equal(readBack.begin(), readBack.end(), data_.begin())) {
        error = "The file was written but did not read back identically. Do not rely on it.";
        return false;
    }

    markSaved();
    lastAction_ = "Saved " + path.filename().string();
    error.clear();
    return true;
}

bool Core::exportFile(const std::filesystem::path& path, Profile profile, std::string& error) const {
    const std::vector<std::uint8_t> output = renderExport(data_, profile);
    AtomicWriteOptions write{};
    return atomicWriteFile(path, output, write, error);
}

void Core::newBlank() {
    applyEdit("New blank EEPROM", makeBlankEeprom());
}

void Core::markSaved() {
    saved_ = data_;
    coalesceOpen_ = false;
}

// ---------------------------------------------------------------------------
// Read helpers
// ---------------------------------------------------------------------------
SlotImage Core::slotImage(int slot) const {
    SlotImage image{};
    if (slot < 0 || slot >= static_cast<int>(kSlotCount)) return image;
    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(P[static_cast<std::size_t>(slot)]), Block, image.begin());
    return image;
}

StarSummary Core::stars(int slot) const {
    return summarizeStars(slotImage(slot));
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------
bool Core::applyEdit(const std::string& description, const Bytes& after, const std::string& coalesceKey) {
    if (after == data_) return false;

    if (!coalesceKey.empty() && coalesceOpen_ && !undo_.empty() && undo_.back().coalesceKey == coalesceKey) {
        HistoryEntry& last = undo_.back();
        last.after = after;
        data_ = after;
        redo_.clear();
        if (last.after == last.before) {
            undo_.pop_back();
            coalesceOpen_ = false;
        }
        lastAction_ = description;
        return true;
    }

    undo_.push_back(HistoryEntry{description, data_, after, coalesceKey});
    if (undo_.size() > kMaxHistory) undo_.erase(undo_.begin());
    redo_.clear();
    data_ = after;
    coalesceOpen_ = !coalesceKey.empty();
    lastAction_ = description;
    return true;
}

bool Core::undo() {
    if (undo_.empty()) return false;
    HistoryEntry entry = std::move(undo_.back());
    undo_.pop_back();
    data_ = entry.before;
    lastAction_ = "Undo: " + entry.description;
    redo_.push_back(std::move(entry));
    coalesceOpen_ = false;
    return true;
}

bool Core::redo() {
    if (redo_.empty()) return false;
    HistoryEntry entry = std::move(redo_.back());
    redo_.pop_back();
    data_ = entry.after;
    lastAction_ = "Redo: " + entry.description;
    undo_.push_back(std::move(entry));
    coalesceOpen_ = false;
    return true;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
bool Core::set(std::size_t index, std::uint8_t value) {
    if (index >= Size) return false;
    Bytes after = data_;
    after[index] = value;
    return applyEdit("Edit byte 0x" + hex(index, 3) + ": " + hex(data_[index], 2) + " -> " + hex(value, 2), after,
                     "byte:" + std::to_string(index));
}

bool Core::setMirrored(int slot, std::size_t relativeOffset, std::uint8_t value) {
    if (slot < 0 || slot >= static_cast<int>(kSlotCount) || relativeOffset >= Block) return false;
    const auto s = static_cast<std::size_t>(slot);
    Bytes after = data_;
    after[P[s] + relativeOffset] = value;
    after[B[s] + relativeOffset] = value;
    return applyEdit("Edit slot " + slotLetter(slot) + " byte " + std::to_string(relativeOffset) + " (both copies)",
                     after, "mirror:" + std::to_string(slot) + ":" + std::to_string(relativeOffset));
}

bool Core::pasteBytes(std::size_t offset, std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || offset >= Size || bytes.size() > Size - offset) return false;
    Bytes after = data_;
    std::copy(bytes.begin(), bytes.end(), after.begin() + static_cast<std::ptrdiff_t>(offset));
    return applyEdit("Paste " + std::to_string(bytes.size()) + " byte(s) at 0x" + hex(offset, 3), after);
}

bool Core::editSlot(int slot, const std::string& description, const std::function<void(SlotImage&)>& mutate,
                    const std::string& coalesceKey) {
    if (slot < 0 || slot >= static_cast<int>(kSlotCount) || !mutate) return false;
    const auto s = static_cast<std::size_t>(slot);

    SlotImage image = slotImage(slot);
    mutate(image);

    Bytes after = data_;
    std::copy(image.begin(), image.end(), after.begin() + static_cast<std::ptrdiff_t>(P[s]));
    std::copy(image.begin(), image.end(), after.begin() + static_cast<std::ptrdiff_t>(B[s]));
    signBlock(after.data() + P[s], Block, kSlotMagic);
    signBlock(after.data() + B[s], Block, kSlotMagic);
    return applyEdit(description, after, coalesceKey);
}

bool Core::setFlag(int slot, std::uint32_t mask, bool on) {
    std::string name = "flag 0x" + hex(mask, 8);
    for (const FlagInfo& info : kFlagTable) {
        if (info.mask == mask) {
            name = info.name;
            break;
        }
    }
    return editSlot(slot, "Slot " + slotLetter(slot) + ": " + name + (on ? " on" : " off"), [&](SlotImage& image) {
        const std::uint32_t flags = slotFlags(image);
        setSlotFlags(image, on ? (flags | mask) : (flags & ~mask));
    });
}

bool Core::setCoinScore(int slot, std::size_t course, int value) {
    if (course >= kMainCourseCount) return false;
    const int clamped = std::clamp(value, 0, 255);
    return editSlot(
        slot, "Slot " + slotLetter(slot) + ": " + Courses[course] + " coins = " + std::to_string(clamped),
        [&](SlotImage& image) { image[slot_field::CoinScores + course] = static_cast<std::uint8_t>(clamped); },
        "coin:" + std::to_string(slot) + ":" + std::to_string(course));
}

bool Core::setCourseStar(int slot, std::size_t course, int star, bool on) {
    if (course >= kMainCourseCount || star < 0 || star >= kStarsPerMainCourse) return false;
    return editSlot(slot,
                    "Slot " + slotLetter(slot) + ": " + Courses[course] + " star " + std::to_string(star + 1) +
                        (on ? " collected" : " cleared"),
                    [&](SlotImage& image) {
                        auto& byte = image[slot_field::CourseStars + course];
                        const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(star));
                        byte = on ? static_cast<std::uint8_t>(byte | mask)
                                  : static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
                    });
}

bool Core::setCourseStars(int slot, std::size_t course, std::uint8_t starBits) {
    if (course >= kMainCourseCount) return false;
    return editSlot(slot,
                    "Slot " + slotLetter(slot) + ": " + Courses[course] + (starBits == 0 ? " cleared" : " completed"),
                    [&](SlotImage& image) { setCourseStarBits(image, course, starBits); });
}

bool Core::setSecretStarFlag(int slot, std::size_t secretIndex, bool on) {
    if (secretIndex >= kSecretStarCount) return false;
    const SecretStar& star = kSecretStars[secretIndex];
    return editSlot(slot, "Slot " + slotLetter(slot) + ": " + star.name + (on ? " collected" : " cleared"),
                    [&](SlotImage& image) { setSecretStar(image, star, on); });
}

// ---------------------------------------------------------------------------
// Repair
// ---------------------------------------------------------------------------
std::vector<RepairAction> Core::planRepair() const {
    std::vector<RepairAction> plan;
    const Report report = validate();
    for (std::size_t pair = 0; pair < kPairCount; ++pair) {
        const PairStatus status = report.pairs[pair].status;
        if (status == PairStatus::OnlyPrimaryValid) {
            plan.push_back({pair, false, "Restore " + pairTitle(pair) + " backup from primary"});
        } else if (status == PairStatus::OnlyBackupValid) {
            plan.push_back({pair, true, "Restore " + pairTitle(pair) + " primary from backup"});
        }
    }
    return plan;
}

std::vector<std::string> Core::repair() {
    const std::vector<RepairAction> plan = planRepair();
    std::vector<std::string> actions;
    if (plan.empty()) return actions;

    Bytes after = data_;
    for (const RepairAction& action : plan) {
        const BlockInfo p = blockInfo(primaryId(action.pair));
        const BlockInfo b = blockInfo(backupId(action.pair));
        const BlockInfo& src = action.toPrimary ? b : p;
        const BlockInfo& dst = action.toPrimary ? p : b;
        std::copy_n(after.begin() + static_cast<std::ptrdiff_t>(src.offset), src.size,
                    after.begin() + static_cast<std::ptrdiff_t>(dst.offset));
        actions.push_back(action.description);
    }
    applyEdit("Repair " + std::to_string(plan.size()) + " block pair(s) from the valid copy", after);
    return actions;
}

bool Core::resolvePair(std::size_t pair, bool usePrimary, std::string& error) {
    if (pair >= kPairCount) {
        error = "Invalid block pair.";
        return false;
    }
    const Report report = validate();
    const bool chosenValid = report.block(usePrimary ? primaryId(pair) : backupId(pair)).valid();
    if (!chosenValid) {
        error = std::string("The ") + (usePrimary ? "primary" : "backup") +
                " copy is not valid, so it cannot be used as the source.";
        return false;
    }
    const BlockInfo p = blockInfo(primaryId(pair));
    const BlockInfo b = blockInfo(backupId(pair));
    const BlockInfo& src = usePrimary ? p : b;
    const BlockInfo& dst = usePrimary ? b : p;
    Bytes after = data_;
    std::copy_n(after.begin() + static_cast<std::ptrdiff_t>(src.offset), src.size,
                after.begin() + static_cast<std::ptrdiff_t>(dst.offset));
    error.clear();
    applyEdit(std::string("Keep ") + (usePrimary ? "primary" : "backup") + " copy of " + pairTitle(pair), after);
    return true;
}

bool Core::rebuildChecksum(std::size_t blockId) {
    if (blockId >= kBlockCount) return false;
    const BlockInfo info = blockInfo(blockId);
    Bytes after = data_;
    signBlock(after.data() + info.offset, info.size, info.magic);
    return applyEdit("Rebuild signature and checksum of " + blockTitle(blockId), after);
}

void Core::normalize() {
    Bytes after = data_;
    for (std::size_t id = 0; id < kBlockCount; ++id) {
        const BlockInfo info = blockInfo(id);
        signBlock(after.data() + info.offset, info.size, info.magic);
    }
    applyEdit("Rebuild all signatures and checksums", after);
}

// ---------------------------------------------------------------------------
// GameShark compatibility wrapper (v1.0 signature)
// ---------------------------------------------------------------------------
std::string Core::gameShark(std::uint32_t address, bool use16Bit, bool includeAll) const {
    GameSharkOptions options;
    options.base = address;
    options.use16Bit = use16Bit;
    options.includeAll = includeAll;
    const GameSharkResult result = generateGameShark(data_, original_, options);
    if (!result.ok) return "# " + result.error + "\n";
    return result.text;
}

}  // namespace sm64
