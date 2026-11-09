// True widescreen (Hor+) 3D hook. Design: docs/hooks.md.

#include "generated/doa4/doa4retail_init.h"

#include <atomic>
#include <cmath>

#include <windows.h>

#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_DOUBLE(aspect_ratio, 0.0, "DOA4",
                      "Screen aspect ratio for the 3D camera, e.g. 2.3889 for 3440x1440. "
                      "-1 = auto (match the game window), 0 = game default (16:9). "
                      "Use with present_letterbox=false.");

REX_EXTERN(__imp__sub_8231A3D8);
REX_EXTERN(__imp__sub_8231A658);

namespace {

// Client-area shape of this process's visible top-level window (the game
// window; fullscreen = the monitor it is on), or 0 if there is none yet.
double WindowAspect() {
  static std::atomic<HWND> cached{nullptr};
  HWND hwnd = cached.load(std::memory_order_relaxed);
  if (!hwnd || !IsWindow(hwnd)) {
    hwnd = nullptr;
    EnumWindows(
        [](HWND h, LPARAM out) -> BOOL {
          DWORD pid = 0;
          GetWindowThreadProcessId(h, &pid);
          if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
          *reinterpret_cast<HWND*>(out) = h;
          return FALSE;
        },
        reinterpret_cast<LPARAM>(&hwnd));
    cached.store(hwnd, std::memory_order_relaxed);
  }
  RECT r{};
  if (!hwnd || !GetClientRect(hwnd, &r) || r.bottom <= 0) return 0.0;
  return double(r.right) / double(r.bottom);
}

double Widen(double aspect) {
  double target = REXCVAR_GET(aspect_ratio);
  if (target < 0.0) target = WindowAspect();
  return target > 0.0 && std::abs(aspect - 16.0 / 9.0) < 1e-3 ? target : aspect;
}

}  // namespace

REX_HOOK_RAW(sub_8231A3D8) {
  ctx.f2.f64 = Widen(ctx.f2.f64);
  __imp__sub_8231A3D8(ctx, base);
}

REX_HOOK_RAW(sub_8231A658) {
  ctx.f3.f64 = Widen(ctx.f3.f64);
  __imp__sub_8231A658(ctx, base);
}
