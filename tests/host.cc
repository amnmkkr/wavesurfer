// tests/host.cc - drives wavesurfer.cc as the Pi's API 3 host drives a machine (an arena, 12-frame blocks, stereo out,
// b->p, the transport, note_on per trig, the pool through pool_info/pool_read: the first ask for a slot answers
// NOT_READY, then its frames as 16-bit mono, as the Pi's sample RAM holds them) and renders tests/wav/*.wav from the
// test kit (tests/kit, made by make_kit.py: pool slots 1..8 = pluck, kick, snare, bell, vox, pad, bass, hat; 9 a sung
// phrase, four syllables of 0.8 s on C3 Eb3 G3 F3).
// Also a stress test: knobs and trigs at random, two voices, NaN and level checks, the time a block takes.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rmp/rmp_api3.h"

extern "C" const rmp_plugin rmp_entry;
static const double SR = 48000.0;

// ---- the pool, as the Pi serves it (core/user/um.cpp h_pool_info / h_pool_read / upool_pass): one slot ready per
// voice, the one pool_info last asked about; asking another answers NOT_READY and the control core resolves it
// between blocks (here: 4 blocks, 1 ms, later); pool_read reads only the ready slot.
static std::vector<float> g_pool[128];
static float g_rate[128];
struct UPool { int slot, want, wait; };
static UPool g_up[8];
static int g_vnow = -1;
static long g_switches = 0;
static int32_t pool_info(int32_t slot, rmp_pool_info* info) {
  if (g_vnow < 0 || slot < 1 || slot > 127 || g_pool[slot].empty()) return RMP_POOL_EMPTY;
  UPool& u = g_up[g_vnow];
  if (u.slot == slot) {
    info->frames = (uint32_t)g_pool[slot].size();
    info->channels = 1;
    info->rate = g_rate[slot] > 0 ? g_rate[slot] : 48000.0f;
    info->where = RMP_POOL_PSRAM;
    info->head_frames = info->frames;
    return RMP_POOL_OK;
  }
  if (u.want != slot) u.want = slot, u.wait = 4;
  return RMP_POOL_NOT_READY;
}
static void upool_pass() {   // between blocks
  for (auto& u : g_up)
    if (u.want && --u.wait <= 0) { if (u.slot != u.want) ++g_switches; u.slot = u.want; u.want = 0; }
}
static long g_reads = 0;
static int32_t pool_read(int32_t slot, uint32_t pos, float* buf, int32_t n) {
  ++g_reads;
  if (g_vnow < 0 || g_up[g_vnow].slot != slot) return 0;
  const auto& x = g_pool[slot];
  int32_t k = 0;
  for (; k < n && pos + (uint32_t)k < x.size(); ++k) buf[k] = x[pos + k];
  return k;
}
static void load_kit() {
  for (int s = 1; s <= 9; ++s) {
    char path[64];
    snprintf(path, sizeof path, "kit/%02d.f32", s);
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no %s: run make_kit.py\n", path); exit(1); }
    float v;
    while (fread(&v, 4, 1, f) == 1) g_pool[s].push_back(roundf(v * 32767.0f) / 32768.0f);   // as 16-bit
    fclose(f);
  }
}

