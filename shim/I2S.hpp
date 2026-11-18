// shim/I2S.hpp - the one value of Bastl's I2S driver the DSP reads: the audio block, 48 sample pairs.
// MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstddef>
struct I2S { static constexpr size_t kAudioBufferSize = 48; };
