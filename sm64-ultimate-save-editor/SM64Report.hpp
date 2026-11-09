// SM64Report.hpp - file fingerprints and exportable reports.
#pragma once

#include "SM64SystemCore.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sm64 {

// Standard CRC-32 (IEEE 802.3 / zlib). Used only to identify exact files; it is
// NOT an SM64 save checksum.
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data) noexcept;

[[nodiscard]] std::string jsonEscape(std::string_view text);

// Machine-readable report of the image: layout validation plus per-slot summary.
[[nodiscard]] std::string buildJsonReport(const EepromBytes& bytes, std::string_view fileName, ByteOrder order);

// Plain-text validation report (one line per block, then repair guidance).
[[nodiscard]] std::string buildValidationText(const EepromBytes& bytes, std::string_view fileName);

}  // namespace sm64
