#pragma once

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace irrigation {

// MQTT payloads are not null-terminated, so the length is passed in.
// Accepts a plain number like "40" or "42.5" and nothing else.
inline bool parsePercent(const unsigned char* payload, size_t len, float* out) {
    if (payload == nullptr || out == nullptr || len == 0 || len > 15) return false;

    char buf[16];
    std::memcpy(buf, payload, len);
    buf[len] = '\0';

    char* end = nullptr;
    const float v = std::strtof(buf, &end);
    if (end == buf || *end != '\0') return false;
    if (!std::isfinite(v) || v < 0.0f || v > 100.0f) return false;

    *out = v;
    return true;
}

}  // namespace irrigation
