// SM64Presets.hpp - safe, previewable, single-undo presets.
//
// A preset is a pure function: current bytes in, new bytes + a field-level
// change list out. Nothing is written until the caller applies the plan, and
// applying it is exactly one history entry.
#pragma once

#include "SM64SystemCore.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sm64 {

enum class PresetId : std::uint8_t {
    NewBlankEeprom,
    NewOccupiedSlotA,
    UnlockAllCaps,
    UnlockCastleAccess,
    Progress70Stars,
    Complete120Stars,
    ClearSlot,
    CopySlot,
    SyncPrimaryToBackup,
    RestoreFromBackup,
    RepairAllRecoverable,
    NoveltyAllCapsNoStars,
    RandomChallenge,
};
inline constexpr std::size_t kPresetCount = 13;

enum class PresetScope : std::uint8_t { WholeFile, Slot, SlotPair };

struct PresetInfo {
    PresetId id;
    const char* name;
    const char* description;
    PresetScope scope;
    bool destructive;  // discards existing data the user might want to keep
};

[[nodiscard]] const std::array<PresetInfo, kPresetCount>& presetCatalog() noexcept;
[[nodiscard]] const PresetInfo& presetInfo(PresetId id) noexcept;

struct PresetRequest {
    PresetId id{PresetId::NewBlankEeprom};
    int slot{0};
    int destSlot{1};
    std::uint32_t seed{1};
};

struct PresetPlan {
    bool ok{false};
    bool unchanged{false};
    std::string error;
    std::string title;
    std::string summary;
    EepromBytes after{};
    std::vector<FieldChange> changes;
};

[[nodiscard]] PresetPlan planPreset(const EepromBytes& current, const PresetRequest& request);

// Plans and applies as one undoable edit. Returns false (with `error`) if the
// preset is not applicable or would change nothing.
bool applyPreset(Core& core, const PresetRequest& request, std::string& error);

}  // namespace sm64
