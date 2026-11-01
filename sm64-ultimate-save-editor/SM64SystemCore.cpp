#include "SM64SystemCore.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace sm64 {
namespace {
constexpr std::size_t MenuPrimaryOffset = 0x1C0;
constexpr std::size_t MenuBackupOffset = 0x1E0;
constexpr std::size_t MenuBlockSize = 32;

std::uint16_t readBigEndian16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U) |
        static_cast<std::uint16_t>(data[1])
    );
}

bool blockIsValid(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size < 4U) return false;
    if (data[size - 4U] != 0x44U || data[size - 3U] != 0x41U) return false;

    std::uint16_t calculated = 0U;
    for (std::size_t index = 0; index < size - 2U; ++index) {
        calculated = static_cast<std::uint16_t>(calculated + data[index]);
    }

    return readBigEndian16(data + size - 2U) == calculated;
}

Core::Bytes swap16(Core::Bytes bytes) noexcept {
    for (std::size_t index = 0; index < bytes.size(); index += 2U) {
        std::swap(bytes[index], bytes[index + 1U]);
    }
    return bytes;
}

Core::Bytes swap32(Core::Bytes bytes) noexcept {
    for (std::size_t index = 0; index < bytes.size(); index += 4U) {
        std::swap(bytes[index], bytes[index + 3U]);
        std::swap(bytes[index + 1U], bytes[index + 2U]);
    }
    return bytes;
}

int validBlockCount(const Core::Bytes& bytes) noexcept {
    int count = 0;

    for (std::size_t slot = 0; slot < P.size(); ++slot) {
        if (blockIsValid(bytes.data() + P[slot], Block)) ++count;
        if (blockIsValid(bytes.data() + B[slot], Block)) ++count;
    }

    if (blockIsValid(bytes.data() + MenuPrimaryOffset, MenuBlockSize)) ++count;
    if (blockIsValid(bytes.data() + MenuBackupOffset, MenuBlockSize)) ++count;
    return count;
}
}

std::uint16_t Core::sum(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 2U) return 0U;

    std::uint16_t result = 0U;
    for (std::size_t index = 0; index < size - 2U; ++index) {
        result = static_cast<std::uint16_t>(result + data[index]);
    }
    return result;
}

void Core::fix(std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < 4U) return;

    data[size - 4U] = 0x44U;
    data[size - 3U] = 0x41U;

    const std::uint16_t checksum = sum(data, size);
    data[size - 2U] = static_cast<std::uint8_t>(checksum >> 8U);
    data[size - 1U] = static_cast<std::uint8_t>(checksum & 0xFFU);
}

Core::Core() {
    data_.fill(0U);
    normalize();
    base_ = data_;
}

void Core::set(std::size_t index, std::uint8_t value) {
    data_.at(index) = value;
}

void Core::setMirrored(int slot, std::size_t relativeOffset, std::uint8_t value) {
    if (slot < 0 || slot >= static_cast<int>(P.size()) || relativeOffset >= Block) return;

    const std::size_t slotIndex = static_cast<std::size_t>(slot);
    data_.at(P.at(slotIndex) + relativeOffset) = value;
    data_.at(B.at(slotIndex) + relativeOffset) = value;
}

Report Core::validate() const {
    Report report{};
    report.valid = true;

    for (std::size_t slot = 0; slot < P.size(); ++slot) {
        const std::array<std::size_t, 2> offsets{P[slot], B[slot]};

        for (std::size_t copy = 0; copy < offsets.size(); ++copy) {
            const std::size_t offset = offsets[copy];
            BlockState& state = report.slots.at(slot * 2U + copy);

            state.magic =
                data_.at(offset + Block - 4U) == 0x44U &&
                data_.at(offset + Block - 3U) == 0x41U;
            state.stored = readBigEndian16(data_.data() + offset + Block - 2U);
            state.computed = sum(data_.data() + offset, Block);
            state.checksum = state.stored == state.computed;
            report.valid = report.valid && state.magic && state.checksum;
        }
    }

    const std::array<std::size_t, 2> menuOffsets{MenuPrimaryOffset, MenuBackupOffset};
    for (std::size_t copy = 0; copy < menuOffsets.size(); ++copy) {
        const std::size_t offset = menuOffsets[copy];
        BlockState& state = report.menu.at(copy);

        state.magic =
            data_.at(offset + MenuBlockSize - 4U) == 0x44U &&
            data_.at(offset + MenuBlockSize - 3U) == 0x41U;
        state.stored = readBigEndian16(data_.data() + offset + MenuBlockSize - 2U);
        state.computed = sum(data_.data() + offset, MenuBlockSize);
        state.checksum = state.stored == state.computed;
        report.valid = report.valid && state.magic && state.checksum;
    }

    return report;
}

void Core::normalize() {
    for (const std::size_t offset : P) fix(data_.data() + offset, Block);
    for (const std::size_t offset : B) fix(data_.data() + offset, Block);
    fix(data_.data() + MenuPrimaryOffset, MenuBlockSize);
    fix(data_.data() + MenuBackupOffset, MenuBlockSize);
}

