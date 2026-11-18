// shim/pico/time.h - the Pico SDK's microsecond clock, as Wave Bard's double-trigger guard reads it. Here it counts
// the 44 kHz samples the voice has rendered (the glue advances it a block at a time), so the guard's 20 ms is 20 ms
// of the voice's audio. MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstdint>
typedef uint64_t absolute_time_t;
namespace kastle2 { extern uint64_t g_time_us; }
static inline absolute_time_t get_absolute_time() { return kastle2::g_time_us; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t)(to - from); }
static inline void sleep_ms(uint32_t) {}
