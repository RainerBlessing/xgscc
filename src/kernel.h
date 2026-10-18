#ifndef KERNEL_H_
#define KERNEL_H_
#include <string>

/**
 * The video kernel of the XGS ME. The picture is a signal that the program writes
 * to port RE, to the clock cycle; see doc/video.md. The kernel is assembler that
 * the compiler does not touch.
 */

const int XGS_LINE_CYCLES = 5080;       // 63.5 us at 80 MHz
const int XGS_VSYNC_CYCLES = 19283;     // the long sync that starts a frame
const int XGS_FRAME_CYCLES = 1330956;   // 60.1 Hz, like the example programs

/**
 * Assembler source with two routines:
 *
 *   xgs_init     sets port RE to output, the RTCC to count every 256th cycle, the
 *                level to blanking.
 *   xgs_frame    waits until the frame period since the last frame is over (the
 *                RTCC measures what the game has done meanwhile, up to 65 000
 *                cycles), then makes the vertical sync and `lines` picture lines of
 *                one colour (5080 cycles each), and returns with the level held
 *                at blanking.
 *
 * Both are called with `call` and end with `retp`. They use the registers
 * xgs_count, xgs_n, xgs_hi and xgs_lo, which the caller defines (equ).
 * The routines are put in the first half of a page and stay on one page.
 *
 * colour is the byte for RE: luminance in the low nibble, chroma in the high one.
 */
std::string xgsKernelSource(int lines, int colour);

#endif /*KERNEL_H_*/
