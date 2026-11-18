// shim/common/dsp/sampling/PoolSample.hpp - where Wave Bard's samples come from on the Sidecar: the project's sample
// pool, read with the host's pool_read (rmp_api.h). A PoolSample is what a WaveBardSample's data pointer names here
// (the glue keeps one per bank and sample, in the voice's arena); a PoolCache is a SamplePlayer's window onto it.
// Wave Bard counts a sample's frames at 48 kHz (the glue's virtual Wave Bard file says so); a pool sample at another
// rate is read at the nearest of its own frames (step: its frames per 48 kHz frame). Wave Bard's frame 0 is the
// pool sample's frame `start`.
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstddef>
#include <cstdint>

namespace kastle2 {

struct PoolSample {
  int32_t slot;        // the pool slot, 1..127; 0: none
  uint32_t frames;     // its frames, as pool_info said
  float step;          // its frames per 48 kHz frame (1 for a 48 kHz sample)
  uint32_t channels;   // its channels, 1 or 2
  uint32_t start;      // the first frame Wave Bard plays (the glue's start point), in its own frames
};

// the host's pool_read (set by the glue at init; NULL: silence)
typedef int32_t (*PoolReadFn)(int32_t slot, uint32_t pos, float *buf, int32_t n);
extern PoolReadFn g_pool_read;

struct PoolCache {
  static constexpr uint32_t kFrames = 64;
  const PoolSample *ref = nullptr;
  int32_t slot = 0;
  uint32_t lo = 0, n = 0;            // frames lo..lo+n-1 of the source are in buf
  int16_t buf[2 * kFrames] = {};

  int16_t Get(const PoolSample *s, size_t frame, size_t c) {
    if (!s) return 0;
    const uint32_t src = s->start + (s->step == 1.0f ? (uint32_t)frame : (uint32_t)((float)frame * s->step + 0.5f));
    if (s != ref || s->slot != slot || src < lo || src - lo >= n) Fill(s, src);
    if (src < lo || src - lo >= n) return 0;
    const uint32_t ch = s->channels == 2 ? 2u : 1u;
    return buf[(src - lo) * ch + (c < ch ? c : 0)];
  }

  // kFrames from a little before src (Wave Bard also plays backwards); what the host does not deliver is silence
  void Fill(const PoolSample *s, uint32_t src) {
    ref = s;
    slot = s->slot;
    lo = src >= 8u ? src - 8u : 0u;
    n = kFrames;
    const uint32_t ch = s->channels == 2 ? 2u : 1u;
    int32_t got = 0;
    float tmp[2 * kFrames];
    if (g_pool_read && s->slot > 0 && lo < s->frames) {
      const uint32_t want = s->frames - lo < kFrames ? s->frames - lo : kFrames;
      got = g_pool_read(s->slot, lo, tmp, (int32_t)want);
      if (got < 0) got = 0;
      if (got > (int32_t)want) got = (int32_t)want;
    }
    const uint32_t have = (uint32_t)got * ch;
    for (uint32_t i = 0; i < 2 * kFrames; ++i) {
      float x = i < have ? tmp[i] * 32768.0f : 0.0f;
      x = x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x);
      buf[i] = (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
    }
  }
};

}  // namespace kastle2
