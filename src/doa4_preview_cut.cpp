// Camera-cut "hold" in the menu / stage-select preview backgrounds.
//
// sub_823B7890 is a scene-script element handler (table 0x822C8F08, tag
// 0x40009F04). On each background camera cut it resolves the current frame
// into its texture (obj[16], when obj+72 is set) and then, every frame,
// queues a full-screen sprite of that still (byte (1 - f1) * 255) over the
// live scene for the transition's 20 frames (f1 = k/19, measured R38). It is
// transition type 1 of off_82C794C8, driven by the transition controller
// sub_823B6C30. The still sits over a camera that keeps moving (R33-R34
// constants), which reads as the picture holding after every cut (PIXHOLD 10
// frames per preview cut). Pinning f1 to 0.0 or 1.0 (R39/R40) both held it for
// all 20 frames, so the overlay is skipped outright: the handler only
// captures and queues the still; the camera switch and the controller's
// counters/states live elsewhere, so every cut becomes a clean hard cut.

#include "generated/doa4/doa4retail_init.h"

#include <rex/hook.h>

REX_HOOK_RAW(sub_823B7890) {
  (void)ctx;
  (void)base;
}
