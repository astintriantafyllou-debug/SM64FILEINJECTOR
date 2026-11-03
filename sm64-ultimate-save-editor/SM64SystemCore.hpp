// SM64SystemCore.hpp
//
// The save engine: a 512-byte SM64 EEPROM image plus validation, repair,
// undo/redo history, safe file I/O, exports and GameShark generation.
//
// Public interface compatibility with v1.0 is intentional: Core::load, save,
// exportFile, bytes, byte, set, setMirrored, validate, repair, normalize and
// gameShark keep their names and argument lists. Behavioural changes are listed
// in MIGRATION.md.
#pragma once

#include "SM64SaveFormat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sm64 {

// --------------------------------------------------------------------------
// Block signatures
// --------------------------------------------------------------------------

// Retail checksum: the 16-bit sum of every byte of the block except the final
// two (the stored checksum itself). The magic bytes ARE included. This is
// calc_checksum() from save_file.c, verified against the decompilation.
[[nodiscard]] std::uint16_t checksumOf(const std::uint8_t* data, std::size_t size) noexcept;

// Write magic + big-endian checksum into the last four bytes of a block.
void signBlock(std::uint8_t* data, std::size_t size, std::uint16_t magic) noexcept;

struct BlockState {
    bool magic{};
    bool checksum{};
    std::uint16_t stored{};
    std::uint16_t computed{};
    std::uint16_t storedMagic{};
    [[nodiscard]] bool valid() const noexcept { return magic && checksum; }
};

[[nodiscard]] BlockState inspectBlock(const std::uint8_t* data, std::size_t size,
                                      std::uint16_t magic) noexcept;

// A freshly erased EEPROM exactly as the game would write it: four zeroed,
// correctly signed save slots (mirrored) and the default menu block that
// wipe_main_menu_data() produces.
[[nodiscard]] EepromBytes makeBlankEeprom();

// --------------------------------------------------------------------------
// Validation
// --------------------------------------------------------------------------
enum class PairStatus : std::uint8_t {
    BothValidIdentical,
    BothValidDifferent,
    OnlyPrimaryValid,
    OnlyBackupValid,
    BothInvalid,
};

struct PairReport {
    PairStatus status{PairStatus::BothInvalid};
    bool recoverable{false};  // can be repaired without choosing or guessing
    std::string text;
    std::vector<std::size_t> differingOffsets;  // offsets relative to the block start
};

struct Report {
    std::array<BlockState, 8> slots{};
    std::array<BlockState, 2> menu{};
    bool valid{};
    std::vector<std::string> notes;
    std::array<PairReport, kPairCount> pairs{};  // A, B, C, D, menu

    [[nodiscard]] const BlockState& block(std::size_t id) const noexcept {
        return id < 8U ? slots[id] : menu[id - 8U];
    }
};

[[nodiscard]] Report validateBytes(const EepromBytes& bytes);

enum class ByteOrder : std::uint8_t { Native, Swapped16, Swapped32 };
[[nodiscard]] const char* byteOrderName(ByteOrder order) noexcept;

struct RepairAction {
    std::size_t pair{};  // 0-3 slots, 4 menu
    bool toPrimary{};    // true: backup -> primary, false: primary -> backup
    std::string description;
};

// --------------------------------------------------------------------------
// Describing bytes and changes
// --------------------------------------------------------------------------
struct FieldChange {
    std::string field;
    std::string before;
    std::string after;
};

struct RegionInfo {
    std::string name;  // e.g. "Slot A primary"
    std::size_t blockId{};
    std::size_t relative{};
    std::string field;  // e.g. "Flags (byte 2 of 4)"
};
[[nodiscard]] RegionInfo describeOffset(std::size_t offset);
[[nodiscard]] std::vector<FieldChange> describeChanges(const EepromBytes& before,
                                                       const EepromBytes& after);

// --------------------------------------------------------------------------
// Export profiles
// --------------------------------------------------------------------------
enum class Profile : std::uint8_t {
    Raw,
    EverDriveV1,
    EverDriveV2,
    EverDriveX,
    ED64Plus,
    Retrode2,
    EEP,
    SAV,
    SRA,
    BIN,
};
inline constexpr std::size_t kProfileCount = 10;

