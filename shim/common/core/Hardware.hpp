// shim/common/core/Hardware.hpp - the Kastle's hardware as Wave Bard reads it, driven by the Sidecar glue:
//   the CV inputs hold what the glue patches into them (PITCH 2, the stepped pitch: the trig's note at 1 V/oct;
//     PARAM 1, the sample CV: the trig's velocity), the others 0
//   the pots: each (pot, layer) a Wave Bard knob has is a Sidecar knob of its own, read on every pass whatever the
//     layer (the Kastle reads a pot only on the layer held); a (pot, layer) the Sidecar has no knob for is never read,
//     so Bastl's FancyPot keeps its start value or what Wave Bard forces into it
//   the trigger input is the glue's (a trig), the layer always NORMAL, no LEDs, no envelope out, no audio input
// The enums are Bastl's, in Bastl's order. MIT, Copyright 2026 amnmkkr.
#pragma once
#include <algorithm>   // (Bastl's Hardware.hpp brings these in for the code that includes it)
#include <memory>
#include <cstddef>
#include <cstdint>
#include "common/config.hpp"
#include "common/EnumTools.hpp"
#include "common/core/Kastle2_parameters.hpp"
#include "common/dsp/utility/EdgeDetector.hpp"
namespace kastle2 {
class Hardware {
 public:
  enum class AnalogInput { PITCH_1, PITCH_2, RESET, PARAM_3, PARAM_1, MODE, FEED_1, FEED_2, FEED_3, PARAM_2, POT_5,
                           POT_1, POT_4, POT_6, TRIG_IN, POT_7, POT_2, POT_3, COUNT };
  enum class Led { LED_1, LED_2, LED_3, COUNT };
  enum class Pot { POT_1, POT_2, POT_3, POT_4, POT_5, POT_6, POT_7, COUNT };
  enum class Layer { NORMAL, SHIFT, MODE, SETTINGS, SETTINGS_SHIFT, SETTINGS_MODE, COUNT };
  int32_t GetAnalogValue(AnalogInput i) const { return analog[(size_t)i]; }
  int32_t GetSafeCvMaxValue() const { return ADC_4V; }   // the Kastle 2's
  bool IsAudioInJackProbablyPlugged() const { return false; }
  void SetAnalogInputDirty(AnalogInput) {}
  bool GetAnalogInputDirty(AnalogInput) const { return false; }
  bool ReadPot(int32_t *value, Pot pot, Layer layer) const {
    const size_t k = (size_t)layer * (size_t)Pot::COUNT + (size_t)pot;
    if (!(pot_known >> k & 1u)) return false;
    *value = pots[k];
    return true;
  }
  bool GetTriggerIn() const { return trigger_in; }
  Layer GetLayer() const { return Layer::NORMAL; }
  void SetLed(Led, uint32_t) {}
  void LatchLeds() {}
  void SetEnvOut(int32_t) {}
  void SetDebugPin(int, int) {}
  // the glue's
  void SetPot(Pot pot, Layer layer, int32_t v) {
    const size_t k = (size_t)layer * (size_t)Pot::COUNT + (size_t)pot;
    pots[k] = v;
    pot_known |= (uint64_t)1 << k;
  }
  int32_t analog[(size_t)AnalogInput::COUNT];
  int32_t pots[(size_t)Layer::COUNT * (size_t)Pot::COUNT];
  uint64_t pot_known;
  bool trigger_in;
};
}  // namespace kastle2
