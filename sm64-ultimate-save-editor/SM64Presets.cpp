#include "SM64Presets.hpp"

#include <algorithm>

namespace sm64 {
namespace {

constexpr std::array<PresetInfo, kPresetCount> kCatalog{{
    {PresetId::NewBlankEeprom, "New empty EEPROM",
     "Replaces the whole file with a blank save exactly as the game writes it: four empty slots and a "
     "default menu block.",
     PresetScope::WholeFile, true},
    {PresetId::NewOccupiedSlotA, "New occupied Slot A",
     "A blank file whose Slot A is marked as an existing save (no stars, no switches).",
     PresetScope::WholeFile, true},
    {PresetId::UnlockAllCaps, "Unlock all cap switches",
     "Presses the red, green and blue switches. This only makes the Wing, Metal and Vanish Cap blocks "
     "appear; Mario does not wear a cap permanently.",
     PresetScope::Slot, false},
    {PresetId::UnlockCastleAccess, "Unlock castle access",
     "Marks the basement and upstairs doors, all star doors, the Dire, Dire Docks painting and the moat "
     "as opened. Held-key flags are cleared, as the game does after a door is unlocked.",
     PresetScope::Slot, false},
    {PresetId::Progress70Stars, "Set 70-star progression",
     "Exactly 70 stars: all seven stars in the first ten courses, plus caps and castle access. All other "
     "star data in the slot is replaced.",
     PresetScope::Slot, true},
    {PresetId::Complete120Stars, "Set full 120-star completion",
     "All 105 main-course stars and all 15 secret stars (exactly 120), all caps and doors, and coin high "
     "scores raised to at least 100.",
     PresetScope::Slot, true},
    {PresetId::ClearSlot, "Clear selected slot",
     "Erases the slot the same way the game's Erase option does (both copies zeroed and re-signed).",
     PresetScope::Slot, true},
    {PresetId::CopySlot, "Copy slot to another slot",
     "Copies every byte of the selected slot into both copies of the destination slot.",
     PresetScope::SlotPair, true},
    {PresetId::SyncPrimaryToBackup, "Synchronize backup from primary",
     "Overwrites the backup copy with the primary copy. Requires a valid primary copy.",
     PresetScope::Slot, false},
    {PresetId::RestoreFromBackup, "Restore slot from backup",
     "Overwrites the primary copy with the backup copy. Requires a valid backup copy.",
     PresetScope::Slot, true},
    {PresetId::RepairAllRecoverable, "Repair all recoverable blocks",
     "Restores any block that has exactly one valid copy. Pairs where both copies are invalid, or both "
     "valid but different, are left alone.",
     PresetScope::WholeFile, false},
    {PresetId::NoveltyAllCapsNoStars, "Novelty: all caps, zero stars",
     "Presses every cap switch but removes every star and coin score. A fun challenge start.",
     PresetScope::Slot, true},
    {PresetId::RandomChallenge, "Random challenge save",
     "A reproducible random mix of stars from a seed. The same seed always gives the same save.",
     PresetScope::Slot, true},
}};

constexpr std::uint32_t kCaps = save_flag::HaveWingCap | save_flag::HaveMetalCap |
                                save_flag::HaveVanishCap;
constexpr std::uint32_t kKeys = save_flag::HaveKey1 | save_flag::HaveKey2;
constexpr std::uint32_t kCastle =
    save_flag::UnlockedBasementDoor | save_flag::UnlockedUpstairsDoor | save_flag::DddMovedBack |
    save_flag::MoatDrained | save_flag::UnlockedPssDoor | save_flag::UnlockedWfDoor |
    save_flag::UnlockedCcmDoor | save_flag::UnlockedJrbDoor | save_flag::UnlockedBitdwDoor |
    save_flag::UnlockedBitfsDoor | save_flag::Unlocked50StarDoor;
constexpr std::uint32_t kLostCap = save_flag::CapOnGround | save_flag::CapOnKlepto |
                                   save_flag::CapOnUkiki | save_flag::CapOnMrBlizzard;

std::string letter(int slot) { return std::string(1U, static_cast<char>('A' + slot)); }

bool slotOk(int slot) { return slot >= 0 && slot < static_cast<int>(kSlotCount); }

void writeSlot(EepromBytes& bytes, int slot, const SlotImage& image) {
    const auto s = static_cast<std::size_t>(slot);
    std::copy(image.begin(), image.end(), bytes.begin() + static_cast<std::ptrdiff_t>(P[s]));
    std::copy(image.begin(), image.end(), bytes.begin() + static_cast<std::ptrdiff_t>(B[s]));
    signBlock(bytes.data() + P[s], Block, kSlotMagic);
    signBlock(bytes.data() + B[s], Block, kSlotMagic);
}

SlotImage readSlot(const EepromBytes& bytes, int slot) {
    SlotImage image{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(P[static_cast<std::size_t>(slot)]), Block,
                image.begin());
    return image;
}

void clearAllStars(SlotImage& image) {
    for (std::size_t i = 0; i < slot_field::CourseStarCount; ++i) setCourseStarBits(image, i, 0);
    setSlotFlags(image, slotFlags(image) & ~save_flag::CastleStarMask);
}

// xorshift32: deterministic across platforms.
std::uint32_t nextRandom(std::uint32_t& state) {
    if (state == 0U) state = 0x9E3779B9U;
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

}  // namespace

const std::array<PresetInfo, kPresetCount>& presetCatalog() noexcept { return kCatalog; }

const PresetInfo& presetInfo(PresetId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return kCatalog[index < kCatalog.size() ? index : 0U];
}

PresetPlan planPreset(const EepromBytes& current, const PresetRequest& request) {
    PresetPlan plan;
    const PresetInfo& info = presetInfo(request.id);
    plan.title = info.name;
    plan.after = current;

    const bool needsSlot = info.scope != PresetScope::WholeFile;
    if (needsSlot && !slotOk(request.slot)) {
        plan.error = "Choose a valid slot (A-D).";
        return plan;
    }
    if (info.scope == PresetScope::SlotPair &&
        (!slotOk(request.destSlot) || request.destSlot == request.slot)) {
        plan.error = "Choose a destination slot that is different from the source slot.";
        return plan;
    }

    const int slot = request.slot;
    const Report report = validateBytes(current);

    switch (request.id) {
        case PresetId::NewBlankEeprom:
            plan.after = makeBlankEeprom();
            plan.summary = "Whole file replaced with a blank, valid EEPROM.";
            break;

        case PresetId::NewOccupiedSlotA: {
            plan.after = makeBlankEeprom();
            SlotImage image = readSlot(plan.after, 0);
            setSlotFlags(image, save_flag::FileExists);
            writeSlot(plan.after, 0, image);
            plan.summary = "Blank file with Slot A marked as occupied.";
            break;
        }

        case PresetId::UnlockAllCaps: {
            SlotImage image = readSlot(current, slot);
            setSlotFlags(image, slotFlags(image) | save_flag::FileExists | kCaps);
            writeSlot(plan.after, slot, image);
            plan.summary = "Wing, Metal and Vanish Cap switches pressed in Slot " + letter(slot) + ".";
            break;
        }

        case PresetId::UnlockCastleAccess: {
            SlotImage image = readSlot(current, slot);
            setSlotFlags(image, (slotFlags(image) | save_flag::FileExists | kCastle) & ~kKeys);
            writeSlot(plan.after, slot, image);
            plan.summary = "Castle doors, painting and moat unlocked in Slot " + letter(slot) + ".";
            break;
        }

        case PresetId::Progress70Stars: {
            SlotImage image = readSlot(current, slot);
            clearAllStars(image);
            for (std::size_t course = 0; course < 10U; ++course) {
                setCourseStarBits(image, course, kStarBitsMask);
            }
            setSlotFlags(image, (slotFlags(image) | save_flag::FileExists | kCaps | kCastle) & ~kKeys);
            writeSlot(plan.after, slot, image);
            plan.summary = "Slot " + letter(slot) + " now has exactly 70 stars.";
            break;
        }

        case PresetId::Complete120Stars: {
            SlotImage image = readSlot(current, slot);
            clearAllStars(image);
            for (std::size_t course = 0; course < kMainCourseCount; ++course) {
                setCourseStarBits(image, course, kStarBitsMask);
                image[slot_field::CoinScores + course] =
                    (std::max)(image[slot_field::CoinScores + course], std::uint8_t{100});
            }
            for (const SecretStar& star : kSecretStars) setSecretStar(image, star, true);
            setSlotFlags(image,
                         (slotFlags(image) | save_flag::FileExists | kCaps | kCastle) & ~(kKeys | kLostCap));
            writeSlot(plan.after, slot, image);
            plan.summary = "Slot " + letter(slot) + " now has exactly 120 stars.";
            break;
        }

        case PresetId::ClearSlot: {
            writeSlot(plan.after, slot, SlotImage{});
            plan.summary = "Slot " + letter(slot) + " erased (both copies).";
            break;
        }

        case PresetId::CopySlot: {
            writeSlot(plan.after, request.destSlot, readSlot(current, slot));
            plan.summary = "Slot " + letter(slot) + " copied into Slot " + letter(request.destSlot) + ".";
            break;
        }

        case PresetId::SyncPrimaryToBackup: {
            if (!report.block(static_cast<std::size_t>(slot) * 2U).valid()) {
                plan.error = "The primary copy of Slot " + letter(slot) +
                             " is not valid, so it cannot be used as the source.";
                return plan;
            }
            writeSlot(plan.after, slot, readSlot(current, slot));
            plan.summary = "Backup of Slot " + letter(slot) + " now matches the primary copy.";
            break;
        }

        case PresetId::RestoreFromBackup: {
            const auto s = static_cast<std::size_t>(slot);
            if (!report.block(s * 2U + 1U).valid()) {
                plan.error = "The backup copy of Slot " + letter(slot) +
                             " is not valid, so it cannot be used as the source.";
                return plan;
            }
            std::copy_n(current.begin() + static_cast<std::ptrdiff_t>(B[s]), Block,
                        plan.after.begin() + static_cast<std::ptrdiff_t>(P[s]));
            plan.summary = "Primary copy of Slot " + letter(slot) + " restored from its backup.";
            break;
        }

        case PresetId::RepairAllRecoverable: {
            std::size_t repaired = 0;
            for (std::size_t pair = 0; pair < kPairCount; ++pair) {
                const PairStatus status = report.pairs[pair].status;
                if (status != PairStatus::OnlyPrimaryValid && status != PairStatus::OnlyBackupValid) continue;
                const std::size_t pid = pair < kSlotCount ? pair * 2U : 8U;
                const BlockInfo p = blockInfo(pid);
                const BlockInfo b = blockInfo(pid + 1U);
                const BlockInfo& src = status == PairStatus::OnlyPrimaryValid ? p : b;
                const BlockInfo& dst = status == PairStatus::OnlyPrimaryValid ? b : p;
                std::copy_n(current.begin() + static_cast<std::ptrdiff_t>(src.offset), src.size,
                            plan.after.begin() + static_cast<std::ptrdiff_t>(dst.offset));
                ++repaired;
            }
            if (repaired == 0U) {
                plan.error = "No block has exactly one valid copy, so there is nothing safe to repair.";
                return plan;
            }
            plan.summary = std::to_string(repaired) + " block pair(s) restored from their valid copy.";
            break;
        }

        case PresetId::NoveltyAllCapsNoStars: {
            SlotImage image = readSlot(current, slot);
            clearAllStars(image);
            for (std::size_t i = 0; i < slot_field::CoinScoreCount; ++i) image[slot_field::CoinScores + i] = 0;
            setSlotFlags(image, slotFlags(image) | save_flag::FileExists | kCaps);
            writeSlot(plan.after, slot, image);
            plan.summary = "Slot " + letter(slot) + ": every cap switch on, zero stars.";
            break;
        }

        case PresetId::RandomChallenge: {
            SlotImage image = readSlot(current, slot);
            clearAllStars(image);
            std::uint32_t state = request.seed;
            for (std::size_t course = 0; course < kMainCourseCount; ++course) {
                std::uint8_t bits = 0;
                for (unsigned star = 0; star < 7U; ++star) {
                    if (nextRandom(state) % 3U == 0U) bits = static_cast<std::uint8_t>(bits | (1U << star));
                }
                setCourseStarBits(image, course, bits);
            }
            for (const SecretStar& star : kSecretStars) {
                if (nextRandom(state) % 4U == 0U) setSecretStar(image, star, true);
            }
            setSlotFlags(image, slotFlags(image) | save_flag::FileExists);
            writeSlot(plan.after, slot, image);
            plan.summary = "Slot " + letter(slot) + " filled from seed " + std::to_string(request.seed) + ".";
            break;
        }
    }

    plan.unchanged = plan.after == current;
    plan.changes = describeChanges(current, plan.after);
    plan.ok = true;
    return plan;
}

bool applyPreset(Core& core, const PresetRequest& request, std::string& error) {
    const PresetPlan plan = planPreset(core.view(), request);
    if (!plan.ok) {
        error = plan.error;
        return false;
    }
    if (plan.unchanged) {
        error = "This preset would not change anything.";
        return false;
    }
    error.clear();
    return core.applyEdit(plan.title + ": " + plan.summary, plan.after);
}

}  // namespace sm64
