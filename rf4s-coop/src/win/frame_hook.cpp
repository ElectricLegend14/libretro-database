#include "frame_hook.hpp"

#include <MinHook.h>

#include <mutex>

// The detour is a few lines of assembly (below for GCC/Clang, frame_thunk.asm
// for MSVC) rather than C++, so it can forward any signature untouched:
//
//   save rcx, rdx, r8, r9 and xmm0-3   (the argument registers)
//   call rf4s_coop_frame_callback      (co-op's work for this frame)
//   restore them
//   jmp to the original                (same stack, same return address)
//
// The original then runs exactly as if nothing had hooked it: arguments on
// the stack, float arguments and float or integer return values all survive.
extern "C" {
void* rf4s_coop_frame_original = nullptr;
void rf4s_coop_frame_thunk();
void rf4s_coop_frame_callback();
}

#if defined(__GNUC__)
asm(R"(
    .text
    .globl rf4s_coop_frame_thunk
rf4s_coop_frame_thunk:
    push %rcx
    push %rdx
    push %r8
    push %r9
    sub $0x68, %rsp
    movdqu %xmm0, 0x20(%rsp)
    movdqu %xmm1, 0x30(%rsp)
    movdqu %xmm2, 0x40(%rsp)
    movdqu %xmm3, 0x50(%rsp)
    call rf4s_coop_frame_callback
    movdqu 0x20(%rsp), %xmm0
    movdqu 0x30(%rsp), %xmm1
    movdqu 0x40(%rsp), %xmm2
    movdqu 0x50(%rsp), %xmm3
    add $0x68, %rsp
    pop %r9
    pop %r8
    pop %rdx
    pop %rcx
    jmp *rf4s_coop_frame_original(%rip)
)");
#endif

namespace coop {
namespace {

void* g_target = nullptr;
std::mutex g_mutex;
std::function<void()> g_onFrame;

}  // namespace
}  // namespace coop

// Runs on the game's thread, once per frame, before the original.
extern "C" void rf4s_coop_frame_callback() {
  std::lock_guard lock(coop::g_mutex);
  if (!coop::g_onFrame) return;
  try {
    coop::g_onFrame();
  } catch (...) {
    // Never let an exception unwind into the game: the thunk has no unwind data.
  }
}

namespace coop {

bool FrameHook::install(void* target, std::function<void()> onFrame, std::string& error) {
  if (g_target) {
    error = "a frame hook is already installed";
    return false;
  }
  MH_STATUS status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
    error = MH_StatusToString(status);
    return false;
  }
  {
    std::lock_guard lock(g_mutex);
    g_onFrame = std::move(onFrame);
  }
  status = MH_CreateHook(target, reinterpret_cast<void*>(&rf4s_coop_frame_thunk), &rf4s_coop_frame_original);
  if (status == MH_OK) status = MH_EnableHook(target);
  if (status != MH_OK) {
    error = MH_StatusToString(status);
    std::lock_guard lock(g_mutex);
    g_onFrame = nullptr;
    return false;
  }
  g_target = target;
  return true;
}

void FrameHook::uninstall() {
  {
    std::lock_guard lock(g_mutex);  // waits out a callback in progress
    g_onFrame = nullptr;
  }
  if (!g_target) return;
  MH_DisableHook(g_target);
  MH_RemoveHook(g_target);
  g_target = nullptr;
}

}  // namespace coop
