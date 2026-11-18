// shim/hardware/sync.h - Svf.cpp guards a coefficient update against the Pico's interrupts; the Sidecar calls the effect
// from one thread, so there is nothing to guard. MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstdint>
static inline uint32_t save_and_disable_interrupts() { return 0; }
static inline void restore_interrupts(uint32_t) {}
