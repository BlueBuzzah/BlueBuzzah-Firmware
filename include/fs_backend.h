/**
 * @file fs_backend.h
 * @brief Minimal filesystem abstraction over the per-platform flash filesystem
 *
 * Implementations:
 * - fs_backend_nrf52.cpp: Adafruit InternalFS (LittleFS on internal flash)
 * - fs_backend_esp32.cpp: arduino-esp32 LittleFS (dedicated flash partition)
 * - fs_backend_mock.cpp:  in-memory map for native unit tests
 */

#ifndef FS_BACKEND_H
#define FS_BACKEND_H

#include <stddef.h>
#include <stdint.h>

namespace fsb {

/** Mount the filesystem. @return true if storage is available */
bool begin();

/** @return true if a file exists at path */
bool exists(const char* path);

/**
 * Read up to cap bytes from a file.
 * @param outLen receives the number of bytes read
 * @return true on success (file opened and read)
 */
bool readFile(const char* path, uint8_t* buf, size_t cap, size_t& outLen);

/** Write len bytes to a file from the start (overwrites existing content). */
bool writeFile(const char* path, const uint8_t* buf, size_t len);

/** Remove a file. @return true if removed */
bool removeFile(const char* path);

#if defined(NATIVE_TEST_BUILD)
// Test-only controls implemented by fs_backend_mock.cpp
namespace mock {
void reset();                    // clear all files, restore begin() = true
void setBeginResult(bool ok);    // force begin() to fail (storage unavailable)
}
#endif

} // namespace fsb

#endif // FS_BACKEND_H
