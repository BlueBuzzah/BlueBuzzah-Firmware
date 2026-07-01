/**
 * @file fs_backend_mock.cpp
 * @brief In-memory fs_backend implementation for native unit tests
 */

#include "fs_backend.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
std::map<std::string, std::vector<uint8_t>>& files() {
    static std::map<std::string, std::vector<uint8_t>> f;
    return f;
}
bool g_beginResult = true;
}

namespace fsb {

bool begin() {
    return g_beginResult;
}

bool exists(const char* path) {
    return files().count(path) != 0;
}

bool readFile(const char* path, uint8_t* buf, size_t cap, size_t& outLen) {
    auto it = files().find(path);
    if (it == files().end()) return false;
    outLen = it->second.size() < cap ? it->second.size() : cap;
    memcpy(buf, it->second.data(), outLen);
    return true;
}

bool writeFile(const char* path, const uint8_t* buf, size_t len) {
    files()[path].assign(buf, buf + len);
    return true;
}

bool removeFile(const char* path) {
    return files().erase(path) != 0;
}

namespace mock {
void reset() {
    files().clear();
    g_beginResult = true;
}
void setBeginResult(bool ok) {
    g_beginResult = ok;
}
}

} // namespace fsb
