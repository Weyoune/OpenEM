// SPDX-License-Identifier: GPL-3.0-only
// Crash-safe writing of a new savegame file (declared in session.hpp): the bytes go to a uniquely named
// temporary file next to the destination, are flushed to disk and then moved/linked to the final name,
// which must not exist yet. An existing file is never overwritten.
#include "ui/session.hpp"

#include <atomic>
#include <cerrno>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace em::ui {
namespace {
// Counter that makes temporary file names unique within the process.
std::atomic<unsigned long> serial{0};
}

// Writes bytes to the new file destination (parent directories are created). Throws FormatError with a
// user-facing message for an empty name or an existing file, std::system_error on I/O failure; the
// temporary file is removed on every failure.
void write_new_save(const std::filesystem::path& destination, const Bytes& bytes) {
    namespace fs = std::filesystem;
    if (destination.filename().empty()) throw FormatError("Bitte einen Dateinamen eingeben.");
    if (fs::exists(destination)) throw FormatError("Die Datei existiert bereits. Bitte einen neuen Namen wählen.");
    if (!destination.parent_path().empty()) fs::create_directories(destination.parent_path());

    fs::path temporary;
    bool created = false;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 32; attempt++) {
        temporary = destination;
        temporary += L".writing-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) { created = true; break; }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::system_error(int(GetLastError()), std::system_category(), "Spielstand vorbereiten");
    }
    if (!created) throw FormatError("Temporäre Speicherdatei konnte nicht angelegt werden.");
    try {
        size_t at = 0;
        while (at < bytes.size()) {
            const DWORD count = DWORD(std::min(bytes.size() - at, size_t(1024 * 1024)));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + at, count, &written, nullptr) || written == 0)
                throw std::system_error(int(GetLastError()), std::system_category(), "Spielstand schreiben");
            at += written;
        }
        if (!FlushFileBuffers(file)) throw std::system_error(int(GetLastError()), std::system_category(), "Spielstand sichern");
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        // Without MOVEFILE_REPLACE_EXISTING, a racing destination remains untouched.
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::system_error(int(GetLastError()), std::system_category(), "Spielstand veröffentlichen");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
#else
    int file = -1;
    for (int attempt = 0; attempt < 32; attempt++) {
        temporary = destination;
        temporary += ".writing-" + std::to_string(getpid()) + "-" + std::to_string(++serial);
        file = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file >= 0) { created = true; break; }
        if (errno != EEXIST) throw std::system_error(errno, std::generic_category(), "Spielstand vorbereiten");
    }
    if (!created) throw FormatError("Temporäre Speicherdatei konnte nicht angelegt werden.");
    try {
        size_t at = 0;
        while (at < bytes.size()) {
            const auto written = ::write(file, bytes.data() + at, bytes.size() - at);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::system_error(errno, std::generic_category(), "Spielstand schreiben");
            at += size_t(written);
        }
        if (::fsync(file)) throw std::system_error(errno, std::generic_category(), "Spielstand sichern");
        ::close(file); file = -1;
        if (::link(temporary.c_str(), destination.c_str()))
            throw std::system_error(errno, std::generic_category(), "Spielstand veröffentlichen");
        ::unlink(temporary.c_str());
    } catch (...) {
        if (file >= 0) ::close(file);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
#endif
}
}  // namespace em::ui
