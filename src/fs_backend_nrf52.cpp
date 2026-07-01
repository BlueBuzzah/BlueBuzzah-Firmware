/**
 * @file fs_backend_nrf52.cpp
 * @brief fs_backend implementation over Adafruit InternalFS (nRF52 internal flash)
 */

#include "fs_backend.h"

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

namespace fsb {

bool begin() {
    return InternalFS.begin();
}

bool exists(const char* path) {
    return InternalFS.exists(path);
}

bool readFile(const char* path, uint8_t* buf, size_t cap, size_t& outLen) {
    File file(InternalFS);
    if (!file.open(path, FILE_O_READ)) return false;
    outLen = file.read(buf, cap);
    file.close();
    return true;
}

bool writeFile(const char* path, const uint8_t* buf, size_t len) {
    File file(InternalFS);
    if (!file.open(path, FILE_O_WRITE)) return false;
    // FILE_O_WRITE positions at end-of-file; seek to start to overwrite
    file.seek(0);
    size_t written = file.write(buf, len);
    file.flush();  // Ensure data reaches flash before close
    file.close();
    return written == len;
}

bool removeFile(const char* path) {
    return InternalFS.remove(path);
}

} // namespace fsb
