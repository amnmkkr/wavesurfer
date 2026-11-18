// shim/common/controls/FancyMode.hpp - Wave Bard's bank selector. On the Kastle it steps with the MODE button and
// follows CV and MIDI; here it holds the bank the Sidecar's BANK knob picks (the glue sets it). The Config is Bastl's.
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstdint>
#include "common/core/Hardware.hpp"
#include "common/core/Kastle2.hpp"
#include "common/utils.hpp"
namespace kastle2 {
class FancyMode {
 public:
  static constexpr uint32_t NO_MEMORY = 0xFFFFFFFF;
  static constexpr uint32_t NO_MIDI = 0xFF;
  enum class InputReading { NONE, CONTINUOUS, TRIGGERED };
  struct Config {
    uint32_t memory_addr = NO_MEMORY;
    uint32_t modes_count = 0;
    uint8_t midi_cc = NO_MIDI;
    uint8_t midi_attenuator_cc = NO_MIDI;
    uint8_t midi_output_cc = NO_MIDI;
    FancyPot::MidiNoteControl midi_note_control = {};
    Hardware::Pot attenuator_pot = Hardware::Pot::POT_4;
    Hardware::AnalogInput adc_input = Hardware::AnalogInput::MODE;
    Hardware::Layer attenuator_layer = Hardware::Layer::MODE;
    InputReading input_reading = InputReading::CONTINUOUS;
    InputReading midi_input_reading = InputReading::CONTINUOUS;
  };
  explicit FancyMode(Config config) : config_(config) {}
  void Init(uint32_t modes_count = 0) { if (modes_count) config_.modes_count = modes_count; }
  void TriggerAdcRead() {}
  void Process() {}
  void ReadValue() {}
  void MidiCallback(midi::Message *) {}
  uint32_t GetMode() const { return mode_; }
  void DisableNextChange() {}
  template <typename Container> void DisableNextChangeWhen(const Container &, uint32_t = 0) {}
  void SendMidi(bool = false) {}
  void Set(uint32_t m) { mode_ = m < config_.modes_count ? m : 0; }   // the glue
 private:
  Config config_;
  uint32_t mode_ = 0;
};
}  // namespace kastle2
