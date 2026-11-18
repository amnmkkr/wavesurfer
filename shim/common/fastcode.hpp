// shim/common/fastcode.hpp - FASTCODE puts a function in the Pico's RAM section; on the Sidecar it is an ordinary
// function (an extra section would be dropped by the plugin's linker script). MIT, Copyright 2026 amnmkkr.
#pragma once
#define FASTCODE
