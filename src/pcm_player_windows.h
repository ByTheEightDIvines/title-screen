#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <cstdint>
#include <cstdio>

// Own a single Windows PCM voice. The caller retains the sample allocation
// until close(). No callback, worker thread, game sound objects or virtual calls.
class PcmPlayer {
    HWAVEOUT device_ = nullptr;
    WAVEHDR header_{};
    bool prepared_ = false;
    ULONGLONG fadeStart_ = 0;
    DWORD fadeMs_ = 0;
public:
    MMRESULT error = MMSYSERR_NOERROR;
    bool queue(void* data, size_t bytes, DWORD volume = 0xffffffff) {
        close();
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = 44100;
        format.wBitsPerSample = 16;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = 176400;
        error = waveOutOpen(&device_, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
        if (error != MMSYSERR_NOERROR) { device_ = nullptr; return false; }
        error = waveOutPause(device_);
        if (error != MMSYSERR_NOERROR) { close(); return false; }
        waveOutSetVolume(device_, volume);
        header_ = {};
        header_.lpData = static_cast<LPSTR>(data);
        header_.dwBufferLength = static_cast<DWORD>(bytes);
        error = waveOutPrepareHeader(device_, &header_, sizeof(header_));
        if (error != MMSYSERR_NOERROR) { close(); return false; }
        prepared_ = true;
        error = waveOutWrite(device_, &header_, sizeof(header_));
        if (error != MMSYSERR_NOERROR) { close(); return false; }
        return true;
    }
    bool play() {
        if (!device_) return false;
        error = waveOutRestart(device_);
        if (error != MMSYSERR_NOERROR) { close(); return false; }
        return true;
    }
    bool active() const { return device_ != nullptr && !(header_.dwFlags & WHDR_DONE); }
    bool positionSeconds(double& seconds) const {
        if (!device_) return false;
        MMTIME position{};
        position.wType = TIME_SAMPLES;
        if (waveOutGetPosition(device_, &position, sizeof(position)) != MMSYSERR_NOERROR)
            return false;
        switch (position.wType) {
        case TIME_SAMPLES: seconds = position.u.sample / 44100.0; return true;
        case TIME_BYTES: seconds = position.u.cb / 176400.0; return true;
        case TIME_MS: seconds = position.u.ms / 1000.0; return true;
        default: return false;
        }
    }
    void fade(DWORD milliseconds) {
        if (!device_) return;
        if (!milliseconds) { close(); return; }
        fadeStart_ = GetTickCount64();
        fadeMs_ = milliseconds;
    }
    void update() {
        if (!device_) return;
        if (header_.dwFlags & WHDR_DONE) { close(); return; }
        if (fadeMs_) {
            const auto elapsed = GetTickCount64() - fadeStart_;
            if (elapsed >= fadeMs_) { close(); return; }
            const DWORD gain = static_cast<DWORD>(65535ULL * (fadeMs_ - elapsed) / fadeMs_);
            waveOutSetVolume(device_, gain | (gain << 16));
        }
    }
    void close() {
        if (device_) {
            waveOutReset(device_);
            if (prepared_) waveOutUnprepareHeader(device_, &header_, sizeof(header_));
            waveOutClose(device_);
        }
        device_ = nullptr;
        prepared_ = false;
        header_ = {};
        fadeStart_ = 0;
        fadeMs_ = 0;
    }
};
