// shim/common/core/Kastle2.hpp - the Kastle 2 platform as Wave Bard uses it, driven by the Sidecar glue:
//   base.GetClock(): the tempo (a quarter note of the Rytm's, in 44 kHz audio blocks), which Wave Bard's delay
//     syncs to
//   the layers: always NORMAL (every knob is a Sidecar knob of its own), the layer timers long past
//   base.GetLfoMod(): the Kastle's LFO modulates nothing (each knob reads as it is)
//   the sequencer, LEDs, MIDI out, the settings memory: no part in the sound here (the memory reads nothing back,
//     so every knob starts from Wave Bard's own start value)
// Every member is constant-initialized (no constructor runs at load: a plugin has none run).
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include "common/core/Hardware.hpp"
#include "common/dsp/math/qmath.hpp"
#include "common/dsp/utility/Sequencer.hpp"   // (its length limits: Wave Bard checks its file's)
namespace kastle2 {
class FancyPot;
class Memory {
 public:
  static constexpr size_t ADDR_APP_SPACE = 0x100;
  void Write8(size_t, uint8_t) {}
  bool Read8(size_t, uint8_t *) const { return false; }
  uint8_t Get8(size_t) const { return 0; }
  void QueueUpdate8(size_t, uint8_t) {}
};
class LfoMod {
 public:
  enum class Destination { DEFAULT = 0, NORMAL_1, NORMAL_2, NORMAL_3, NORMAL_4, NORMAL_5, NORMAL_6, MODE_1, MODE_2,
                           MODE_3, MODE_4, MODE_5, MODE_6, SHIFT_1, SHIFT_2, SHIFT_3, SHIFT_4, SHIFT_5, SHIFT_6, COUNT };
  int32_t AdjustedPotValue(FancyPot *pot) const;
  int32_t GetModValue(Destination) const { return 0; }
};
class Clock {
 public:
  enum class Sync { MIDI, EXTERNAL, INTERNAL, COUNT };
  uint32_t target_ticks = 917;    // audio blocks a quarter note (120 BPM until the glue sets it)
  uint32_t GetTargetTicks() const { return target_ticks; }
  uint32_t GetAverageTargetTicks() const { return target_ticks; }
  Sync GetSyncType() const { return Sync::EXTERNAL; }   // the Rytm's clock
};
class Base {
 public:
  enum class Feature { AUDIO_CHAIN, ENV_OUT, INPUT_INDICATION_CLIP, COUNT };
  struct SequencerShim {
    template <typename T> void SetTriggerGeneratorTable(T) {}
    void SetLength(size_t) {}
  };
  struct FakeBlinkerShim { void SetEnabled(bool) {} };
  Clock &GetClock() { return clock; }
  SequencerShim &GetSequencer() { return sequencer; }
  LfoMod &GetLfoMod() { return lfo_mod; }
  FakeBlinkerShim &GetFakeBlinker() { return blinker; }
  void SetFeatureEnabled(Feature, bool) {}
  void SetAllFeaturesEnabled(bool) {}
  void SetMaxVolume(uint8_t) {}
  void SetMidiOutPotEnabled(Hardware::Pot, bool) {}
  void SetLfoModDefaultSelectionEnabled() {}
  template <typename... T> void SetLfoModSelectionEnabled(bool, T...) {}
  size_t GetLayerTimer() const { return 1u << 30; }
  Hardware::Layer GetPrevLayer() const { return Hardware::Layer::NORMAL; }
  size_t GetPrevLayerTimer() const { return 1u << 30; }
  uint8_t GetSettingsLedPulse() const { return 0; }
  Clock clock;
  SequencerShim sequencer;
  LfoMod lfo_mod;
  FakeBlinkerShim blinker;
};
struct MidiShim {
  bool SendCc(uint8_t, uint8_t, bool = false) { return true; }
  bool SendNoteOn(uint8_t, uint8_t = 127) { return true; }
  bool SendNoteOff(uint8_t, uint8_t = 64) { return true; }
  bool SendPitchBend(int32_t, bool = false, uint32_t = 1) { return true; }
};
class Kastle2 {
 public:
  static inline Hardware hw;
  static inline Base base;
  static inline Memory memory;
  static inline MidiShim midi;
};
}  // namespace kastle2
#include "common/controls/FancyPot.hpp"
inline int32_t kastle2::LfoMod::AdjustedPotValue(FancyPot *pot) const { return pot->GetValue(); }