struct ProfileInfo {
    Profile id;
    const char* name;
    const char* extension;
    std::size_t outputSize;
    const char* byteOrder;
    const char* padding;
    const char* device;
    bool verified;
    const char* note;
};
[[nodiscard]] const ProfileInfo& profileInfo(Profile profile) noexcept;
// Pure transformation used by exports; tested independently of the file system.
[[nodiscard]] std::vector<std::uint8_t> renderExport(const EepromBytes& bytes, Profile profile);

// --------------------------------------------------------------------------
// GameShark
// --------------------------------------------------------------------------
struct GameSharkOptions {
    std::uint32_t base{0x80207700U};  // placeholder: NOT verified for any ROM region
    bool use16Bit{true};
    bool includeAll{false};  // false = changed bytes only (versus the original image)
    bool comments{true};
};

struct GameSharkResult {
    bool ok{false};
    std::string error;
    std::string text;                // full text including comments
    std::vector<std::string> codes;  // code lines only
    std::vector<std::size_t> codeOffsets;  // EEPROM offset each code line starts at
    std::size_t byte8Count{};
    std::size_t byte16Count{};
    std::size_t firstOffset{};
    std::size_t lastOffset{};
};
[[nodiscard]] GameSharkResult generateGameShark(const EepromBytes& current,
                                                const EepromBytes& original,
                                                const GameSharkOptions& options);

// --------------------------------------------------------------------------
// Safe file I/O
// --------------------------------------------------------------------------
struct AtomicWriteOptions {
    bool makeBackup{true};         // copy an existing destination to "<name>.bak" first
    bool failBeforeCommit{false};  // test hook: simulate a crash after the temp file is complete
};

// Writes bytes to "<path>.tmp" in the same directory, flushes and fsyncs, checks
// the exact size, optionally backs up the existing file, then renames over the
// destination. The destination is never removed or truncated before the new
// content is complete: on any failure the original is left untouched.
[[nodiscard]] bool atomicWriteFile(const std::filesystem::path& path,
                                   std::span<const std::uint8_t> bytes,
                                   const AtomicWriteOptions& options, std::string& error);

[[nodiscard]] std::filesystem::path backupPathFor(const std::filesystem::path& path);

// --------------------------------------------------------------------------
// History
// --------------------------------------------------------------------------
struct HistoryEntry {
    std::string description;
    EepromBytes before{};
    EepromBytes after{};
    std::string coalesceKey;
};

// --------------------------------------------------------------------------
// The engine
// --------------------------------------------------------------------------
class Core {
public:
    using Bytes = EepromBytes;

    struct SaveOptions {
        bool makeBackup{true};
        bool failBeforeCommit{false};  // test hook, see AtomicWriteOptions
    };

    Core();

    // ---- file I/O ---------------------------------------------------------
    // Reads exactly 512 bytes. Detects 16-bit and 32-bit byte-swapped dumps and
    // converts to canonical order. If autoRepair is set, blocks that have exactly
    // one valid copy are restored from it (never when both are invalid, never by
    // guessing between two valid copies).
    bool load(const std::filesystem::path& path, bool autoRepair, std::string& error);
    bool loadBytes(std::span<const std::uint8_t> bytes, bool autoRepair, std::string& error);
    // Writes the current 512 bytes exactly as they are (no hidden checksum
    // rewriting), atomically, with an optional .bak of any existing file.
    bool save(const std::filesystem::path& path, std::string& error);
    bool save(const std::filesystem::path& path, const SaveOptions& options, std::string& error);
    // Writes an export profile. Does not modify the loaded data.
    bool exportFile(const std::filesystem::path& path, Profile profile, std::string& error) const;
    // Replace the whole image with a blank valid EEPROM (undoable).
    void newBlank();

