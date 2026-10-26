#pragma once
#include <chrono>
#include <cstdint>
inline std::uint64_t audioClockMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
