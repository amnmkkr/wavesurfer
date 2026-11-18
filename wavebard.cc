// wavebard.cc - WAVEBARD: Bastl Instruments' Kastle 2 Wave Bard sample player (Vaclav Mach, Marek Mach; MIT:
// LICENSE.kastle2; github.com/bastl-instruments/kastle2, code/src/apps/WaveBard, kept unchanged in third_party/kastle2)
// as a Plugin API 3 user machine (sdk/rmp/rmp_api3.h), stereo, 48 kHz, playing the project's pool samples.
//   SRC page   P1 SMPL  the sample in the bank (Wave Bard's Sample pot)
//              P2 PTCH  free pitch, 1/4x..4x (Wave Bard's Pitch pot)
//              P3 LEN   length: right of centre forward, longer to the right; left of centre backwards
//              P4 BANK  the bank, 1..16
//              P5 FX    left: delay (synced to the Rytm's tempo), right: chorus; harder, drive (Wave Bard's FX)
//              P6 FILT  Wave Bard's DJ filter: left low-pass, right high-pass
//              P7 SCAL  the quantizer's scale (Wave Bard's seven)
//              P8 ROOT  the quantizer's root
//   page 2     P9 FINE  +-1 semitone (Wave Bard's Fine)
//              P10 TRCK how the trig's note moves the pitch: centre none, right +1 V/oct (the start), left inverted
//              P11 VSMP the trig's velocity picks a later sample: right of centre also plays on a change
//              P12 VLEV the trig's velocity sets the level (0: Wave Bard's own, every hit the same)
//              P13 FRST the first pool slot of bank 1
//              P14 SIZE samples a bank, 1..16: bank b, sample n plays pool slot FRST + (b - 1) x SIZE + (n - 1)
//              P15 TUNE +-24 semitones on the trig's note
//              P16 STRT where in the sample a trig starts (0: its start; taken at each trig, so it can be locked
//                       per step: a vocal phrase played from its words)
// What runs: Wave Bard's own AppWaveBard (Init, AudioLoop, its second core's FX, UiLoop, its parameter maps) and the
// Kastle 2 DSP library it uses, unchanged, at the Kastle's 44 kHz in its 48-sample blocks. Around it:
//   - the samples: a virtual Wave Bard file (Wave Bard's own loader reads it: 16 banks of up to 16 samples, Wave
//     Bard's seven scales, 48 kHz) whose samples are pool slots, read with pool_read (shim/common/dsp/sampling)
//   - a trig: the Kastle's trigger input, with the trig's note on the stepped pitch CV (1 V/oct, 60 = 0 V) and its
//     velocity on the sample CV, as a sequencer patched into a Kastle would play it
//   - the knobs: each Wave Bard knob (whatever layer it sits on) is a Sidecar knob, read by Bastl's own FancyPot
//   - the rate: 44 kHz out, 12/11 to 48 kHz (one 924-tap low-pass at 528 kHz, flat to 19 kHz, 73 dB down from
//     22 kHz: resample_taps.h)
// The Kastle platform Wave Bard calls is global (Kastle2::hw and the rest), and the Pi renders voices 0-3 and 4-7 on
// two cores at once: each voice keeps its own copy of it and runs Wave Bard under one lock.
// The heap Wave Bard's Init uses (its delay lines: 2 x 22000 samples, its knobs, the file) is the voice's arena.
// MIT, Copyright 2026 amnmkkr (LICENSE). Wave Bard and the Kastle 2 code: Copyright 2023-2025 Bastl Instruments, MIT.
#include <new>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "rmp/rmp_api3.h"
#include "resample_taps.h"

#define private public   // the glue sets Wave Bard's knobs and its samples' sizes (AppWaveBard is unchanged)
#include "AppWaveBard.hpp"
#undef private

using kastle2::AppWaveBard;
using kastle2::Hardware;
using kastle2::Kastle2;

