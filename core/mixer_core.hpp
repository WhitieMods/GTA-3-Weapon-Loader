#define NOMINMAX
// Pure mixing logic for custom weapon sounds (host-tested). The Win32 plugin only feeds this to waveOut.
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include "ogg_vorbis.hpp"

namespace wl {
struct Voice { std::shared_ptr<const Pcm> pcm; double pos = 0.0; float gain = 1.f; };

// Adds 'frames' stereo frames of every voice into 'mix' (interleaved L,R floats, size frames*2), linear-resampling from
// each voice's sample rate to outRate, then drops voices that finished. Mono is centered, >2 channels use the first two.
inline void mixVoices(std::vector<Voice>& voices, float* mix, int frames, int outRate) {
    for (auto& v : voices) {
        const Pcm& p = *v.pcm; const double step = (double)p.rate / outRate; const size_t total = p.frames();
        for (int n = 0; n < frames; ++n) {
            size_t i0 = (size_t)v.pos;
            if (i0 + 1 >= total) { v.pos = (double)total; break; }
            float t = (float)(v.pos - (double)i0);
            auto smp = [&](size_t fr, int ch) { return p.samples[fr * (size_t)p.channels + (size_t)(std::min)(ch, p.channels - 1)]; };
            float l = smp(i0, 0) * (1 - t) + smp(i0 + 1, 0) * t;
            float r = p.channels > 1 ? smp(i0, 1) * (1 - t) + smp(i0 + 1, 1) * t : l;
            mix[n * 2] += l * v.gain; mix[n * 2 + 1] += r * v.gain; v.pos += step;
        }
    }
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return v.pos >= (double)v.pcm->frames() - 1; }), voices.end());
}
inline int16_t toPcm16(float x) { return (int16_t)std::clamp((int)(x * 32767.f), -32768, 32767); }
} // namespace wl
