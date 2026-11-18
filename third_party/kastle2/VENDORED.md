# Vendored: Kastle 2 Wave Bard and its DSP library (Bastl Instruments)

From https://github.com/bastl-instruments/kastle2 at commit `a4a7a0451038bc4ed4719f46573c3eff92e7c7ce`, MIT
(`LICENSE`, Copyright (c) 2023-2025 Bastl Instruments), unchanged:

- `code/src/apps/WaveBard/`: the Wave Bard app (AppWaveBard, its parameter maps, file format, cc headers), without
  `SAMPLES.bin` (Bastl's sample bank: not part of this repository or the package)
- `code/src/common/`: the Kastle 2 platform. Compiled: `controls/FancyPot.cpp`, `core/midi/Message.cpp`,
  `dsp/control/{AdsrEnv,EnvelopeFollower}.cpp`, `dsp/effects/{HardClipper,SoftClipper}.cpp`,
  `dsp/filters/{DjFilterStereo,SvfStereo,Svf}.cpp`, `dsp/synthesis/Oscillator.cpp`, `dsp/utility/Quantizer.cpp` and
  the headers they and Wave Bard include. The rest is kept as it is in the repository and not built.

No file here is changed. The platform pieces Wave Bard calls that need the Kastle's hardware (knobs, layers, CV
inputs, trigger, clock, second core, LEDs, memory, MIDI, the Pico's clock) are replaced by the headers in
`../../shim/`, which come first on the include path; two of them are Bastl's own with one change each, said at their
top: `UserDataFile.hpp` (reads the sample file from where the glue puts it) and `dsp/sampling/SamplePlayer.hpp` (reads
its frames from the Sidecar's sample pool). See `../../wavebard.cc`.