    // ---- read access ------------------------------------------------------
    [[nodiscard]] Bytes bytes() const { return data_; }
    [[nodiscard]] const Bytes& view() const noexcept { return data_; }
    [[nodiscard]] std::uint8_t byte(std::size_t index) const { return data_.at(index); }
    [[nodiscard]] const Bytes& original() const noexcept { return original_; }
    [[nodiscard]] const Bytes& savedImage() const noexcept { return saved_; }
    [[nodiscard]] ByteOrder detectedOrder() const noexcept { return order_; }
    [[nodiscard]] bool dirty() const noexcept { return data_ != saved_; }
    [[nodiscard]] SlotImage slotImage(int slot) const;
    [[nodiscard]] StarSummary stars(int slot) const;
    [[nodiscard]] std::vector<FieldChange> pendingChanges() const {
        return describeChanges(saved_, data_);
    }

    // ---- editing (each call is one undoable history entry) ----------------
    // Raw edits do NOT touch checksums, so a raw edit is visible as an invalid block.
    bool set(std::size_t index, std::uint8_t value);
    bool setMirrored(int slot, std::size_t relativeOffset, std::uint8_t value);
    bool pasteBytes(std::size_t offset, std::span<const std::uint8_t> bytes);

    // Structured edits modify the slot and keep both copies synchronized with
    // freshly computed signatures.
    bool editSlot(int slot, const std::string& description,
                  const std::function<void(SlotImage&)>& mutate,
                  const std::string& coalesceKey = {});
    bool setFlag(int slot, std::uint32_t mask, bool on);
    bool setCoinScore(int slot, std::size_t course, int value);  // clamped to 0..255
    bool setCourseStar(int slot, std::size_t course, int star, bool on);
    bool setCourseStars(int slot, std::size_t course, std::uint8_t starBits);
    bool setSecretStarFlag(int slot, std::size_t secretIndex, bool on);

    // Generic undoable replacement of the whole image.
    bool applyEdit(const std::string& description, const Bytes& after,
                   const std::string& coalesceKey = {});

    // ---- validation & repair ---------------------------------------------
    [[nodiscard]] Report validate() const { return validateBytes(data_); }
    [[nodiscard]] std::vector<RepairAction> planRepair() const;
    // Applies every unambiguous repair (one valid copy -> invalid copy) as a
    // single history entry and returns what it did. Does nothing for pairs where
    // both copies are invalid, or both are valid but different.
    std::vector<std::string> repair();
    // Resolve a pair whose copies are both valid but different (or force a
    // direction): copy the chosen valid block over the other.
    bool resolvePair(std::size_t pair, bool usePrimary, std::string& error);
    // Explicit advanced action: recompute magic + checksum of one block (0-9).
    // Does not make semantically wrong data correct.
    bool rebuildChecksum(std::size_t blockId);
    // Recompute magic + checksum of every block (explicit advanced action).
    void normalize();

    // ---- GameShark --------------------------------------------------------
    [[nodiscard]] std::string gameShark(std::uint32_t address, bool use16Bit,
                                        bool includeAll) const;
    [[nodiscard]] GameSharkResult gameSharkCodes(const GameSharkOptions& options) const {
        return generateGameShark(data_, original_, options);
    }

    // ---- history ----------------------------------------------------------
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    bool undo();
    bool redo();
    [[nodiscard]] const std::vector<HistoryEntry>& undoStack() const noexcept { return undo_; }
    [[nodiscard]] const std::vector<HistoryEntry>& redoStack() const noexcept { return redo_; }
    [[nodiscard]] const std::string& lastAction() const noexcept { return lastAction_; }
    void setLastAction(std::string text) { lastAction_ = std::move(text); }
    void markSaved();

private:
    static constexpr std::size_t kMaxHistory = 256;

    void adopt(const Bytes& bytes, ByteOrder order);

    Bytes data_{};
    Bytes original_{};  // as loaded (or as created); used for diffs and GameShark
    Bytes saved_{};     // as last written; used for the unsaved-changes indicator
    ByteOrder order_{ByteOrder::Native};
    std::vector<HistoryEntry> undo_;
    std::vector<HistoryEntry> redo_;
    bool coalesceOpen_{false};
    std::string lastAction_;
};

}  // namespace sm64
