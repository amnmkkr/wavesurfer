// shim/common/core/App.hpp - Bastl's App interface, as FX Wizard implements it (the same virtuals).
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstddef>
#include <cstdint>
#include "common/dsp/math/qmath.hpp"
#include "common/core/midi/Message.hpp"
namespace kastle2 {
class App {
 public:
  virtual void Init() = 0;
  virtual void DeInit() = 0;
  virtual void AudioLoop(q15_t *input, q15_t *output, size_t size) = 0;
  virtual void UiLoop() = 0;
  virtual uint8_t GetId() = 0;
  virtual void MemoryInitialization() = 0;
  virtual void SecondCoreWorker() {}
  virtual void MidiCallback([[maybe_unused]] midi::Message *msg) {}
};
}  // namespace kastle2
