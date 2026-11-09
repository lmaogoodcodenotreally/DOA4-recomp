// True widescreen, 3D part.
//
// Every 3D camera builds its projection in sub_8231A3D8(fov f1, aspect f2,
// near f3, far f4): m00 = 1 / (tan(fov/2) * aspect), m11 = 1 / tan(fov/2), so
// the field of view is vertical and a wider aspect shows more at the sides
// (Hor+). Its view-frustum twin sub_8231A658(fov f1, ?, aspect f3, near f4,
// far f5) feeds culling (sub_8276C160). The HD cameras pass 16:9 (constant
// 0x822BCAAC, or 1 / 0.5625 in sub_8231A4D8); other callers pass their own
// shape (4:3 constant 0x822BCA40, render-to-texture cameras) and are left
// alone. The guest still renders 1280x720: the presenter stretches it to the
// window (present_letterbox=false), and the wider projection makes that
// stretch come out geometrically correct.

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
