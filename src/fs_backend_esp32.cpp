/**
 * @file fs_backend_esp32.cpp
 * @brief fs_backend implementation over arduino-esp32 LittleFS
 *
 * Requires a LittleFS ("spiffs"-type) partition in the partition table.
 */

#include "fs_backend.h"

#include <FS.h>
#include <LittleFS.h>

namespace fsb {

bool begin() {
    // true = format on first mount failure (fresh flash)
    return LittleFS.begin(true);
}

bool exists(const char* path) {
    return LittleFS.exists(path);
}

bool readFile(const char* path, uint8_t* buf, size_t cap, size_t& outLen) {
    fs::File file = LittleFS.open(path, "r");
    if (!file) return false;
    outLen = file.read(buf, cap);
    file.close();
    return true;
}

bool writeFile(const char* path, const uint8_t* buf, size_t len) {
    fs::File file = LittleFS.open(path, "w");  // "w" truncates
    if (!file) return false;
    size_t written = file.write(buf, len);
    file.close();
    return written == len;
}

bool removeFile(const char* path) {
    return LittleFS.remove(path);
}

} // namespace fsb