// ---- the heap Wave Bard's Init allocates from: the voice's arena
namespace {
uint8_t* g_heap = nullptr;
uint8_t* g_heap_end = nullptr;
bool g_heap_short = false;
void* heap_alloc(size_t n) {
  uint8_t* p = (uint8_t*)(((uintptr_t)g_heap + 15u) & ~(uintptr_t)15u);
  if (!g_heap || p + n > g_heap_end) {
    g_heap_short = true;
    return nullptr;
  }
  g_heap = p + n;
  return p;
}
}  // namespace

#ifdef RMP_NATIVE
#include <cstdlib>
// The native test build: the arena during Init, malloc for the test host otherwise.
void* operator new(size_t n) {
  if (g_heap) { void* p = heap_alloc(n); if (p) return p; }
  void* p = malloc(n ? n : 1);
  if (!p) abort();
  return p;
}
void* operator new[](size_t n) { return operator new(n); }
static bool in_arena(void* p);
void operator delete(void* p) noexcept { if (p && !in_arena(p)) free(p); }
void operator delete(void* p, size_t) noexcept { operator delete(p); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete[](void* p, size_t) noexcept { operator delete(p); }
#else
void* operator new(size_t n) { return heap_alloc(n); }
void* operator new[](size_t n) { return heap_alloc(n); }
void operator delete[](void*) noexcept {}
void operator delete[](void*, size_t) noexcept {}
#endif

// ---- what the shims read
namespace kastle2 {
const uint8_t* g_user_data = nullptr;   // the virtual Wave Bard file during Init
uint64_t g_time_us = 0;                 // the running voice's clock
PoolReadFn g_pool_read = nullptr;
namespace MultiCore { void (*sample_hook)(size_t) = nullptr; }
}  // namespace kastle2

namespace {

const int kBanks = 16, kMaxSamples = 16, kScales = 7;
const uint32_t kAppBytes = (uint32_t)((sizeof(AppWaveBard) + 63u) & ~(size_t)63u);
const uint32_t kHeapBytes = 2u * 22000u * sizeof(int16_t) + 49152u;   // the delay lines; the knobs, file, banks, room
const uint32_t kArenaBytes = kAppBytes + kHeapBytes;
const int kBlock = 48;   // Wave Bard's block at 44 kHz

// Wave Bard's seven scales (12 bits, the root in bit 0), as Bastl's sample file has them: minor chord, minor
// pentatonic, minor, chromatic, major, major pentatonic, major chord
const uint32_t kScaleBits[kScales] = {0x089u, 0x4A9u, 0x5ADu, 0xFFFu, 0xAB5u, 0x295u, 0x091u};

// a polyphase rational converter through 528 kHz: up by L, down by M, kResampleTaps (924 = L x taps)
template <int L, int M, int CH>
struct Converter {
  static constexpr int kTaps = 924 / L;
  float ring[CH][128];
  uint32_t written;     // inputs written
  uint32_t base;        // the newest input the next output uses
  int phase;            // its phase, 0..L-1
  void push(const float* x) {
    for (int c = 0; c < CH; ++c) ring[c][written & 127u] = x[c];
    ++written;
  }
  bool ready() const { return base < written; }
  void pull(float* y) {
    for (int c = 0; c < CH; ++c) {
      float acc = 0.0f;
      for (int k = 0; k < kTaps; ++k) acc += kResampleTaps[phase + k * L] * ring[c][(base - (uint32_t)k) & 127u];
      y[c] = acc * (float)L;
    }
    phase += M;
    while (phase >= L) { phase -= L; ++base; }
  }
};

enum {
  P_SMPL, P_PTCH, P_LEN, P_BANK, P_FX, P_FILT, P_SCAL, P_ROOT,
  P_FINE, P_TRCK, P_VSMP, P_VLEV, P_FRST, P_SIZE, P_TUNE, P_STRT
};
const float kDefaults[16] = {0, 64, 127, 0, 64, 64, 64, 0, 64, 127, 64, 0, 1, 60, 64, 0};

struct PoolEntry {
  uint32_t frames;     // 0: no sample (yet), or none
  float step;
  uint32_t channels;
  bool empty;          // pool_info said EMPTY: no sample in the slot
};

struct State {
  AppWaveBard* app;                // in the arena; NULL until the first render made it, or without one (silent)
  uint8_t* arena;                  // the arena voice_attach gave, made into the app at the first render
  bool init_failed;
  kastle2::PoolSample* refs;       // [kBanks][kMaxSamples], in the arena
  float p[16];
  Hardware hw;                     // this voice's Kastle hardware: its knobs, CVs and trigger
  uint64_t samples44;              // 44 kHz samples rendered (the voice's clock)
  uint32_t target_ticks;           // audio blocks a quarter note
  // trigs (note_on, on the control core)
  uint32_t trig_seq;
  float trig_note, trig_vel;
  uint32_t trig_seen;
  bool trig_high;                  // the trigger input was high in the last block
  bool trig_waiting;               // a trig waits for the input to go low first
  float note_cv, vel_cv;
  float gain, gain_target;
  // the rate
  Converter<12, 11, 2> up;         // 44 -> 48
  float out48[128][2];
  uint32_t out_r, out_w;
  // the pool
  // The Pi keeps one pool sample ready for a voice: the one it last asked pool_info about. So only one slot is asked
  // for: the playing sample's, else the one the next trig will play.
  PoolEntry pool[128];             // what pool_info last said of each slot
  int want;                        // the slot asked for
  int ready;                       // the slot pool_info last answered OK (0 none)
  uint32_t trig_wait;              // Wave Bard blocks a trig has waited for its sample
  int first, size;                 // the mapping in use
  float start;                     // the start point in use (0..1 of a sample), taken at each trig
};
static_assert(sizeof(State) <= 65536, "the state fits the Pi's 64 KB");

// ---- one lock around Wave Bard: the Kastle platform it calls is global
uint32_t g_lock = 0;
struct Lock {
  Lock() {
    for (uint32_t i = 0; i < 50000u; ++i)   // (a voice that faulted holding it: go on after ~1 ms)
      if (!__atomic_exchange_n(&g_lock, 1u, __ATOMIC_ACQUIRE)) return;
  }
  ~Lock() { __atomic_store_n(&g_lock, 0u, __ATOMIC_RELEASE); }
};

AppWaveBard* g_app = nullptr;      // the app the second core's hook steps
void second_core(size_t i) { g_app->SecondCoreProcess(i); }

#ifdef RMP_NATIVE
}  // namespace
static uint8_t* g_arena_lo[64];
static uint8_t* g_arena_hi[64];
static int g_arenas = 0;
static bool in_arena(void* p) {
  for (int i = 0; i < g_arenas; ++i) if ((uint8_t*)p >= g_arena_lo[i] && (uint8_t*)p < g_arena_hi[i]) return true;
  return false;
}
namespace {
#endif

rmp_pool_info_fn g_pool_info = nullptr;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
// a knob 0..127 to Bastl's pot 0..4095, 64 exactly at Bastl's centre (POT_HALF)
inline int32_t pot_of(float v) {
  v = clampf(v, 0.0f, 127.0f);
  const float x = v <= 64.0f ? v * (2047.0f / 64.0f) : 2047.0f + (v - 64.0f) * (2048.0f / 63.0f);
  return (int32_t)(x + 0.5f);
}
inline int zone_of(float v, int n) {   // a list knob's entry, as the Sidecar shows it
  int k = (int)(clampf(v, 0.0f, 127.0f) * (float)n / 128.0f);
  return k < 0 ? 0 : (k >= n ? n - 1 : k);
}
// the pot value Bastl's FancyPot maps (sticky_map over 0..4095) to entry k of n, after its centre dead zone if it has
// one (kMapDeadzone: 0.45..0.55 is the centre)
int32_t pot_for_entry(int k, int n, bool deadzone) {
  if (k <= 0) return 0;
  const float t = ((float)k + 0.5f) * 4096.0f / (float)n;
  if (!deadzone) return (int32_t)t;
  const float half = 2047.0f, lo = 0.45f * 4095.0f, hi = 0.55f * 4095.0f;
  if (t < half) return (int32_t)(t * lo / half);
  if (t > half) return (int32_t)(hi + (t - half) * (4095.0f - hi) / (4095.0f - half));
  return 2047;
}
inline int first_of(const float* p) { int f = (int)(clampf(p[P_FRST], 0.0f, 127.0f)); return f < 1 ? 1 : f; }
inline int size_of(const float* p) { return 1 + zone_of(p[P_SIZE], kMaxSamples); }
inline int bank_of(const float* p) { return zone_of(p[P_BANK], kBanks); }

// ---- the virtual Wave Bard file (WAVE_BARD_FORMAT.md): a header, the scales, one rhythm, 16 banks of 16 empty
// samples. Its samples' sizes and data pointers are set from the pool afterwards.
uint8_t* build_file() {
  const uint32_t bytes = 20u + 4u * kScales + 4u + (uint32_t)kBanks * (12u + 16u * (uint32_t)kMaxSamples) + 4u;
  uint8_t* f = (uint8_t*)heap_alloc(bytes);
  if (!f) return nullptr;
  memset(f, 0, bytes);
  memcpy(f, "k2wb", 4);
  memcpy(f + 4, &bytes, 4);
  const uint32_t rate = 48000u;
  memcpy(f + 8, &rate, 4);
  f[12] = 16;                 // bit depth
  f[13] = kBanks;
  f[14] = kMaxSamples;
  f[15] = kScales;
  f[16] = 1;                  // rhythms (the Kastle's sequencer: not used here)
  f[17] = 16;                 // sequencer length
  uint8_t* q = f + 20;
  memcpy(q, kScaleBits, 4u * kScales);
  q += 4u * kScales;
  const uint32_t rhythm = 0x8888u;
  memcpy(q, &rhythm, 4);
  q += 4;
  for (int b = 0; b < kBanks; ++b) {
    q[0] = 'B', q[1] = 'A', q[2] = 'N', q[3] = 'K';
    q += 12;
    for (int n = 0; n < kMaxSamples; ++n) {
      q[4] = 1;               // size 0, mono
      q += 16;
    }
  }
  memcpy(q, "ahoj", 4);
  return f;
}

// a bank's sample from the pool table, as Wave Bard reads it
void apply_ref(State* s, int b, int n) {
  kastle2::PoolSample& r = s->refs[b * kMaxSamples + n];
  kastle2::WaveBardSample& w = s->app->samples_.banks[b].samples[n];
  const int slot = s->first + b * s->size + n;
  const PoolEntry* e = (slot >= 1 && slot <= 127) ? &s->pool[slot] : nullptr;
  if (e && e->frames > 0 && e->step > 0.0f) {
    r.slot = slot;
    r.frames = e->frames;
    r.step = e->step;
    r.channels = e->channels == 2 ? 2u : 1u;
    r.start = (uint32_t)(s->start * (float)e->frames);
    if (r.start >= e->frames) r.start = e->frames - 1u;
    const uint32_t frames48 = (uint32_t)((float)(e->frames - r.start) / e->step);
    w.size = frames48 * 2u * r.channels;
    w.channels = (uint8_t)r.channels;
  } else {
    r.slot = 0;
    r.frames = 0;
    r.step = 1.0f;
    r.channels = 1;
    r.start = 0;
    w.size = 0;
    w.channels = 1;
  }
  w.data = (uint8_t*)&r;
}

// the mapping (FRST, SIZE): every bank's samples again
void apply_mapping(State* s, bool force = false) {
  const int f = first_of(s->p), z = size_of(s->p);
  if (f == s->first && z == s->size && !force) return;
  s->first = f;
  s->size = z;
  AppWaveBard* a = s->app;
  for (int b = 0; b < kBanks; ++b)
    for (int n = 0; n < kMaxSamples; ++n) apply_ref(s, b, n);
  // the bank size: Wave Bard's sample count and its Sample knob's steps
  a->samples_.num_samples = (uint8_t)z;
  auto* pot = a->pots_[AppWaveBard::Pot::SAMPLE].get();
  pot->config_.map_size = (size_t)z;
  pot->UpdateInternalMappedValue();
}

// the slot the next trig plays: Wave Bard's own choice (GetPatchedSample: the Sample knob's step, moved by the
// velocity's sample CV through VSMP), worked out from the knobs as they are now
int predicted_slot(const State* s) {
  const int z = s->size;
  const int32_t step = 4096 / z;
  int32_t mapped = pot_of(s->p[P_SMPL]) / step;
  if (mapped > z - 1) mapped = z - 1;
  const int32_t smp = kastle2::curve_map(pot_of(s->p[P_VSMP]), kastle2::kMapDeadzone);
  const int32_t amount = smp < kastle2::POT_HALF ? (kastle2::POT_HALF - smp) * 2 : (smp - kastle2::POT_HALF) * 2;
  const int32_t cv = (int32_t)(clampf(s->vel_cv, 0.0f, 1.0f) * (float)kastle2::ADC_4V) * amount / kastle2::POT_RANGE;
  const int32_t range = 2 * kastle2::ADC_4V + 1, steps = 2 * z + 1;
  int32_t mod = -z + (cv + kastle2::ADC_4V) / (range / steps);
  if (mod > z - 1) mod = z - 1;
  if (mod < -(z - 1)) mod = -(z - 1);
  const int n = (int)((z + mapped + mod) % z);
  return s->first + bank_of(s->p) * z + n;
}

// the slot the playing deck reads, 0 none
int playing_slot(const State* s) {
  const auto& pl = s->app->players_[s->app->active_player_];
  if (!pl.IsPlaying() || !pl.sample_.data) return 0;
  return ((const kastle2::PoolSample*)pl.sample_.data)->slot;
}

// ask pool_info about the slot wanted (once a render call)
void poll_pool(State* s) {
  const int slot = s->want;
  if (!g_pool_info || slot < 1 || slot > 127) { s->ready = 0; return; }
  rmp_pool_info info;
  PoolEntry e = {0, 1.0f, 1, false};
  const int32_t r = g_pool_info(slot, &info);
  if (r == RMP_POOL_OK && info.frames > 0 && info.rate > 1000.0f) {
    e.frames = info.frames;
    e.step = info.rate / 48000.0f;
    e.channels = info.channels == 2 ? 2u : 1u;
    s->ready = slot;
  } else {
    s->ready = 0;
    if (r != RMP_POOL_EMPTY) return;   // NOT_READY: being loaded, ask again
    e.empty = true;
  }
  PoolEntry& old = s->pool[slot];
  if (old.frames == e.frames && old.step == e.step && old.channels == e.channels && old.empty == e.empty) return;
  old = e;
  const int k = slot - s->first;
  if (k >= 0 && k / s->size < kBanks) apply_ref(s, k / s->size, k % s->size);
}

void set_knobs(State* s) {
  const float* p = s->p;
  Hardware& hw = s->hw;
  hw.SetPot(Hardware::Pot::POT_6, Hardware::Layer::NORMAL, pot_of(p[P_SMPL]));
  hw.SetPot(Hardware::Pot::POT_5, Hardware::Layer::NORMAL, pot_of(p[P_PTCH]));
  hw.SetPot(Hardware::Pot::POT_4, Hardware::Layer::NORMAL, pot_of(p[P_LEN]));
  hw.SetPot(Hardware::Pot::POT_1, Hardware::Layer::NORMAL, pot_of(p[P_TRCK]));
  hw.SetPot(Hardware::Pot::POT_2, Hardware::Layer::NORMAL, pot_of(p[P_VSMP]));
  hw.SetPot(Hardware::Pot::POT_2, Hardware::Layer::SHIFT, pot_of(p[P_FX]));
  hw.SetPot(Hardware::Pot::POT_6, Hardware::Layer::SHIFT, pot_of(p[P_FILT]));
  hw.SetPot(Hardware::Pot::POT_1, Hardware::Layer::MODE, pot_for_entry(zone_of(p[P_SCAL], kScales), kScales, true));
  hw.SetPot(Hardware::Pot::POT_2, Hardware::Layer::MODE, pot_for_entry(zone_of(p[P_ROOT], 12), 12, false));
  hw.SetPot(Hardware::Pot::POT_3, Hardware::Layer::MODE, pot_of(p[P_FINE]));
}

// the trig's note and velocity on Wave Bard's CV inputs (ADC_1V a volt; Bastl's attenuverter at its top passes
// 2047/2048 of it, made up here so the note plays in tune)
void set_cvs(State* s) {
  const float tune = (float)(int)((s->p[P_TUNE] - 64.0f) * (24.0f / 63.0f) + (s->p[P_TUNE] >= 64.0f ? 0.5f : -0.5f));
  const float semis = clampf(s->note_cv + tune - 60.0f, -48.0f, 48.0f);
  const float cv = semis * ((float)kastle2::ADC_1V / 12.0f) * (2048.0f / 2047.0f);
  s->hw.analog[(size_t)Hardware::AnalogInput::PITCH_2] = (int32_t)(cv < 0 ? cv - 0.5f : cv + 0.5f);
  s->hw.analog[(size_t)Hardware::AnalogInput::PARAM_1] = (int32_t)(clampf(s->vel_cv, 0.0f, 1.0f) * (float)kastle2::ADC_4V);
}

// One Wave Bard block (under the lock): this voice's platform in, AudioLoop, then UiLoop twice (on the Kastle UiLoop
// is the main loop and runs many times between blocks: the run after a trig reads its CVs), the platform out
void run_block(State* s) {
  // a new trig: the trigger input high for one block (low for one first if it was high)
  if (s->trig_seen != __atomic_load_n(&s->trig_seq, __ATOMIC_ACQUIRE)) {
    s->trig_seen = s->trig_seq;
    s->note_cv = s->trig_note;
    s->vel_cv = s->trig_vel;
    s->gain_target = 1.0f - clampf(s->p[P_VLEV], 0.0f, 127.0f) / 127.0f * (1.0f - clampf(s->vel_cv, 0.0f, 1.0f));
    s->trig_waiting = true;
    s->trig_wait = 0;
    set_cvs(s);
    const float start = clampf(s->p[P_STRT], 0.0f, 127.0f) / 128.0f;
    if (start != s->start) {
      s->start = start;
      apply_mapping(s, true);
    }
  }
  // the trig waits (13 ms at most) until the Sidecar has its sample ready, so it starts on its first frame
  bool go = false;
  if (s->trig_waiting && !s->trig_high) {
    const int slot = predicted_slot(s);
    go = s->ready == slot || slot > 127 || s->pool[slot].empty || s->trig_wait >= 12;
    ++s->trig_wait;
  }
  if (go) {
    s->hw.trigger_in = true;
    s->trig_waiting = false;
    s->trig_wait = 0;
  } else {
    s->hw.trigger_in = false;
  }
  s->trig_high = s->hw.trigger_in;

  kastle2::q15_t io_in[2 * kBlock] = {}, io_out[2 * kBlock];
  {
    Lock lock;
    Kastle2::hw = s->hw;
    Kastle2::base.clock.target_ticks = s->target_ticks;
    kastle2::g_time_us = s->samples44 * 1000000ull / 44000ull;
    g_app = s->app;
    kastle2::MultiCore::sample_hook = second_core;
    s->app->AudioLoop(io_in, io_out, kBlock);
    s->app->UiLoop();
    const bool triggered = s->app->triggered_;   // a trig played: its sample loaded, the deck switched
    s->app->UiLoop();
    // The run after a trig sets the new deck's speed and start point (backwards: the end of its length envelope),
    // after the trig started it from the last start point that deck had. No audio has played since: start it again
    // from the point just set (as the Kastle's main loop has it from the second hit on).
    if (triggered) s->app->players_[s->app->active_player_].Play();
  }
  s->samples44 += kBlock;
  for (int i = 0; i < kBlock; ++i) {
    const float y[2] = {io_out[2 * i] * (1.0f / 32768.0f), io_out[2 * i + 1] * (1.0f / 32768.0f)};
    s->up.push(y);
    while (s->up.ready()) {
      float z[2];
      s->up.pull(z);
      s->out48[s->out_w & 127u][0] = z[0];
      s->out48[s->out_w & 127u][1] = z[1];
      ++s->out_w;
    }
  }
}

// the output: +3 dB over Wave Bard's level, and above 0.8 a
// soft knee to 1 instead of the host's hard clip
const float kOutGain = 1.4125375f;   // +3 dB
inline float soft_knee(float x) {
  const float a = x < 0.0f ? -x : x;
  if (a <= 0.8f) return x;
  const float e = expf(-2.0f * (a - 0.8f) / 0.2f);   // 0.8 + 0.2 tanh((a - 0.8) / 0.2)
  const float y = 0.8f + 0.2f * (1.0f - e) / (1.0f + e);
  return x < 0.0f ? -y : y;
}

// Flush-to-zero while Wave Bard renders (AArch64 FPCR.FZ): its float filters decay towards zero
struct FlushToZero {
#if defined(__aarch64__)
  uint64_t saved;
  FlushToZero() {
    __asm__ volatile("mrs %0, fpcr" : "=r"(saved));
    __asm__ volatile("msr fpcr, %0" : : "r"(saved | (1ull << 24)));
  }
  ~FlushToZero() { __asm__ volatile("msr fpcr, %0" : : "r"(saved)); }
#endif
};

int32_t c_init(const rmp_host* h) {
  if (h->sample_rate != 48000.0f) return 1;
  g_pool_info = rmp_pool_info_of(h);
  kastle2::g_pool_read = (kastle2::PoolReadFn)rmp_pool_read_of(h);
  return 0;
}

void c_voice_init(void* st) {
  State* s = (State*)st;
  memset(s, 0, sizeof *s);
  memcpy(s->p, kDefaults, sizeof s->p);
  s->target_ticks = 917;   // 120 BPM until the transport says
  s->note_cv = 60.0f;
  s->gain = s->gain_target = 1.0f;
  set_knobs(s);
  set_cvs(s);
}

void c_params(void* st, const float* p) {
  State* s = (State*)st;
  memcpy(s->p, p, sizeof s->p);   // applied in render (the voice's own thread)
}

void c_note_on(void* st, float note, float velocity) {
  State* s = (State*)st;
  s->trig_note = note;
  s->trig_vel = velocity;
  __atomic_store_n(&s->trig_seq, s->trig_seq + 1u, __ATOMIC_RELEASE);
}

void c_note_off(void*) {}   // Wave Bard plays a trig's sample through its length envelope

// voice_attach only notes the arena: Wave Bard's Init runs at the voice's first render (a control call must take
// under 0.5 ms on the Pi)
void c_attach(void* st, const rmp3_voice* v) {
  State* s = (State*)st;
  s->app = nullptr;
  s->init_failed = false;
  s->arena = (v->arena && v->arena_bytes >= kArenaBytes) ? (uint8_t*)v->arena : nullptr;   // no room: silent
}

// Wave Bard made in the arena: the virtual file, Init, the samples' references, the knobs read
void make_app(State* s) {
  uint8_t* a = s->arena;
  memset(a, 0, kArenaBytes);   // (the arena may hold an earlier machine's bytes)
#ifdef RMP_NATIVE
  if (g_arenas < 64) g_arena_lo[g_arenas] = a, g_arena_hi[g_arenas] = a + kArenaBytes, ++g_arenas;
#endif
  Lock lock;
  g_heap = a + kAppBytes;
  g_heap_end = a + kArenaBytes;
  g_heap_short = false;
  s->refs = (kastle2::PoolSample*)heap_alloc(sizeof(kastle2::PoolSample) * kBanks * kMaxSamples);
  kastle2::g_user_data = build_file();
  AppWaveBard* app = nullptr;
  if (s->refs && kastle2::g_user_data) {
    Kastle2::hw = s->hw;
    kastle2::g_time_us = 0;
    app = new (a) AppWaveBard;
    app->Init();
  }
  kastle2::g_user_data = nullptr;
  g_heap = g_heap_end = nullptr;
  if (!app || g_heap_short || !app->inited_) {   // (cannot happen with kHeapBytes; silent if it did)
    s->init_failed = true;
    return;
  }
  s->app = app;
  s->first = s->size = -1;
  apply_mapping(s);
  set_knobs(s);
  app->bank_select_.Set((uint32_t)bank_of(s->p));
  Kastle2::hw = s->hw;
  g_app = app;
  kastle2::MultiCore::sample_hook = second_core;
  app->UiLoop();   // the knobs read
  app->UiLoop();
}

void c_render(void* st, const rmp3_block* b) {
  State* s = (State*)st;
  const int32_t n = b->n < 0 ? 0 : (b->n > 16 ? 16 : b->n);
  float* l = b->out[0];
  float* r = (b->flags & RMP3_B_MONO_OUT) ? nullptr : b->out[1];
  if (b->p) memcpy(s->p, b->p, sizeof s->p);
  if (!s->app && s->arena && !s->init_failed) {
    set_knobs(s);
    make_app(s);
  }
  if (!s->app) {
    for (int32_t i = 0; i < n; ++i) l[i] = 0.0f;
    if (r) for (int32_t i = 0; i < n; ++i) r[i] = 0.0f;
    return;
  }
  set_knobs(s);
  apply_mapping(s);
  s->app->bank_select_.Set((uint32_t)bank_of(s->p));
  {
    const int playing = playing_slot(s);
    s->want = (s->trig_waiting || !playing) ? predicted_slot(s) : playing;
  }
  poll_pool(s);
  const rmp3_transport* t = b->transport;
  if (t && t->tick_us > 1000.0f) {
    const double blocks = (double)t->tick_us * 24.0 * 44000.0 / 1e6 / kBlock;
    s->target_ticks = (uint32_t)(blocks + 0.5);
  }
  FlushToZero ftz;
  const float glide = 1.0f / 240.0f;   // the velocity level: 5 ms
  for (int32_t i = 0; i < n; ++i) {
    if (s->out_r >= s->out_w) run_block(s);
    float yl = s->out48[s->out_r & 127u][0], yr = s->out48[s->out_r & 127u][1];
    ++s->out_r;
    s->gain += (s->gain_target - s->gain) * glide;
    yl = soft_knee(yl * s->gain * kOutGain), yr = soft_knee(yr * s->gain * kOutGain);
    if (r) l[i] = yl, r[i] = yr;
    else l[i] = 0.5f * (yl + yr);   // a mono route: the mix
  }
}

const rmp3_plugin kPlugin3 = {
    RMP3_MAGIC, (uint32_t)sizeof(rmp3_plugin), RMP3_MINOR,
    RMP3_KIND_MACHINE,
    RMP3_CAP_ARENA,                                          // needs: without an arena it cannot play
    RMP3_CAP_GUARD | RMP3_CAP_STEREO | RMP3_CAP_TRANSPORT | RMP3_CAP_BLOCK_P,   // wants
    0, 0,                                                    // shared_min, shared_want
    kArenaBytes, kArenaBytes,                                // voice_min, voice_want
    8,                                                       // states: the 8 voices
    0,                                                       // latency_frames
    2, 0, 0,                                                 // out_channels, in_channels, aux_channels
    RMP3_AT_VOICE,
    0, 0, {0, 0, 0, 0, 0, 0},
    nullptr, nullptr,
    nullptr,                                                 // setup: nothing shared
    c_attach, c_render, nullptr,
    {nullptr, nullptr, nullptr, nullptr},
};

}  // namespace

RMP_ENTRY const rmp_plugin rmp_entry = {
    RMP_PLUGIN_MAGIC,
    RMP_API3,                                                // API 3 only: the arena is not optional
    (uint32_t)sizeof(State),
    80,
    c_init, c_voice_init, c_note_on, c_note_off, c_params,
    nullptr,                                                 // no API 2 render
    {nullptr, nullptr, nullptr, nullptr, RMP_FN(&kPlugin3), nullptr, nullptr, nullptr},
};