std::vector<std::string> Core::repair() {
    std::vector<std::string> actions;

    for (std::size_t slot = 0; slot < P.size(); ++slot) {
        const bool primaryValid = blockIsValid(data_.data() + P[slot], Block);
        const bool backupValid = blockIsValid(data_.data() + B[slot], Block);

        if (!primaryValid && backupValid) {
            std::copy_n(data_.data() + B[slot], Block, data_.data() + P[slot]);
            actions.push_back("Restored primary slot " + std::string(1U, static_cast<char>('A' + slot)));
        } else if (primaryValid && !backupValid) {
            std::copy_n(data_.data() + P[slot], Block, data_.data() + B[slot]);
            actions.push_back("Restored backup slot " + std::string(1U, static_cast<char>('A' + slot)));
        }
    }

    const bool menuPrimaryValid = blockIsValid(data_.data() + MenuPrimaryOffset, MenuBlockSize);
    const bool menuBackupValid = blockIsValid(data_.data() + MenuBackupOffset, MenuBlockSize);

    if (!menuPrimaryValid && menuBackupValid) {
        std::copy_n(data_.data() + MenuBackupOffset, MenuBlockSize, data_.data() + MenuPrimaryOffset);
        actions.push_back("Restored primary menu block from backup");
    } else if (menuPrimaryValid && !menuBackupValid) {
        std::copy_n(data_.data() + MenuPrimaryOffset, MenuBlockSize, data_.data() + MenuBackupOffset);
        actions.push_back("Restored backup menu block from primary");
    }

    return actions;
}

bool Core::load(const std::filesystem::path& path, bool autoRepair, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Cannot open the selected file.";
        return false;
    }

    Bytes incoming{};
    input.read(reinterpret_cast<char*>(incoming.data()), static_cast<std::streamsize>(incoming.size()));

    if (input.gcount() != static_cast<std::streamsize>(incoming.size()) ||
        input.peek() != std::ifstream::traits_type::eof()) {
        error = "Input must be exactly 512 bytes.";
        return false;
    }

    Bytes best = incoming;
    int bestScore = validBlockCount(best);

    const Bytes swapped16 = swap16(incoming);
    const int swapped16Score = validBlockCount(swapped16);
    if (swapped16Score > bestScore) {
        best = swapped16;
        bestScore = swapped16Score;
    }

    const Bytes swapped32 = swap32(incoming);
    const int swapped32Score = validBlockCount(swapped32);
    if (swapped32Score > bestScore) {
        best = swapped32;
    }

    data_ = best;
    base_ = data_;

    if (autoRepair) repair();
    error.clear();
    return true;
}

bool Core::write(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& error
) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Cannot create the temporary output file.";
            return false;
        }

        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );

        if (!output) {
            error = "Writing the output file failed.";
            return false;
        }
    }

    std::error_code operationError;
    std::filesystem::rename(temporary, path, operationError);

    if (operationError) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        operationError.clear();
        std::filesystem::rename(temporary, path, operationError);
    }

    if (operationError) {
        error = operationError.message();
        return false;
    }

    error.clear();
    return true;
}

bool Core::save(const std::filesystem::path& path, std::string& error) {
    normalize();
    const std::vector<std::uint8_t> output(data_.begin(), data_.end());

    if (!write(path, output, error)) return false;
    base_ = data_;
    return true;
}

bool Core::exportFile(const std::filesystem::path& path, Profile profile, std::string& error) {
    normalize();

    const bool sramWrapper = profile == Profile::SRA;
    const std::size_t outputSize = sramWrapper ? 32768U : Size;
    const std::uint8_t fillByte = sramWrapper ? 0xFFU : 0x00U;
    std::vector<std::uint8_t> output(outputSize, fillByte);

    std::copy(data_.begin(), data_.end(), output.begin());

    if (sramWrapper) {
        for (std::size_t index = 0; index < Size; index += 2U) {
            std::swap(output[index], output[index + 1U]);
        }
    }

    return write(path, output, error);
}

std::string Core::gameShark(std::uint32_t address, bool use16Bit, bool includeAll) const {
    if (address < 0x80000000U || address > 0x807FFFFFU) {
        return "# Invalid RDRAM address. Expected 0x80000000 through 0x807FFFFF.\n";
    }

    std::ostringstream output;
    output << "# Verify this buffer address for the exact ROM region and revision.\n";
    output << std::uppercase << std::hex << std::setfill('0');

    std::size_t index = 0U;
    while (index < Size) {
        if (!includeAll && data_[index] == base_[index]) {
            ++index;
            continue;
        }

        const std::uint32_t codeAddress =
            (address + static_cast<std::uint32_t>(index)) & 0x00FFFFFFU;

        const bool canUseWord =
            use16Bit &&
            (index % 2U == 0U) &&
            (index + 1U < Size) &&
            (includeAll || data_[index + 1U] != base_[index + 1U]);

        if (canUseWord) {
            const std::uint16_t value = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(data_[index]) << 8U) |
                static_cast<std::uint16_t>(data_[index + 1U])
            );

            output << "81" << std::setw(6) << codeAddress
                   << ' ' << std::setw(4) << value << '\n';
            index += 2U;
        } else {
            output << "80" << std::setw(6) << codeAddress
                   << " 00" << std::setw(2)
                   << static_cast<unsigned int>(data_[index]) << '\n';
            ++index;
        }
    }

    return output.str();
}
}
