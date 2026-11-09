// SM64Export.cpp - export profiles and GameShark code generation.
//
// No profile invents a device-specific byte transformation. Every profile except
// the SRA wrapper writes the canonical 512-byte big-endian EEPROM unchanged.
#include "SM64SystemCore.hpp"

#include <algorithm>
#include <cstdio>

namespace sm64 {
namespace {

constexpr std::size_t kSraSize = 32768;

constexpr std::array<ProfileInfo, kProfileCount> kProfiles{{
    {Profile::Raw, "Raw EEPROM (canonical)", ".eep", Size, "Big-endian, exactly as the game stores it", "None",
     "Most N64 emulators; the reference format", true, "The 512-byte EEPROM image with no transformation."},
    {Profile::EverDriveV1, "EverDrive V1 (alias)", ".eep", Size, "Big-endian, no transform", "None", "EverDrive-64 V1",
     false, "Experimental label. No device-specific transformation is documented, so this is identical to Raw."},
    {Profile::EverDriveV2, "EverDrive V2 (alias)", ".eep", Size, "Big-endian, no transform", "None", "EverDrive-64 V2",
     false, "Experimental label. No device-specific transformation is documented, so this is identical to Raw."},
    {Profile::EverDriveX, "EverDrive X/Pro (alias)", ".eep", Size, "Big-endian, no transform", "None",
     "EverDrive-64 X-series", false,
     "Experimental label. No device-specific transformation is documented, so this is identical to Raw."},
    {Profile::ED64Plus, "ED64Plus (alias)", ".eep", Size, "Big-endian, no transform", "None", "ED64Plus", false,
     "Experimental label. No device-specific transformation is documented, so this is identical to Raw."},
    {Profile::Retrode2, "Retrode 2 (alias)", ".eep", Size, "Big-endian, no transform", "None", "Retrode 2", false,
     "Experimental label. No device-specific transformation is documented, so this is identical to Raw."},
    {Profile::EEP, "Emulator .eep", ".eep", Size, "Big-endian, no transform", "None",
     "Emulators that use .eep EEPROM files", true, "Same bytes as Raw with the common .eep extension."},
    {Profile::SAV, "Generic .sav", ".sav", Size, "Big-endian, no transform", "None",
     "Tools that expect a .sav extension", true,
     "Same bytes as Raw. The extension is a naming convention only; check that your tool accepts it."},
    {Profile::SRA, "32 KiB .sra wrapper (compatibility)", ".sra", kSraSize,
     "16-bit swapped copy of the EEPROM in the first 512 bytes", "0xFF to 32768 bytes", "SRAM-style containers", false,
     "Compatibility wrapper only. Retail SM64 uses EEPROM, not SRAM, so this is not the native format."},
    {Profile::BIN, "Generic .bin", ".bin", Size, "Big-endian, no transform", "None",
     "Tools that expect a .bin extension", true, "Same bytes as Raw. The extension is a naming convention only."},
}};

}  // namespace

const ProfileInfo& profileInfo(Profile profile) noexcept {
    const auto index = static_cast<std::size_t>(profile);
    return kProfiles[index < kProfiles.size() ? index : 0U];
}

std::vector<std::uint8_t> renderExport(const EepromBytes& bytes, Profile profile) {
    if (profile == Profile::SRA) {
        std::vector<std::uint8_t> out(kSraSize, 0xFFU);
        std::copy(bytes.begin(), bytes.end(), out.begin());
        for (std::size_t i = 0; i + 1U < Size; i += 2U) std::swap(out[i], out[i + 1U]);
        return out;
    }
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

GameSharkResult generateGameShark(const EepromBytes& current, const EepromBytes& original,
                                  const GameSharkOptions& options) {
    GameSharkResult result;

    constexpr std::uint32_t kRdramStart = 0x80000000U;
    constexpr std::uint32_t kRdramEnd = 0x807FFFFFU;  // 8 MiB with the Expansion Pak
    if (options.base < kRdramStart || options.base > kRdramEnd - static_cast<std::uint32_t>(Size - 1U)) {
        result.error =
            "RDRAM base must be between 0x80000000 and 0x807FFE00 so the whole 512-byte "
            "buffer fits in RDRAM.";
        return result;
    }
    if (options.use16Bit && (options.base % 2U) != 0U) {
        result.error = "16-bit codes need an even (2-byte aligned) base address.";
        return result;
    }

    bool haveOffsets = false;
    std::size_t index = 0;
    while (index < Size) {
        if (!options.includeAll && current[index] == original[index]) {
            ++index;
            continue;
        }

        const std::uint32_t address = (options.base + static_cast<std::uint32_t>(index)) & 0x00FFFFFFU;
        const bool word = options.use16Bit && (index % 2U == 0U) && (index + 1U < Size) &&
                          (options.includeAll || current[index + 1U] != original[index + 1U]);

        char line[32]{};
        if (word) {
            const unsigned value = (static_cast<unsigned>(current[index]) << 8U) | current[index + 1U];
            std::snprintf(line, sizeof(line), "81%06X %04X", static_cast<unsigned>(address), value);
            ++result.byte16Count;
        } else {
            std::snprintf(line, sizeof(line), "80%06X 00%02X", static_cast<unsigned>(address),
                          static_cast<unsigned>(current[index]));
            ++result.byte8Count;
        }
        result.codes.emplace_back(line);
        result.codeOffsets.push_back(index);
        if (!haveOffsets) {
            result.firstOffset = index;
            haveOffsets = true;
        }
        result.lastOffset = index + (word ? 1U : 0U);
        index += word ? 2U : 1U;
    }

    if (options.comments) {
        char header[160]{};
        std::snprintf(header, sizeof(header), "# Base address 0x%08X. Verify it for your exact ROM region/revision.\n",
                      static_cast<unsigned>(options.base));
        result.text += header;
        result.text += "# " + std::to_string(result.byte8Count) + " 8-bit and " + std::to_string(result.byte16Count) +
                       " 16-bit code(s).\n";
        if (result.codes.empty()) result.text += "# No bytes differ from the original image.\n";
    }
    for (const std::string& code : result.codes) {
        result.text += code;
        result.text += '\n';
    }
    result.ok = true;
    return result;
}

}  // namespace sm64