static void write_wav(const std::string& path, const std::vector<float>& lr) {
  FILE* f = fopen(path.c_str(), "wb");
  const uint32_t frames = (uint32_t)(lr.size() / 2), bytes = frames * 4;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(bytes);
  for (float v : lr) {
    float y = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
    int16_t s = (int16_t)lrintf(y * 32767.f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

struct Trig { double t; float note, vel; };
struct Knob { double t; int p; float v; };
struct Res { float peak; int nan; double us_max, us_avg; std::vector<float> out; };

static const float kDefaults[16] = {0, 64, 127, 0, 64, 64, 64, 0, 64, 127, 64, 0, 1, 60, 64, 0};   // as manifest.json

static Res run(double secs, std::vector<Trig> trigs, std::vector<Knob> knobs, double bpm = 120.0, int voices = 1) {
  memset(g_up, 0, sizeof g_up);
  std::stable_sort(trigs.begin(), trigs.end(), [](const Trig& a, const Trig& b) { return a.t < b.t; });
  std::stable_sort(knobs.begin(), knobs.end(), [](const Knob& a, const Knob& b) { return a.t < b.t; });
  const rmp3_plugin* p3 = (const rmp3_plugin*)rmp_entry.reserved[RMP_P_V3];
  rmp_host h = {};
  h.api = 2; h.sample_rate = 48000.0f; h.max_block = 12;
  h.reserved[RMP_H_POOL_INFO] = (void*)pool_info;
  h.reserved[RMP_H_POOL_READ] = (void*)pool_read;
  if (rmp_entry.init(&h) != 0) { fprintf(stderr, "init refused\n"); exit(1); }
  std::vector<void*> st(voices), ar(voices);
  std::vector<std::vector<float>> k(voices, std::vector<float>(kDefaults, kDefaults + 16));
  for (int v = 0; v < voices; ++v) {
    st[v] = aligned_alloc(64, (rmp_entry.state_bytes + 63) & ~63u);
    memset(st[v], 0, rmp_entry.state_bytes);
    ar[v] = aligned_alloc(64, (p3->voice_want + 63) & ~63u);
    memset(ar[v], 0x5a, p3->voice_want);   // (an earlier occupant's bytes)
    rmp_entry.voice_init(st[v]);
    for (auto& kn : knobs) if (kn.t < 0) k[v][kn.p - 1] = kn.v;
    rmp_entry.params(st[v], k[v].data());
    rmp3_voice vv = {};
    vv.arena = ar[v]; vv.arena_bytes = p3->voice_want; vv.slot = 1; vv.voice = v; vv.instance = -1;
    vv.place = RMP3_AT_VOICE;
    p3->voice_attach(st[v], &vv);
  }
  rmp3_transport tr = {};
  tr.bpm = (float)bpm; tr.tick_us = (float)(60e6 / (24.0 * bpm)); tr.song_tick = -1;
  float L[16], R[16];
  rmp3_block b = {};
  b.size = sizeof b; b.n = 12; b.out[0] = L; b.out[1] = R; b.place = RMP3_AT_VOICE; b.transport = &tr;
  Res res = {0, 0, 0, 0, {}};
  const size_t frames = (size_t)(secs * SR);
  res.out.assign(frames * 2, 0.0f);
  size_t ti = 0, ki = 0; double us_sum = 0; long blocks = 0;
  for (size_t f = 0; f + 12 <= frames; f += 12) {
    const double t = f / SR;
    while (ki < knobs.size() && knobs[ki].t <= t) {
      if (knobs[ki].t >= 0) for (int v = 0; v < voices; ++v) k[v][knobs[ki].p - 1] = knobs[ki].v;
      ++ki;
    }
    while (ti < trigs.size() && trigs[ti].t <= t) {
      rmp_entry.note_on(st[ti % voices], trigs[ti].note, trigs[ti].vel);
      ++ti;
    }
    upool_pass();
    for (int v = 0; v < voices; ++v) {
      b.p = k[v].data();
      g_vnow = v;
      const auto t0 = std::chrono::steady_clock::now();
      p3->render(st[v], &b);
      g_vnow = -1;
      const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
      us_sum += us; ++blocks;
      if (us > res.us_max) res.us_max = us;
      for (int i = 0; i < 12; ++i) {
        if (!(L[i] == L[i]) || !(R[i] == R[i])) { ++res.nan; continue; }
        res.out[2 * (f + i)] += L[i];
        res.out[2 * (f + i) + 1] += R[i];
        res.peak = fmaxf(res.peak, fmaxf(fabsf(L[i]), fabsf(R[i])));
      }
    }
  }
  res.us_avg = us_sum / (blocks ? blocks : 1);
  printf("  (pool: %ld slot switches)\n", g_switches); g_switches = 0;
  for (int v = 0; v < voices; ++v) free(st[v]), free(ar[v]);
  return res;
}

static double step_s(double bpm) { return 60.0 / bpm / 4.0; }
static void report(const char* name, const Res& r) {
  printf("%-22s peak %.3f  nan %d  block avg %.1f us  max %.1f us\n", name, r.peak, r.nan, r.us_avg, r.us_max);
  write_wav(std::string("wav/") + name + ".wav", r.out);
}
// the SMPL knob value that lands on sample n (0-based) of a bank of `size`
static float smpl(int n, int size = 8) { return (n + 0.5f) * 127.0f / size; }

int main() {
  const bool want_crash = getenv("CRASH") != nullptr; (void)want_crash;
  system("mkdir -p wav");
  load_kit();
  // crash checks (CRASH=seconds): a huge sample (4M frames) and the kit, two voices, trigs every 5..60 ms, every knob
  // jumping or stepping like a fast LFO, sample switching on most hits
  if (getenv("CRASH")) {
    const double secs = atof(getenv("CRASH"));
    g_pool[20].assign(4000000, 0.0f);
    for (size_t i = 0; i < g_pool[20].size(); ++i) g_pool[20][i] = 0.3f * sinf((float)i * 0.01f);
    g_pool[21] = g_pool[9];
    std::vector<Trig> tr; std::vector<Knob> kn;
    srand(77);
    for (double t = 0.05; t < secs; t += 0.005 + (rand() % 56) * 0.001) tr.push_back({t, (float)(rand() % 128), (rand() % 128) / 127.0f});
    for (double t = 0.01; t < secs; t += 0.002) {
      const int p = 1 + rand() % 16;
      float v = (float)(rand() % 128) + (rand() % 128) / 128.0f;
      if (p == 13) v = (float)(1 + rand() % 22);
      kn.push_back({t, p, v});
    }
    Res r = run(secs, tr, kn, 60.0 + rand() % 180, 2);
    printf("crash check: %.0f s, %zu trigs, %zu knob moves: peak %.3f, nan %d\n", secs, tr.size(), kn.size(), r.peak, r.nan);
    return r.nan ? 1 : 0;
  }
  const double pre = 0.25;   // the pool asked before the first trig (a project loaded)
  const double bpm = 120.0, st = step_s(bpm);

  // 1 a melody on the pluck (sample 1), the trig's note as the pitch
  {
    const float mel[] = {48, 51, 55, 58, 60, 58, 55, 51, 53, 56, 60, 63, 65, 63, 60, 56};
    std::vector<Trig> tr;
    for (int i = 0; i < 32; ++i) tr.push_back({pre + i * 2 * st, mel[i % 16] + 12, 0.8f});
    report("01_pluck_melody", run(pre + 32 * 2 * st + 1.5, tr, {{-1, 1, smpl(0)}}));
  }
  // 2 a beat: kick, snare, hat by SMPL locks (samples 2, 3, 8)
  {
    std::vector<Trig> tr; std::vector<Knob> kn;
    const char* pat = "K.h.S.h.K.KhS.hh";
    for (int bar = 0; bar < 4; ++bar)
      for (int s = 0; s < 16; ++s) {
        const char c = pat[s];
        if (c == '.') continue;
        const double t = pre + (bar * 16 + s) * st;
        const int n = c == 'K' ? 1 : (c == 'S' ? 2 : 7);
        kn.push_back({t - 0.002, 1, smpl(n)});
        tr.push_back({t, 60, 0.8f});
      }
    report("02_beat_smpl_locks", run(pre + 64 * st + 1.0, tr, kn));
  }
  // 3 the bell at PTCH low, centre, high (free pitch), then the TUNE knob +-12
  {
    std::vector<Trig> tr; std::vector<Knob> kn = {{-1, 1, smpl(3)}};
    const float ptch[] = {16, 64, 112};
    for (int i = 0; i < 3; ++i) { kn.push_back({pre + i * 1.5 - 0.01, 2, ptch[i]}); tr.push_back({pre + i * 1.5, 60, 0.8f}); }
    kn.push_back({pre + 4.5 - 0.01, 2, 64});
    kn.push_back({pre + 4.5 - 0.01, 15, 64 - 31.5f}); tr.push_back({pre + 4.5, 60, 0.8f});
    kn.push_back({pre + 6.0 - 0.01, 15, 64 + 31.5f}); tr.push_back({pre + 6.0, 60, 0.8f});
    report("03_bell_pitch_tune", run(pre + 8.0, tr, kn));
  }
  // 4 LEN: the vox forward long, forward short, then backwards (left of centre) short and long
  {
    std::vector<Trig> tr; std::vector<Knob> kn = {{-1, 1, smpl(4)}};
    const float len[] = {127, 100, 80, 40, 20, 0};
    for (int i = 0; i < 6; ++i) { kn.push_back({pre + i * 1.6 - 0.01, 3, len[i]}); tr.push_back({pre + i * 1.6, 60, 0.8f}); }
    report("04_vox_length_reverse", run(pre + 6 * 1.6 + 0.5, tr, kn));
  }
  // 5 the FX knob: the bass line dry, with the delay (left), with the chorus (right)
  {
    const float line[] = {36, 36, 48, 36, 39, 36, 43, 46};
    std::vector<Trig> tr; std::vector<Knob> kn = {{-1, 1, smpl(6)}, {-1, 3, 105}};
    const float fx[] = {64, 30, 8, 98, 122};
    for (int part = 0; part < 5; ++part) {
      kn.push_back({pre + part * 16 * st - 0.01, 5, fx[part]});
      for (int i = 0; i < 8; ++i) tr.push_back({pre + (part * 16 + i * 2) * st, line[i] + 12, 0.8f});
    }
    report("05_bass_fx_dry_delay_chorus", run(pre + 80 * st + 2.0, tr, kn));
  }
  // 6 FILT: the pad, the DJ filter swept low-pass (left) to high-pass (right)
  {
    std::vector<Trig> tr; std::vector<Knob> kn = {{-1, 1, smpl(5)}};
    for (int i = 0; i < 8; ++i) tr.push_back({pre + i * 1.0, 60, 0.8f});
    for (int i = 0; i <= 160; ++i) kn.push_back({pre + i * 0.05, 6, i <= 80 ? 64.0f - i * 0.8f : (i - 80) * 1.58f});
    report("06_pad_filter_sweep", run(pre + 8.5, tr, kn));
  }
  // 7 SCAL: random notes, chromatic, then minor pentatonic (the quantizer: the notes keep to the scale)
  {
    std::vector<Trig> tr; std::vector<Knob> kn = {{-1, 1, smpl(0)}, {-1, 3, 110}};
    srand(3);
    for (int i = 0; i < 32; ++i) tr.push_back({pre + i * 2 * st, (float)(48 + rand() % 24), 0.8f});
    kn.push_back({pre + 16 * 2 * st - 0.02, 7, (1 * 128 + 64) / 7.0f});   // MINPENT
    report("07_pluck_scale_chrom_minpent", run(pre + 64 * st + 1.0, tr, kn));
  }
  // 8 velocity: VSMP right picks a later sample the harder the hit (drums in slots 2, 3; bank size 2 from slot 2),
  // then VLEV the level
  {
    std::vector<Trig> tr;
    std::vector<Knob> kn = {{-1, 13, 2}, {-1, 14, 8}, {-1, 11, 127}, {-1, 1, smpl(0, 2)}};
    for (int i = 0; i < 16; ++i) tr.push_back({pre + i * 2 * st, 60, (i % 4 == 2) ? 1.0f : 0.3f});
    kn.push_back({pre + 32 * st - 0.02, 11, 64});
    kn.push_back({pre + 32 * st - 0.02, 12, 127});
    for (int i = 0; i < 16; ++i) tr.push_back({pre + (32 + i * 2) * st, 60, 0.2f + 0.8f * (i % 4) / 3.0f});
    report("08_velocity_sample_level", run(pre + 64 * st + 1.0, tr, kn));
  }
  // ---- vocals: the sung phrase (slot 9: FRST 9, a bank of one)
  const std::vector<Knob> V = {{-1, 13, 9}, {-1, 14, 0}};
  auto with = [](std::vector<Knob> a, std::vector<Knob> b) { a.insert(a.end(), b.begin(), b.end()); return a; };
  // 9 the phrase as it is, then a trig a bar with the note: up a fourth, down a tone (TRCK: 1 V/oct)
  {
    std::vector<Trig> tr = {{pre, 60, 0.8f}, {pre + 3.5, 65, 0.8f}, {pre + 7.0, 58, 0.8f}};
    report("09_vocal_phrase_notes", run(pre + 10.5, tr, V));
  }
  // 10 a vocal chop: STRT locked per step to the syllables (0, 1/4, 1/2, 3/4 of the phrase), short LEN, with a beat
  // pattern of starts
  {
    std::vector<Trig> tr; std::vector<Knob> kn = with(V, {{-1, 3, 96}});
    const int order[16] = {0, 0, 2, 1, 3, 0, 2, 2, 1, 0, 3, 3, 2, 1, 0, 3};
    for (int bar = 0; bar < 4; ++bar)
      for (int s = 0; s < 16; ++s) {
        if (s % 4 == 3 && bar % 2) continue;
        const double t = pre + (bar * 16 + s) * st;
        kn.push_back({t - 0.003, 16, order[s] * 32.0f + 1.0f});
        tr.push_back({t, 60, 0.8f});
      }
    report("10_vocal_chop_start_locks", run(pre + 64 * st + 1.0, tr, kn));
  }
  // 11 one syllable ("ha", STRT 0, LEN 100) as an instrument: random notes in minor pentatonic, with the delay
  {
    std::vector<Trig> tr; std::vector<Knob> kn = with(V, {{-1, 3, 104}, {-1, 7, (1 * 128 + 64) / 7.0f}, {-1, 5, 28}});
    srand(5);
    for (int i = 0; i < 24; ++i) tr.push_back({pre + i * 2 * st, (float)(55 + rand() % 14), 0.8f});
    report("11_vocal_syllable_minpent_delay", run(pre + 48 * st + 3.0, tr, kn));
  }
  // 12 backwards: the phrase reversed into the next bar (LEN 0), then with the chorus, then a high-pass sweep
  {
    std::vector<Trig> tr = {{pre, 60, 0.8f}, {pre + 3.5, 60, 0.8f}, {pre + 7.0, 60, 0.8f}};
    std::vector<Knob> kn = with(V, {{-1, 3, 0}, {pre + 3.4, 5, 100}});
    for (int i = 0; i <= 60; ++i) kn.push_back({pre + 7.0 + i * 0.05, 6, 64.0f + i * 1.0f});
    report("12_vocal_reverse_chorus_filter", run(pre + 10.5, tr, kn));
  }
  // an LFO on SMPL between hits (a step every 20 ms over the 8 kit samples), a hit every 0.5 s: the Sidecar should be
  // asked for a new sample only at the hits (the pool's slot switches printed above this line)
  {
    std::vector<Trig> tr; std::vector<Knob> kn;
    for (int i = 0; i < 16; ++i) tr.push_back({pre + i * 0.5, 60, 0.8f});
    for (int i = 0; i < 400; ++i) kn.push_back({pre + i * 0.02, 1, (float)((i * 37) % 128)});
    Res r = run(pre + 8.5, tr, kn);
    report("17_lfo_on_smpl_hits", r);
  }
  // gain: a 440 Hz sine at 0.25 (-12 dBFS) in slot 20, played at the defaults: the level out
  {
    for (int i = 0; i < 48000; ++i) g_pool[20].push_back(0.25f * sinf(2.0f * 3.14159265f * 440.0f * i / 48000.0f));
    Res r = run(pre + 1.0, {{pre, 60, 0.8f}}, {{-1, 13, 20}});
    float pk = 0;
    for (size_t i = (size_t)((pre + 0.3) * SR) * 2; i < (size_t)((pre + 0.6) * SR) * 2; ++i) pk = fmaxf(pk, fabsf(r.out[i]));
    printf("%-22s a 0.25 sine plays at %.3f (%.1f dB)\n", "gain", pk, 20 * log10f(pk / 0.25f));
    g_pool[20].clear();
  }
  // stress: two voices, trigs and knobs at random
  {
    std::vector<Trig> tr; std::vector<Knob> kn;
    srand(11);
    for (int i = 0; i < 400; ++i) tr.push_back({0.05 + i * 0.037, (float)(24 + rand() % 72), (rand() % 128) / 127.0f});
    for (int i = 0; i < 3000; ++i) {
      const int p = 1 + rand() % 16;
      kn.push_back({0.01 + i * 0.005, p, p == 13 ? (float)(1 + rand() % 9) : (float)(rand() % 128)});
    }
    Res r = run(16.0, tr, kn, 133.0, 2);
    printf("%-22s peak %.3f  nan %d  block avg %.1f us  max %.1f us  pool reads %ld\n", "stress_2_voices", r.peak, r.nan,
           r.us_avg, r.us_max, g_reads);
  }
  return 0;
}
