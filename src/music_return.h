#pragma once
#include <cstdint>

// Own native title music until custom playback has ended and five full seconds
// of silence have elapsed. Callers keep ownership if the native receiver is absent.
class MusicReturn {
    bool owned_ = false;
    bool ending_ = false;
    bool silenceStarted_ = false;
    std::uint64_t silenceAt_ = 0;
public:
    void begin() { owned_ = true; ending_ = false; silenceStarted_ = false; }
    void finish() { if (owned_) ending_ = true; }
    void cancel() { owned_ = ending_ = silenceStarted_ = false; }
    bool blocked() const { return owned_; }
    bool ready(bool customActive, std::uint64_t now) {
        if (!owned_ || !ending_) return false;
        if (customActive) { silenceStarted_ = false; return false; }
        if (!silenceStarted_) { silenceAt_ = now; silenceStarted_ = true; }
        return now - silenceAt_ >= 5000;
    }
};
