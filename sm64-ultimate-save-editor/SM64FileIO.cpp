// SM64FileIO.cpp - crash-safe file replacement.
//
// Strategy (never touches the destination until the new content is complete):
//   1. write "<name>.tmp" next to the destination, flush + fsync, close
//   2. verify the temp file has exactly the expected size
//   3. copy any existing destination to "<name>.bak" (optional)
//   4. rename the temp file over the destination (atomic on POSIX; MoveFileEx
//      with replace-existing semantics on Windows via std::filesystem::rename)
// If any step fails the original file is left exactly as it was.
#include "SM64SystemCore.hpp"

#include <cstdio>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#else
#include <unistd.h>
#endif

namespace sm64 {
namespace {

std::FILE* openForWrite(const std::filesystem::path& path) {
#if defined(_WIN32)
    return _wfopen(path.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

bool syncToDisk(std::FILE* file) {
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}

}  // namespace

std::filesystem::path backupPathFor(const std::filesystem::path& path) {
    std::filesystem::path backup = path;
    backup += ".bak";
    return backup;
}

bool atomicWriteFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes,
                     const AtomicWriteOptions& options, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;

    fs::path temporary = path;
    temporary += ".tmp";

    std::FILE* file = openForWrite(temporary);
    if (file == nullptr) {
        error =
            "Cannot create the temporary output file next to the destination. "
            "The original file was not touched.";
        return false;
    }

    const std::size_t written = bytes.empty() ? 0U : std::fwrite(bytes.data(), 1U, bytes.size(), file);
    bool ok = written == bytes.size();
    ok = (std::fflush(file) == 0) && ok;
    ok = syncToDisk(file) && ok;
    ok = (std::fclose(file) == 0) && ok;

    if (!ok) {
        fs::remove(temporary, ec);
        error = "Writing the output file failed. The original file was not touched.";
        return false;
    }

    const std::uintmax_t actual = fs::file_size(temporary, ec);
    if (ec || actual != bytes.size()) {
        fs::remove(temporary, ec);
        error = "The temporary file has the wrong size. The original file was not touched.";
        return false;
    }

    if (options.failBeforeCommit) {
        fs::remove(temporary, ec);
        error = "Simulated failure before commit. The original file was not touched.";
        return false;
    }

    if (options.makeBackup) {
        std::error_code existsError;
        if (fs::is_regular_file(path, existsError)) {
            fs::copy_file(path, backupPathFor(path), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::error_code ignored;
                fs::remove(temporary, ignored);
                error = "Could not create the backup copy (" + ec.message() + "). The original file was not touched.";
                return false;
            }
        }
    }

    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        error = "Could not replace the destination file (" + ec.message() + "). The original file was not touched.";
        return false;
    }

    error.clear();
    return true;
}

}  // namespace sm64
