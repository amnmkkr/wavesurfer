// shim/common/core/MultiCore.hpp - the Kastle runs FX Wizard's DJ filter, feedback and dry/wet (SecondCoreProcess) on its
// second core, a sample behind the first. Here a SAMPLE_REQUEST runs it at once, in the same order: the glue sets
// the hook. MIT, Copyright 2026 amnmkkr.
#pragma once
#include <cstddef>
namespace kastle2 {
namespace MultiCore {
enum class MessageType { BEGIN, SAMPLE_REQUEST, DONE };
struct Message { MessageType type; size_t data; };
extern void (*sample_hook)(size_t index);
inline void SendMessage(MessageType t, size_t data = 0) {
  if (t == MessageType::SAMPLE_REQUEST && sample_hook) sample_hook(data);
}
inline bool HasMessage() { return false; }
inline Message GetMessage() { return Message{MessageType::DONE, 0}; }
inline void WaitForMessage(MessageType) {}
}  // namespace MultiCore
}  // namespace kastle2
