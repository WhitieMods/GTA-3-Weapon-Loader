// Minimal software mixer that plays decoded OGG/Vorbis PCM (see core/ogg_vorbis.hpp) through the Windows waveOut API.
// Input is raw float PCM from our own Vorbis decoder: there is no WAV container, WAV parsing, or WAV fallback anywhere.
// The player's weapon is always at the listener position, so plain stereo output matches what a positional engine
// sample would do for these events. Volume follows the game's own SFX slider (see runtime.hpp).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>
#include "../core/mixer_core.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

namespace wl {
class Mixer {
public:
    static constexpr int kRate = 44100, kFrames = 512, kBuffers = 4, kMaxVoices = 24;

    bool start() {
        if (running_) return true;
        InitializeCriticalSection(&cs_);
        WAVEFORMATEX f{}; f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = kRate; f.wBitsPerSample = 16;
        f.nBlockAlign = (WORD)(f.nChannels * f.wBitsPerSample / 8); f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        event_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!event_ || waveOutOpen(&hwo_, WAVE_MAPPER, &f, (DWORD_PTR)event_, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
        for (int i = 0; i < kBuffers; ++i) {
            std::memset(&hdr_[i], 0, sizeof(WAVEHDR));
            hdr_[i].lpData = (LPSTR)buf_[i]; hdr_[i].dwBufferLength = sizeof(buf_[i]);
            waveOutPrepareHeader(hwo_, &hdr_[i], sizeof(WAVEHDR));
            std::memset(buf_[i], 0, sizeof(buf_[i])); waveOutWrite(hwo_, &hdr_[i], sizeof(WAVEHDR));
        }
        running_ = true;
        thread_ = CreateThread(nullptr, 0, &Mixer::threadMain, this, 0, nullptr);
        return thread_ != nullptr;
    }

    // gain: 0..~4. Safe to call from the game thread at any time.
    void play(const std::shared_ptr<const Pcm>& pcm, float gain) {
        if (!running_ || !pcm || pcm->channels < 1 || pcm->samples.empty() || gain <= 0.f) return;
        EnterCriticalSection(&cs_);
        if ((int)voices_.size() >= kMaxVoices) voices_.erase(voices_.begin());   // drop the oldest voice
        voices_.push_back({pcm, 0.0, gain});
        LeaveCriticalSection(&cs_);
    }

private:
    CRITICAL_SECTION cs_{};
    HWAVEOUT hwo_ = nullptr; HANDLE event_ = nullptr, thread_ = nullptr; volatile bool running_ = false;
    WAVEHDR hdr_[kBuffers]; int16_t buf_[kBuffers][kFrames * 2];
    std::vector<Voice> voices_;

    static DWORD WINAPI threadMain(LPVOID p) { static_cast<Mixer*>(p)->loop(); return 0; }
    void loop() {
        for (;;) {
            WaitForSingleObject(event_, 100);
            for (int i = 0; i < kBuffers; ++i) {
                if (!(hdr_[i].dwFlags & WHDR_DONE)) continue;
                fill(buf_[i]);
                hdr_[i].dwFlags &= ~WHDR_DONE;
                waveOutWrite(hwo_, &hdr_[i], sizeof(WAVEHDR));
            }
        }
    }
    void fill(int16_t* out) {
        float mix[kFrames * 2]; std::memset(mix, 0, sizeof mix);
        EnterCriticalSection(&cs_);
        mixVoices(voices_, mix, kFrames, kRate);
        LeaveCriticalSection(&cs_);
        for (int i = 0; i < kFrames * 2; ++i) out[i] = toPcm16(mix[i]);
    }
};
} // namespace wl
