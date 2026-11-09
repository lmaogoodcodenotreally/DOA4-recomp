// TEMP probe (frame interpolation RE, 2026-10-16): which part of one main-loop
// iteration issues the GPU work. Every direct child of frame_systems_tick
// (sub_824F4878) and of the main loop (sub_824F41E0) is wrapped; the
// outermost wrapped function on the stack is the "tag", and every D3D
// draw/clear/resolve primitive (found by their PM4 DRAW_INDX/DRAW_INDX_2
// headers) counts against the current tag. Static reach cannot see vtable
// draws, this can. Every 600 presents it logs DRAWPROBE rows per
// (tag, game mode, loading gate) with per-frame averages.
// Dev build only, inert unless DOA4_DRAWPROBE=1.

#include "generated/doa4/doa4retail_init.h"

#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <tuple>

#include <rex/hook.h>
#include <rex/logging/macros.h>

#ifndef DOA4_LEAN
#define DOA4_LEAN 0
#endif

#if !DOA4_LEAN

namespace {

bool Enabled() {
  static const bool on = [] {
    const char* v = std::getenv("DOA4_DRAWPROBE");
    return v && v[0] == '1';
  }();
  return on;
}

thread_local uint32_t g_tag = 0;
thread_local int g_depth = 0;
thread_local bool g_main = false;  // set on the thread that runs present_flip
uint32_t g_mode = 0;                // fs | gate<<8, sampled per tick

struct Row {
  uint64_t calls = 0, draws = 0, clears = 0, resolves = 0;
};
std::mutex g_mu;
std::map<std::tuple<uint32_t, uint32_t>, Row> g_rows;  // (tag, mode)
std::map<uint32_t, uint64_t> g_frames_per_mode;
uint64_t g_frames = 0;

struct Scope {
  bool outer;
  uint32_t prev = 0;
  explicit Scope(uint32_t addr) : outer(g_depth == 0) {
    if (outer) {
      prev = g_tag;
      g_tag = addr;
      std::lock_guard lk(g_mu);
      ++g_rows[{addr, g_mode}].calls;
    }
    ++g_depth;
  }
  ~Scope() {
    --g_depth;
    if (outer) g_tag = prev;
  }
};

void Count(int kind) {
  // tag 1 = primitive on a non-main thread, 0 = main thread outside any tag
  uint32_t tag = g_main ? g_tag : 1;
  std::lock_guard lk(g_mu);
  Row& r = g_rows[{tag, g_mode}];
  (kind == 0 ? r.draws : kind == 1 ? r.clears : r.resolves)++;
}

void Dump() {
  std::lock_guard lk(g_mu);
  for (auto& [k, r] : g_rows) {
    auto [tag, mode] = k;
    double n = double(g_frames_per_mode[mode] ? g_frames_per_mode[mode] : 1);
    REXLOG_INFO(
        "DRAWPROBE tag={:08X} fs={} gate={} frames={} calls/f={:.2f} draws/f={:.1f} "
        "clears/f={:.2f} resolves/f={:.2f}",
        tag, mode & 0xFF, mode >> 8, g_frames_per_mode[mode], r.calls / n, r.draws / n,
        r.clears / n, r.resolves / n);
  }
  g_rows.clear();
  g_frames_per_mode.clear();
}

}  // namespace

#define DOA4_PROBE(a)                       \
  REX_EXTERN(__imp__sub_##a);               \
  REX_HOOK_RAW(sub_##a) {                   \
    if (!Enabled()) {                       \
      __imp__sub_##a(ctx, base);            \
      return;                               \
    }                                       \
    Scope s(0x##a);                         \
    __imp__sub_##a(ctx, base);              \
  }

#define DOA4_PRIM(a, kind)                  \
  REX_EXTERN(__imp__sub_##a);               \
  REX_HOOK_RAW(sub_##a) {                   \
    if (Enabled()) Count(kind);             \
    __imp__sub_##a(ctx, base);              \
  }

// main loop body
DOA4_PROBE(824F4498)
DOA4_PROBE(824F0F78)
DOA4_PROBE(824F0B20)
DOA4_PROBE(824F34D8)
// frame_systems_tick children, in call order
DOA4_PROBE(824F31E8)
DOA4_PROBE(82401CD8)
DOA4_PROBE(823FBE18)
DOA4_PROBE(824F4CE8)
DOA4_PROBE(82566430)
DOA4_PROBE(82354DB0)
DOA4_PROBE(82542BF8)
DOA4_PROBE(825413D0)
DOA4_PROBE(82541538)
DOA4_PROBE(8231DCD8)
DOA4_PROBE(8245E580)
DOA4_PROBE(82566818)
DOA4_PROBE(823EACB8)
DOA4_PROBE(824B2F78)
DOA4_PROBE(82566B80)
DOA4_PROBE(824B2EE0)
DOA4_PROBE(824F0F00)
DOA4_PROBE(82566920)
DOA4_PROBE(82354EB8)
DOA4_PROBE(82354F30)
DOA4_PROBE(8231D750)
DOA4_PROBE(824B2DC8)
DOA4_PROBE(823B43D0)
DOA4_PROBE(8231D248)
DOA4_PROBE(82449EA8)
DOA4_PROBE(8244B690)
DOA4_PROBE(8247F388)
DOA4_PROBE(82453430)
DOA4_PROBE(82308A10)
DOA4_PROBE(824F10D8)
DOA4_PROBE(8242B778)
DOA4_PROBE(8242A5D8)
DOA4_PROBE(823F8A10)
DOA4_PROBE(824519A0)
DOA4_PROBE(8277A248)
DOA4_PROBE(8277A308)
DOA4_PROBE(82758A48)
DOA4_PROBE(82758DB0)
DOA4_PROBE(82472ED8)
DOA4_PROBE(82451F20)
DOA4_PROBE(824524A8)
DOA4_PROBE(8245D718)
DOA4_PROBE(82354678)
DOA4_PROBE(823B4BB8)
DOA4_PROBE(82475648)
DOA4_PROBE(824F3E68)
DOA4_PROBE(823886D8)

DOA4_PRIM(82750AC8, 0)
DOA4_PRIM(82750D68, 0)
DOA4_PRIM(82750FA8, 0)
DOA4_PRIM(8274ECA8, 0)
DOA4_PRIM(8274F0C0, 1)
DOA4_PRIM(8274FDD8, 2)

// The loading gate: r3 != 0 = the transition/loading branch ran instead of
// the gameplay branch (sub_82566B80 ... sub_82354F30).
REX_EXTERN(__imp__sub_824F4E90);
REX_HOOK_RAW(sub_824F4E90) {
  if (!Enabled()) {
    __imp__sub_824F4E90(ctx, base);
    return;
  }
  {
    Scope s(0x824F4E90);
    __imp__sub_824F4E90(ctx, base);
  }
  g_mode = (g_mode & 0xFF) | ((ctx.r3.u32 & 0xFF) ? 0x100u : 0u);
}

REX_EXTERN(__imp__sub_824F4398);
REX_HOOK_RAW(sub_824F4398) {
  if (!Enabled()) {
    __imp__sub_824F4398(ctx, base);
    return;
  }
  g_main = true;
  {
    Scope s(0x824F4398);
    __imp__sub_824F4398(ctx, base);
  }
  {
    std::lock_guard lk(g_mu);
    ++g_frames_per_mode[g_mode];
  }
  // next tick's mode: game mode byte (fs, see doa4retail_app.h), gate resets
  g_mode = base[0x83035CC0u];
  if (++g_frames % 600 == 0) Dump();
}

#endif  // !DOA4_LEAN
