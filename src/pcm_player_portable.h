#pragma once
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_SSE2
#define MA_NO_AVX
#define MA_NO_AVX2
#define MA_NO_NEON
#include "miniaudio.h"
#include "audio_clock.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

// Dedicated PCM device; callback owns the consumed-frame cursor. Uninitializing
// the device joins its audio thread before the resource buffer can be freed.
class PcmPlayer {
    ma_device device_{};
    const unsigned char* samples_ = nullptr;
    std::uint64_t frameCount_ = 0;
    std::atomic<std::uint64_t> consumed_{0};
    std::atomic<bool> finished_{false};
    bool initialized_ = false;
    std::uint64_t fadeStart_ = 0;
    std::uint32_t fadeMs_ = 0;
    float initialVolume_ = 1;
    static void callback(ma_device* device, void* output, const void*, ma_uint32 count) {
        auto* self = static_cast<PcmPlayer*>(device->pUserData);
        std::memset(output, 0, std::size_t(count) * 4);
        const auto cursor = self->consumed_.load(std::memory_order_relaxed);
        const auto available = std::min<std::uint64_t>(count, self->frameCount_ - cursor);
        if (available) std::memcpy(output, self->samples_ + cursor * 4, std::size_t(available) * 4);
        self->consumed_.store(cursor + available, std::memory_order_release);
        if (cursor + available == self->frameCount_) self->finished_.store(true, std::memory_order_release);
    }
public:
    unsigned error = 0;
    bool queue(void* data, std::size_t bytes, std::uint32_t volume = 0xffffffff) {
        close();
        if (!data || !bytes || bytes % 4) { error = 1; return false; }
        samples_ = static_cast<const unsigned char*>(data); frameCount_ = bytes / 4;
        consumed_.store(0); finished_.store(false);
        auto config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16; config.playback.channels = 2;
        config.sampleRate = 44100; config.dataCallback = callback; config.pUserData = this;
        config.periodSizeInMilliseconds = 10;
        const auto result = ma_device_init(nullptr, &config, &device_);
        error = static_cast<unsigned>(result);
        if (result != MA_SUCCESS) return false;
        initialized_ = true;
        initialVolume_ = float(volume & 65535) / 65535.f;
        ma_device_set_master_volume(&device_, initialVolume_);
        return true;
    }
    bool play() {
        if (!initialized_) return false;
        const auto result = ma_device_start(&device_); error = static_cast<unsigned>(result);
        if (result != MA_SUCCESS) { close(); return false; }
        return true;
    }
    bool active() const { return initialized_ && !finished_.load(std::memory_order_acquire); }
    bool positionSeconds(double& seconds) const {
        if (!initialized_) return false;
        seconds = double(consumed_.load(std::memory_order_acquire)) / 44100.; return true;
    }
    void fade(std::uint32_t milliseconds) {
        if (!initialized_) return;
        if (!milliseconds) { close(); return; }
        fadeStart_ = audioClockMilliseconds(); fadeMs_ = milliseconds;
    }
    void update() {
        if (!initialized_) return;
        if (finished_.load(std::memory_order_acquire)) { close(); return; }
        if (fadeMs_) {
            const auto elapsed = audioClockMilliseconds() - fadeStart_;
            if (elapsed >= fadeMs_) { close(); return; }
            ma_device_set_master_volume(&device_, initialVolume_ * float(fadeMs_ - elapsed) / float(fadeMs_));
        }
    }
    void close() {
        if (initialized_) ma_device_uninit(&device_);
        initialized_ = false; samples_ = nullptr; frameCount_ = 0;
        consumed_.store(0); finished_.store(false); fadeStart_ = 0; fadeMs_ = 0;
    }
};
